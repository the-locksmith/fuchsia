// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_device_cache.h"

#include <fbl/function.h>
#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/async/default.h"
#include "src/lib/uuid/uuid.h"

namespace btlib {
namespace gap {

using common::DeviceAddress;

namespace {

// Return an address with the same value as given, but with type kBREDR for
// kLEPublic addresses and vice versa.
DeviceAddress GetAliasAddress(const DeviceAddress& address) {
  if (address.type() == DeviceAddress::Type::kBREDR) {
    return {DeviceAddress::Type::kLEPublic, address.value()};
  } else if (address.type() == DeviceAddress::Type::kLEPublic) {
    return {DeviceAddress::Type::kBREDR, address.value()};
  }
  return address;
}

}  // namespace

RemoteDevice* RemoteDeviceCache::NewDevice(const DeviceAddress& address,
                                           bool connectable) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  auto* const device =
      InsertDeviceRecord(uuid::Generate(), address, connectable);
  if (device) {
    UpdateExpiry(*device);
    NotifyDeviceUpdated(*device);
  }
  return device;
}

void RemoteDeviceCache::ForEach(DeviceCallback f) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(f);
  for (const auto& iter : devices_) {
    f(*iter.second.device());
  }
}

bool RemoteDeviceCache::AddBondedDevice(
    const std::string& identifier, const DeviceAddress& address,
    const sm::PairingData& bond_data, const std::optional<sm::LTK>& link_key) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(!bond_data.identity_address ||
                  address.value() == bond_data.identity_address->value());
  ZX_DEBUG_ASSERT(address.type() != DeviceAddress::Type::kLEAnonymous);

  const bool bond_le = bond_data.ltk || bond_data.csrk;
  const bool bond_bredr = link_key.has_value();

  // |bond_data| must contain either a LTK or CSRK for LE Security Mode 1 or 2.
  if (address.IsLowEnergy() && !bond_le) {
    bt_log(ERROR, "gap-le", "mandatory keys missing: no LTK or CSRK (id: %s)",
           identifier.c_str());
    return false;
  }

  if (address.IsBrEdr() && !bond_bredr) {
    bt_log(ERROR, "gap-bredr", "mandatory link key missing (id: %s)",
           identifier.c_str());
    return false;
  }

  auto* device = InsertDeviceRecord(identifier, address, true);
  if (!device) {
    return false;
  }

  // A bonded device must have its identity known.
  device->set_identity_known(true);

  if (bond_le) {
    device->MutLe().SetBondData(bond_data);
    ZX_DEBUG_ASSERT(device->le()->bonded());

    // Add the device to the resolving list if it has an IRK.
    if (bond_data.irk) {
      le_resolving_list_.Add(device->address(), bond_data.irk->value());
    }
  }

  if (bond_bredr) {
    device->MutBrEdr().SetLinkKey(*link_key);
    ZX_DEBUG_ASSERT(device->bredr()->bonded());
  }

  if (device->technology() == TechnologyType::kDualMode) {
    address_map_[GetAliasAddress(address)] = identifier;
  }

  ZX_DEBUG_ASSERT(!device->temporary());
  ZX_DEBUG_ASSERT(device->bonded());
  bt_log(SPEW, "gap", "restored bonded device: %s, id: %s",
         address.ToString().c_str(), identifier.c_str());

  // Don't call UpdateExpiry(). Since a bonded device starts out as
  // non-temporary it is not necessary to ever set up the expiration callback.
  NotifyDeviceUpdated(*device);
  return true;
}

bool RemoteDeviceCache::StoreLowEnergyBond(const std::string& identifier,
                                           const sm::PairingData& bond_data) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  auto* device = FindDeviceById(identifier);
  if (!device) {
    bt_log(TRACE, "gap-le", "failed to store bond for unknown device: %s",
           identifier.c_str());
    return false;
  }

  // Either a LTK or CSRK is mandatory for bonding (the former is needed for LE
  // Security Mode 1 and the latter is needed for Mode 2).
  if (!bond_data.ltk && !bond_data.csrk) {
    bt_log(TRACE, "gap-le", "mandatory keys missing: no IRK or CSRK (id: %s)",
           identifier.c_str());
    return false;
  }

  if (bond_data.identity_address) {
    auto existing_id = FindIdByAddress(*bond_data.identity_address);
    if (!existing_id) {
      // Map the new address to |device|. We leave old addresses that map to
      // this device in the cache in case there are any pending controller
      // procedures that expect them.
      // TODO(armansito): Maybe expire the old address after a while?
      address_map_[*bond_data.identity_address] = identifier;
    } else if (existing_id->get() != identifier) {
      bt_log(TRACE, "gap-le",
             "identity address %s for device %s belongs to another device %s!",
             bond_data.identity_address->ToString().c_str(), identifier.c_str(),
             existing_id->get().c_str());
      return false;
    }
    // We have either created a new mapping or the identity address already
    // maps to this device.
  }

  // TODO(BT-619): Check that we're not downgrading the security level before
  // overwriting the bond.
  device->MutLe().SetBondData(bond_data);
  ZX_DEBUG_ASSERT(!device->temporary());
  ZX_DEBUG_ASSERT(device->le()->bonded());

  // Add the device to the resolving list if it has an IRK.
  if (device->identity_known() && bond_data.irk) {
    le_resolving_list_.Add(device->address(), bond_data.irk->value());
  }

  // Report the bond for persisting only if the identity of the device is known.
  if (device->identity_known()) {
    NotifyDeviceBonded(*device);
  }
  return true;
}

bool RemoteDeviceCache::StoreBrEdrBond(const common::DeviceAddress& address,
                                       const sm::LTK& link_key) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(address.type() == common::DeviceAddress::Type::kBREDR);
  auto* device = FindDeviceByAddress(address);
  if (!device) {
    bt_log(TRACE, "gap-bredr", "failed to store bond for unknown device: %s",
           address.ToString().c_str());
    return false;
  }

  // TODO(BT-619): Check that we're not downgrading the security level before
  // overwriting the bond.
  device->MutBrEdr().SetLinkKey(link_key);
  ZX_DEBUG_ASSERT(!device->temporary());
  ZX_DEBUG_ASSERT(device->bredr()->bonded());

  NotifyDeviceBonded(*device);
  return true;
}

RemoteDevice* RemoteDeviceCache::FindDeviceById(
    const std::string& identifier) const {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  auto iter = devices_.find(identifier);
  return iter != devices_.end() ? iter->second.device() : nullptr;
}

RemoteDevice* RemoteDeviceCache::FindDeviceByAddress(
    const DeviceAddress& in_address) const {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());

  std::optional<DeviceAddress> address;
  if (in_address.IsResolvablePrivate()) {
    address = le_resolving_list_.Resolve(in_address);
  }

  // Fall back to the input if an identity wasn't resolved.
  if (!address) {
    address = in_address;
  }

  ZX_DEBUG_ASSERT(address);
  auto identifier = FindIdByAddress(*address);
  if (!identifier) {
    return nullptr;
  }

  auto* dev = FindDeviceById(identifier->get());
  ZX_DEBUG_ASSERT(dev);
  return dev;
}

// Private methods below.

RemoteDevice* RemoteDeviceCache::InsertDeviceRecord(
    const std::string& identifier, const common::DeviceAddress& address,
    bool connectable) {
  if (FindIdByAddress(address)) {
    bt_log(WARN, "gap", "tried to insert device with existing address: %s",
           address.ToString().c_str());
    return nullptr;
  }

  std::unique_ptr<RemoteDevice> device(new RemoteDevice(
      fit::bind_member(this, &RemoteDeviceCache::NotifyDeviceUpdated),
      fit::bind_member(this, &RemoteDeviceCache::UpdateExpiry),
      fit::bind_member(this, &RemoteDeviceCache::MakeDualMode), identifier,
      address, connectable));
  // Note: we must construct the RemoteDeviceRecord in-place, because it doesn't
  // support copy or move.
  auto [iter, inserted] =
      devices_.try_emplace(device->identifier(), std::move(device),
                           [this, d = device.get()] { RemoveDevice(d); });
  if (!inserted) {
    bt_log(WARN, "gap", "tried to insert device with existing ID: %s",
           identifier.c_str());
    return nullptr;
  }

  address_map_[address] = identifier;
  return iter->second.device();
}

void RemoteDeviceCache::NotifyDeviceBonded(const RemoteDevice& device) {
  ZX_DEBUG_ASSERT(devices_.find(device.identifier()) != devices_.end());
  ZX_DEBUG_ASSERT(devices_.at(device.identifier()).device() == &device);
  ZX_DEBUG_ASSERT_MSG(device.identity_known(),
                      "devices not allowed to bond with unknown identity!");

  bt_log(INFO, "gap", "peer bonded %s", device.ToString().c_str());
  if (device_bonded_callback_) {
    device_bonded_callback_(device);
  }
}

void RemoteDeviceCache::NotifyDeviceUpdated(const RemoteDevice& device) {
  ZX_DEBUG_ASSERT(devices_.find(device.identifier()) != devices_.end());
  ZX_DEBUG_ASSERT(devices_.at(device.identifier()).device() == &device);
  if (device_updated_callback_)
    device_updated_callback_(device);
}

void RemoteDeviceCache::UpdateExpiry(const RemoteDevice& device) {
  auto device_record_iter = devices_.find(device.identifier());
  ZX_DEBUG_ASSERT(device_record_iter != devices_.end());

  auto& device_record = device_record_iter->second;
  ZX_DEBUG_ASSERT(device_record.device() == &device);

  const auto cancel_res = device_record.removal_task()->Cancel();
  ZX_DEBUG_ASSERT(cancel_res == ZX_OK || cancel_res == ZX_ERR_NOT_FOUND);

  // Previous expiry task has been canceled. Re-schedule only if the device is
  // temporary.
  if (device.temporary()) {
    const auto schedule_res = device_record.removal_task()->PostDelayed(
        async_get_default_dispatcher(), kCacheTimeout);
    ZX_DEBUG_ASSERT(schedule_res == ZX_OK || schedule_res == ZX_ERR_BAD_STATE);
  }
}

void RemoteDeviceCache::MakeDualMode(const RemoteDevice& device) {
  ZX_DEBUG_ASSERT(address_map_.at(device.address()) == device.identifier());
  const auto address_alias = GetAliasAddress(device.address());
  auto [iter, inserted] =
      address_map_.try_emplace(address_alias, device.identifier());
  ZX_DEBUG_ASSERT_MSG(inserted || iter->second == device.identifier(),
                      "%s can't become dual-mode because %s maps to %s",
                      device.identifier().c_str(),
                      address_alias.ToString().c_str(), iter->second.c_str());

  // The device became dual mode in lieu of adding a new device but is as
  // significant, so notify listeners of the change.
  NotifyDeviceUpdated(device);
}

void RemoteDeviceCache::RemoveDevice(RemoteDevice* device) {
  ZX_DEBUG_ASSERT(device);

  auto device_record_it = devices_.find(device->identifier());
  ZX_DEBUG_ASSERT(device_record_it != devices_.end());
  ZX_DEBUG_ASSERT(device_record_it->second.device() == device);

  const std::string identifier_copy = device->identifier();
  for (auto iter = address_map_.begin(); iter != address_map_.end();) {
    if (iter->second == device->identifier()) {
      iter = address_map_.erase(iter);
    } else {
      iter++;
    }
  }
  devices_.erase(device_record_it);  // Destroys |device|.
  if (device_removed_callback_) {
    device_removed_callback_(identifier_copy);
  }
}

// This returns string cref instead of string_view because the caller is likely
// to use the ID to index into |devices_|.
// TODO(BT-305): This would be less messy using an integer ID type.
std::optional<std::reference_wrapper<const std::string>>
RemoteDeviceCache::FindIdByAddress(const common::DeviceAddress& address) const {
  auto iter = address_map_.find(address);
  if (iter == address_map_.end()) {
    // Search again using the other technology's address. This is necessary when
    // a dual-mode device is known by only one technology and is then discovered
    // or connected on its other technology.
    iter = address_map_.find(GetAliasAddress(address));
  }

  if (iter == address_map_.end()) {
    return {};
  }
  return {iter->second};
}

}  // namespace gap
}  // namespace btlib
