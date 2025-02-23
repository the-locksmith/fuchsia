// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"

#include "garnet/drivers/bluetooth/lib/common/device_class.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/common/uint128.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "garnet/drivers/bluetooth/lib/sm/types.h"
#include "garnet/drivers/bluetooth/lib/sm/util.h"

namespace btlib {
namespace gap {
namespace {

using common::CreateStaticByteBuffer;
using common::DeviceAddress;
using common::MutableBufferView;
using common::StaticByteBuffer;

// All fields are initialized to zero as they are unused in these tests.
const hci::LEConnectionParameters kTestParams;

constexpr int8_t kTestRSSI = 10;

const DeviceAddress kAddrBrEdr(DeviceAddress::Type::kBREDR,
                               "AA:BB:CC:DD:EE:FF");
const DeviceAddress kAddrLePublic(DeviceAddress::Type::kLEPublic,
                                  "01:02:03:04:05:06");
// LE Public Device Address that has the same value as a BR/EDR BD_ADDR, e.g. on
// a dual-mode device.
const DeviceAddress kAddrLeAlias(DeviceAddress::Type::kLEPublic,
                                 "AA:BB:CC:DD:EE:FF");

// TODO(armansito): Make these adhere to privacy specification.
const DeviceAddress kAddrLeRandom(DeviceAddress::Type::kLERandom,
                                  "06:05:04:03:02:01");
const DeviceAddress kAddrLeRandom2(DeviceAddress::Type::kLERandom,
                                   "FF:EE:DD:CC:BB:AA");
const DeviceAddress kAddrLeAnon(DeviceAddress::Type::kLEAnonymous,
                                "06:05:04:03:02:01");

const auto kAdvData =
    CreateStaticByteBuffer(0x05,  // Length
                           0x09,  // AD type: Complete Local Name
                           'T', 'e', 's', 't');
const auto kEirData = kAdvData;

const btlib::sm::LTK kLTK;
const btlib::sm::Key kKey{};

const btlib::sm::LTK kBrEdrKey;

// Phone (Networking)
const common::DeviceClass kTestDeviceClass({0x06, 0x02, 0x02});

class GAP_RemoteDeviceCacheTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() {}
  void TearDown() { RunLoopUntilIdle(); }

 protected:
  // Creates a new RemoteDevice, and caches a pointer to that device.
  __WARN_UNUSED_RESULT bool NewDevice(const DeviceAddress& addr,
                                      bool connectable) {
    auto* dev = cache_.NewDevice(addr, connectable);
    if (!dev) {
      return false;
    }
    device_ = dev;
    return true;
  }

  RemoteDeviceCache* cache() { return &cache_; }
  // Returns the cached pointer to the device created in the most recent call to
  // NewDevice(). The caller must ensure that the device has not expired out of
  // the cache. (Tests of cache expiration should generally subclass the
  // GAP_RemoteDeviceCacheExpirationTest fixture.)
  RemoteDevice* device() { return device_; }

 private:
  RemoteDeviceCache cache_;
  RemoteDevice* device_;
};

TEST_F(GAP_RemoteDeviceCacheTest, LookUp) {
  auto kAdvData0 = CreateStaticByteBuffer(0x05, 0x09, 'T', 'e', 's', 't');
  auto kAdvData1 = CreateStaticByteBuffer(0x0C, 0x09, 'T', 'e', 's', 't', ' ',
                                          'D', 'e', 'v', 'i', 'c', 'e');

  EXPECT_FALSE(cache()->FindDeviceByAddress(kAddrLePublic));
  EXPECT_FALSE(cache()->FindDeviceById("foo"));

  auto device = cache()->NewDevice(kAddrLePublic, true);
  ASSERT_TRUE(device);
  ASSERT_TRUE(device->le());
  EXPECT_EQ(TechnologyType::kLowEnergy, device->technology());
  EXPECT_TRUE(device->connectable());
  EXPECT_TRUE(device->temporary());
  EXPECT_EQ(kAddrLePublic, device->address());
  EXPECT_EQ(0u, device->le()->advertising_data().size());
  EXPECT_EQ(hci::kRSSIInvalid, device->rssi());

  // A look up should return the same instance.
  EXPECT_EQ(device, cache()->FindDeviceById(device->identifier()));
  EXPECT_EQ(device, cache()->FindDeviceByAddress(device->address()));

  // Adding a device with the same address should return nullptr.
  EXPECT_FALSE(cache()->NewDevice(kAddrLePublic, true));

  device->MutLe().SetAdvertisingData(kTestRSSI, kAdvData1);
  EXPECT_TRUE(
      common::ContainersEqual(kAdvData1, device->le()->advertising_data()));
  EXPECT_EQ(kTestRSSI, device->rssi());

  device->MutLe().SetAdvertisingData(kTestRSSI, kAdvData0);
  EXPECT_TRUE(
      common::ContainersEqual(kAdvData0, device->le()->advertising_data()));
  EXPECT_EQ(kTestRSSI, device->rssi());
}

TEST_F(GAP_RemoteDeviceCacheTest, LookUpBrEdrDeviceByLePublicAlias) {
  ASSERT_FALSE(cache()->FindDeviceByAddress(kAddrLeAlias));
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));
  auto* dev = cache()->FindDeviceByAddress(kAddrBrEdr);
  ASSERT_TRUE(dev);
  EXPECT_EQ(device(), dev);

  dev = cache()->FindDeviceByAddress(kAddrLeAlias);
  ASSERT_TRUE(dev);
  EXPECT_EQ(device(), dev);
  EXPECT_EQ(DeviceAddress::Type::kBREDR, dev->address().type());
}

TEST_F(GAP_RemoteDeviceCacheTest, LookUpLeDeviceByBrEdrAlias) {
  EXPECT_FALSE(cache()->FindDeviceByAddress(kAddrBrEdr));
  ASSERT_TRUE(NewDevice(kAddrLeAlias, true));
  auto* dev = cache()->FindDeviceByAddress(kAddrLeAlias);
  ASSERT_TRUE(dev);
  EXPECT_EQ(device(), dev);

  dev = cache()->FindDeviceByAddress(kAddrBrEdr);
  ASSERT_TRUE(dev);
  EXPECT_EQ(device(), dev);
  EXPECT_EQ(DeviceAddress::Type::kLEPublic, dev->address().type());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       NewDeviceDoesNotCrashWhenNoCallbackIsRegistered) {
  RemoteDeviceCache().NewDevice(kAddrLePublic, true);
}

TEST_F(GAP_RemoteDeviceCacheTest, ForEachEmpty) {
  bool found = false;
  cache()->ForEach([&](const auto&) { found = true; });
  EXPECT_FALSE(found);
}

TEST_F(GAP_RemoteDeviceCacheTest, ForEach) {
  int count = 0;
  ASSERT_TRUE(NewDevice(kAddrLePublic, true));
  cache()->ForEach([&](const auto& dev) {
    count++;
    EXPECT_EQ(device()->identifier(), dev.identifier());
    EXPECT_EQ(device()->address(), dev.address());
  });
  EXPECT_EQ(1, count);
}

TEST_F(GAP_RemoteDeviceCacheTest,
       NewDeviceInvokesCallbackWhenDeviceIsFirstRegistered) {
  bool was_called = false;
  cache()->set_device_updated_callback(
      [&was_called](const auto&) { was_called = true; });
  cache()->NewDevice(kAddrLePublic, true);
  EXPECT_TRUE(was_called);
}

TEST_F(GAP_RemoteDeviceCacheTest,
       NewDeviceDoesNotInvokeCallbackWhenDeviceIsReRegistered) {
  int call_count = 0;
  cache()->set_device_updated_callback(
      [&call_count](const auto&) { ++call_count; });
  cache()->NewDevice(kAddrLePublic, true);
  cache()->NewDevice(kAddrLePublic, true);
  EXPECT_EQ(1, call_count);
}

TEST_F(GAP_RemoteDeviceCacheTest, NewDeviceIdentityKnown) {
  EXPECT_TRUE(cache()->NewDevice(kAddrBrEdr, true)->identity_known());
  EXPECT_TRUE(cache()->NewDevice(kAddrLePublic, true)->identity_known());
  EXPECT_FALSE(cache()->NewDevice(kAddrLeRandom, true)->identity_known());
  EXPECT_FALSE(cache()->NewDevice(kAddrLeAnon, false)->identity_known());
}

TEST_F(GAP_RemoteDeviceCacheTest, NewDeviceInitialTechnologyIsClassic) {
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));

  // A device initialized with a BR/EDR address should start out as a
  // classic-only.
  ASSERT_TRUE(device());
  EXPECT_TRUE(device()->bredr());
  EXPECT_FALSE(device()->le());
  EXPECT_TRUE(device()->identity_known());
  EXPECT_EQ(TechnologyType::kClassic, device()->technology());
}

TEST_F(GAP_RemoteDeviceCacheTest, NewDeviceInitialTechnologyLowEnergy) {
  // LE address types should initialize the device as LE-only.
  auto* le_publ_dev = cache()->NewDevice(kAddrLePublic, true /*connectable*/);
  auto* le_rand_dev = cache()->NewDevice(kAddrLeRandom, true /*connectable*/);
  auto* le_anon_dev = cache()->NewDevice(kAddrLeAnon, false /*connectable*/);
  ASSERT_TRUE(le_publ_dev);
  ASSERT_TRUE(le_rand_dev);
  ASSERT_TRUE(le_anon_dev);
  EXPECT_TRUE(le_publ_dev->le());
  EXPECT_TRUE(le_rand_dev->le());
  EXPECT_TRUE(le_anon_dev->le());
  EXPECT_FALSE(le_publ_dev->bredr());
  EXPECT_FALSE(le_rand_dev->bredr());
  EXPECT_FALSE(le_anon_dev->bredr());
  EXPECT_EQ(TechnologyType::kLowEnergy, le_publ_dev->technology());
  EXPECT_EQ(TechnologyType::kLowEnergy, le_rand_dev->technology());
  EXPECT_EQ(TechnologyType::kLowEnergy, le_anon_dev->technology());
  EXPECT_TRUE(le_publ_dev->identity_known());
  EXPECT_FALSE(le_rand_dev->identity_known());
  EXPECT_FALSE(le_anon_dev->identity_known());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       DisallowNewLowEnergyDeviceIfBrEdrDeviceExists) {
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));

  // Try to add new LE device with a public identity address containing the same
  // value as the existing BR/EDR device's BD_ADDR.
  auto* le_alias_dev = cache()->NewDevice(kAddrLeAlias, true);
  EXPECT_FALSE(le_alias_dev);
}

TEST_F(GAP_RemoteDeviceCacheTest,
       DisallowNewBrEdrDeviceIfLowEnergyDeviceExists) {
  ASSERT_TRUE(NewDevice(kAddrLeAlias, true));

  // Try to add new BR/EDR device with BD_ADDR containing the same value as the
  // existing LE device's public identity address.
  auto* bredr_alias_dev = cache()->NewDevice(kAddrBrEdr, true);
  ASSERT_FALSE(bredr_alias_dev);
}

TEST_F(GAP_RemoteDeviceCacheTest,
       BrEdrDeviceBecomesDualModeWithAdvertisingData) {
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));
  ASSERT_TRUE(device()->bredr());
  ASSERT_FALSE(device()->le());

  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  EXPECT_TRUE(device()->le());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());

  // Searching by LE address should turn up this device, which should retain its
  // original address type.
  auto* const le_device = cache()->FindDeviceByAddress(kAddrLeAlias);
  ASSERT_EQ(device(), le_device);
  EXPECT_EQ(DeviceAddress::Type::kBREDR, device()->address().type());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       BrEdrDeviceBecomesDualModeWhenConnectedOverLowEnergy) {
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));
  ASSERT_TRUE(device()->bredr());
  ASSERT_FALSE(device()->le());

  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(device()->le());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());

  auto* const le_device = cache()->FindDeviceByAddress(kAddrLeAlias);
  ASSERT_EQ(device(), le_device);
  EXPECT_EQ(DeviceAddress::Type::kBREDR, device()->address().type());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       BrEdrDeviceBecomesDualModeWithLowEnergyConnParams) {
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));
  ASSERT_TRUE(device()->bredr());
  ASSERT_FALSE(device()->le());

  device()->MutLe().SetConnectionParameters({});
  EXPECT_TRUE(device()->le());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());

  auto* const le_device = cache()->FindDeviceByAddress(kAddrLeAlias);
  ASSERT_EQ(device(), le_device);
  EXPECT_EQ(DeviceAddress::Type::kBREDR, device()->address().type());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       BrEdrDeviceBecomesDualModeWithLowEnergyPreferredConnParams) {
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));
  ASSERT_TRUE(device()->bredr());
  ASSERT_FALSE(device()->le());

  device()->MutLe().SetPreferredConnectionParameters({});
  EXPECT_TRUE(device()->le());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());

  auto* const le_device = cache()->FindDeviceByAddress(kAddrLeAlias);
  ASSERT_EQ(device(), le_device);
  EXPECT_EQ(DeviceAddress::Type::kBREDR, device()->address().type());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       LowEnergyDeviceBecomesDualModeWithInquiryData) {
  ASSERT_TRUE(NewDevice(kAddrLeAlias, true));
  ASSERT_TRUE(device()->le());
  ASSERT_FALSE(device()->bredr());

  hci::InquiryResult ir;
  ir.bd_addr = kAddrLeAlias.value();
  device()->MutBrEdr().SetInquiryData(ir);
  EXPECT_TRUE(device()->bredr());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());

  // Searching by only BR/EDR technology should turn up this device, which
  // should still retain its original address type.
  auto* const bredr_device = cache()->FindDeviceByAddress(kAddrBrEdr);
  ASSERT_EQ(device(), bredr_device);
  EXPECT_EQ(DeviceAddress::Type::kLEPublic, device()->address().type());
  EXPECT_EQ(kAddrBrEdr, device()->bredr()->address());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       LowEnergyDeviceBecomesDualModeWhenConnectedOverClassic) {
  ASSERT_TRUE(NewDevice(kAddrLeAlias, true));
  ASSERT_TRUE(device()->le());
  ASSERT_FALSE(device()->bredr());

  device()->MutBrEdr().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(device()->bredr());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());

  auto* const bredr_device = cache()->FindDeviceByAddress(kAddrBrEdr);
  ASSERT_EQ(device(), bredr_device);
  EXPECT_EQ(DeviceAddress::Type::kLEPublic, device()->address().type());
  EXPECT_EQ(kAddrBrEdr, device()->bredr()->address());
}

class GAP_RemoteDeviceCacheTest_BondingTest : public GAP_RemoteDeviceCacheTest {
 public:
  void SetUp() {
    was_called_ = false;
    ASSERT_TRUE(NewDevice(kAddrLePublic, true));
    cache()->set_device_bonded_callback(
        [this](const auto&) { was_called_ = true; });
    EXPECT_FALSE(was_called_);
  }

 protected:
  bool bonded_callback_called() const { return was_called_; }

 private:
  bool was_called_;
};

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedDeviceFailsWithExistingId) {
  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_FALSE(cache()->AddBondedDevice(device()->identifier(), kAddrLeRandom,
                                        data, {}));
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedDeviceFailsWithExistingAddress) {
  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_FALSE(cache()->AddBondedDevice("foo", device()->address(), data, {}));
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedLowEnergyDeviceFailsWithExistingBrEdrAliasAddress) {
  EXPECT_TRUE(NewDevice(kAddrBrEdr, true));
  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_FALSE(cache()->AddBondedDevice("foo", kAddrLeAlias, data, {}));
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedBrEdrDeviceFailsWithExistingLowEnergyAliasAddress) {
  EXPECT_TRUE(NewDevice(kAddrLeAlias, true));
  EXPECT_FALSE(cache()->AddBondedDevice("foo", kAddrBrEdr, {}, kBrEdrKey));
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedDeviceFailsWithoutMandatoryKeys) {
  sm::PairingData data;
  EXPECT_FALSE(cache()->AddBondedDevice("foo", kAddrLeAlias, data, kBrEdrKey));
  data.ltk = kLTK;
  EXPECT_FALSE(cache()->AddBondedDevice("foo", kAddrBrEdr, data, {}));
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, AddLowEnergyBondedDeviceSuccess) {
  const std::string kId("test-id");
  sm::PairingData data;
  data.ltk = kLTK;

  EXPECT_TRUE(cache()->AddBondedDevice(kId, kAddrLeRandom, data, {}));
  auto* dev = cache()->FindDeviceById(kId);
  ASSERT_TRUE(dev);
  EXPECT_EQ(dev, cache()->FindDeviceByAddress(kAddrLeRandom));
  EXPECT_EQ(kId, dev->identifier());
  EXPECT_EQ(kAddrLeRandom, dev->address());
  EXPECT_TRUE(dev->identity_known());
  ASSERT_TRUE(dev->le());
  EXPECT_TRUE(dev->le()->bonded());
  ASSERT_TRUE(dev->le()->bond_data());
  EXPECT_EQ(data, *dev->le()->bond_data());
  EXPECT_FALSE(dev->bredr());
  EXPECT_EQ(TechnologyType::kLowEnergy, dev->technology());

  // The "new bond" callback should not be called when restoring a previously
  // bonded device.
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, AddBrEdrBondedDeviceSuccess) {
  const std::string kId("test-id");
  sm::PairingData data;

  EXPECT_TRUE(cache()->AddBondedDevice(kId, kAddrBrEdr, data, kBrEdrKey));
  auto* dev = cache()->FindDeviceById(kId);
  ASSERT_TRUE(dev);
  EXPECT_EQ(dev, cache()->FindDeviceByAddress(kAddrBrEdr));
  EXPECT_EQ(kId, dev->identifier());
  EXPECT_EQ(kAddrBrEdr, dev->address());
  EXPECT_TRUE(dev->identity_known());
  ASSERT_TRUE(dev->bredr());
  EXPECT_TRUE(dev->bredr()->bonded());
  ASSERT_TRUE(dev->bredr()->link_key());
  EXPECT_EQ(kBrEdrKey, *dev->bredr()->link_key());
  EXPECT_FALSE(dev->le());
  EXPECT_EQ(TechnologyType::kClassic, dev->technology());

  // The "new bond" callback should not be called when restoring a previously
  // bonded device.
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedDeviceWithIrkIsAddedToResolvingList) {
  const std::string kId("test-id");
  sm::PairingData data;
  data.ltk = kLTK;
  data.irk = sm::Key(sm::SecurityProperties(), common::RandomUInt128());

  EXPECT_TRUE(cache()->AddBondedDevice(kId, kAddrLeRandom, data, {}));
  auto* dev = cache()->FindDeviceByAddress(kAddrLeRandom);
  ASSERT_TRUE(dev);
  EXPECT_EQ(kAddrLeRandom, dev->address());

  // Looking up the device by RPA generated using the IRK should return the same
  // device.
  DeviceAddress rpa = sm::util::GenerateRpa(data.irk->value());
  EXPECT_EQ(dev, cache()->FindDeviceByAddress(rpa));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondFailsWithNoKeys) {
  sm::PairingData data;
  EXPECT_FALSE(cache()->StoreLowEnergyBond(device()->identifier(), data));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, StoreLowEnergyBondDeviceUnknown) {
  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_FALSE(cache()->StoreLowEnergyBond("foo", data));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, StoreLowEnergyBondWithLtk) {
  ASSERT_TRUE(device()->temporary());
  ASSERT_TRUE(device()->le());
  ASSERT_FALSE(device()->le()->bonded());

  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));

  EXPECT_TRUE(bonded_callback_called());
  EXPECT_FALSE(device()->temporary());
  EXPECT_TRUE(device()->le()->bonded());
  EXPECT_TRUE(device()->le()->bond_data());
  EXPECT_EQ(data, *device()->le()->bond_data());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, StoreLowEnergyBondWithCsrk) {
  ASSERT_TRUE(device()->temporary());
  ASSERT_TRUE(device()->le());
  ASSERT_FALSE(device()->le()->bonded());

  sm::PairingData data;
  data.csrk = kKey;
  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));

  EXPECT_TRUE(bonded_callback_called());
  EXPECT_FALSE(device()->temporary());
  EXPECT_TRUE(device()->le()->bonded());
  EXPECT_TRUE(device()->le()->bond_data());
  EXPECT_EQ(data, *device()->le()->bond_data());
}

// StoreLowEnergyBond fails if it contains the address of a different,
// previously known device.
TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondWithExistingDifferentIdentity) {
  auto* dev = cache()->NewDevice(kAddrLeRandom, true);

  // Assign the other device's address as identity.
  sm::PairingData data;
  data.ltk = kLTK;
  data.identity_address = device()->address();
  EXPECT_FALSE(cache()->StoreLowEnergyBond(dev->identifier(), data));
  EXPECT_FALSE(dev->le()->bonded());
  EXPECT_TRUE(dev->temporary());
}

// StoreLowEnergyBond fails if the new identity is the address of a "different"
// (another device record with a distinct ID) BR/EDR device.
TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondWithNewIdentityMatchingExistingBrEdrDevice) {
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));
  ASSERT_TRUE(NewDevice(kAddrLeRandom, true));
  ASSERT_FALSE(device()->identity_known());

  sm::PairingData data;
  data.ltk = kLTK;
  // new identity address is same as another device's BR/EDR identity
  data.identity_address = kAddrLeAlias;
  const auto old_address = device()->address();
  ASSERT_EQ(device(), cache()->FindDeviceByAddress(old_address));
  ASSERT_NE(device(), cache()->FindDeviceByAddress(*data.identity_address));
  EXPECT_FALSE(cache()->StoreLowEnergyBond(device()->identifier(), data));
  EXPECT_FALSE(device()->identity_known());
}

// StoreLowEnergyBond succeeds if it contains an identity address that already
// matches the target device.
TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondWithExistingMatchingIdentity) {
  sm::PairingData data;
  data.ltk = kLTK;
  data.identity_address = device()->address();
  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));
  EXPECT_TRUE(device()->le()->bonded());
  EXPECT_EQ(device(), cache()->FindDeviceByAddress(*data.identity_address));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondWithNewIdentity) {
  ASSERT_TRUE(NewDevice(kAddrLeRandom, true));
  ASSERT_FALSE(device()->identity_known());

  sm::PairingData data;
  data.ltk = kLTK;
  data.identity_address = kAddrLeRandom2;  // assign a new identity address
  const auto old_address = device()->address();
  ASSERT_EQ(device(), cache()->FindDeviceByAddress(old_address));
  ASSERT_EQ(nullptr, cache()->FindDeviceByAddress(*data.identity_address));

  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));
  EXPECT_TRUE(device()->le()->bonded());

  // Address should have been updated.
  ASSERT_NE(*data.identity_address, old_address);
  EXPECT_EQ(*data.identity_address, device()->address());
  EXPECT_TRUE(device()->identity_known());
  EXPECT_EQ(device(), cache()->FindDeviceByAddress(*data.identity_address));

  // The old address should still map to |dev|.
  ASSERT_EQ(device(), cache()->FindDeviceByAddress(old_address));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondWithIrkIsAddedToResolvingList) {
  ASSERT_TRUE(NewDevice(kAddrLeRandom, true));
  ASSERT_FALSE(device()->identity_known());

  sm::PairingData data;
  data.ltk = kLTK;
  data.identity_address = kAddrLeRandom;
  data.irk = sm::Key(sm::SecurityProperties(), common::RandomUInt128());

  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));
  ASSERT_TRUE(device()->le()->bonded());
  ASSERT_TRUE(device()->identity_known());

  // Looking up the device by RPA generated using the IRK should return the same
  // device.
  DeviceAddress rpa = sm::util::GenerateRpa(data.irk->value());
  EXPECT_EQ(device(), cache()->FindDeviceByAddress(rpa));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreBrEdrBondWithUnknownAddress) {
  ASSERT_EQ(nullptr, cache()->FindDeviceByAddress(kAddrBrEdr));
  EXPECT_FALSE(cache()->StoreBrEdrBond(kAddrBrEdr, kBrEdrKey));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, StoreBrEdrBond) {
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));
  ASSERT_EQ(device(), cache()->FindDeviceByAddress(kAddrBrEdr));
  ASSERT_TRUE(device()->temporary());
  ASSERT_FALSE(device()->bonded());
  ASSERT_TRUE(device()->bredr());
  ASSERT_FALSE(device()->bredr()->bonded());

  EXPECT_TRUE(cache()->StoreBrEdrBond(kAddrBrEdr, kBrEdrKey));

  EXPECT_FALSE(device()->temporary());
  EXPECT_TRUE(device()->bonded());
  EXPECT_TRUE(device()->bredr()->bonded());
  EXPECT_TRUE(device()->bredr()->link_key());
  EXPECT_EQ(kBrEdrKey, *device()->bredr()->link_key());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, StoreBondsForBothTech) {
  ASSERT_TRUE(NewDevice(kAddrBrEdr, true));
  ASSERT_EQ(device(), cache()->FindDeviceByAddress(kAddrBrEdr));
  ASSERT_TRUE(device()->temporary());
  ASSERT_FALSE(device()->bonded());

  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  ASSERT_EQ(TechnologyType::kDualMode, device()->technology());

  // Without Secure Connections cross-transport key generation, bonding on one
  // technology does not bond on the other.
  ASSERT_FALSE(kBrEdrKey.security().secure_connections());
  EXPECT_TRUE(cache()->StoreBrEdrBond(kAddrBrEdr, kBrEdrKey));
  EXPECT_TRUE(device()->bonded());
  EXPECT_FALSE(device()->le()->bonded());

  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));

  EXPECT_FALSE(device()->temporary());
  EXPECT_TRUE(device()->bonded());
  EXPECT_TRUE(device()->bredr()->bonded());
  EXPECT_TRUE(device()->le()->bonded());
}

// Fixture parameterized by device address
class DualModeBondingTest
    : public GAP_RemoteDeviceCacheTest_BondingTest,
      public ::testing::WithParamInterface<DeviceAddress> {};

TEST_P(DualModeBondingTest, AddBondedDeviceSuccess) {
  const std::string kId("test-id");
  sm::PairingData data;
  data.ltk = kLTK;

  const DeviceAddress& address = GetParam();
  EXPECT_TRUE(cache()->AddBondedDevice(kId, address, data, kBrEdrKey));
  auto* dev = cache()->FindDeviceById(kId);
  ASSERT_TRUE(dev);
  EXPECT_EQ(dev, cache()->FindDeviceByAddress(kAddrLeAlias));
  EXPECT_EQ(dev, cache()->FindDeviceByAddress(kAddrBrEdr));
  EXPECT_EQ(kId, dev->identifier());
  EXPECT_EQ(address, dev->address());
  EXPECT_TRUE(dev->identity_known());
  EXPECT_TRUE(dev->bonded());
  ASSERT_TRUE(dev->le());
  EXPECT_TRUE(dev->le()->bonded());
  ASSERT_TRUE(dev->le()->bond_data());
  EXPECT_EQ(data, *dev->le()->bond_data());
  ASSERT_TRUE(dev->bredr());
  EXPECT_TRUE(dev->bredr()->bonded());
  ASSERT_TRUE(dev->bredr()->link_key());
  EXPECT_EQ(kBrEdrKey, *dev->bredr()->link_key());
  EXPECT_EQ(TechnologyType::kDualMode, dev->technology());

  // The "new bond" callback should not be called when restoring a previously
  // bonded device.
  EXPECT_FALSE(bonded_callback_called());
}

// Test dual-mode character of device using the same address of both types.
INSTANTIATE_TEST_SUITE_P(GAP_RemoteDeviceCacheTest, DualModeBondingTest,
                         ::testing::Values(kAddrBrEdr, kAddrLeAlias));

template <const DeviceAddress* DevAddr>
class GAP_RemoteDeviceCacheTest_UpdateCallbackTest
    : public GAP_RemoteDeviceCacheTest {
 public:
  void SetUp() {
    was_called_ = false;
    ASSERT_TRUE(NewDevice(*DevAddr, true));
    cache()->set_device_updated_callback(
        [this](const auto&) { was_called_ = true; });
    ir_.bd_addr = device()->address().value();
    irr_.bd_addr = device()->address().value();
    eirep_.bd_addr = device()->address().value();
    eir_data().SetToZeros();
    EXPECT_FALSE(was_called_);
  }

 protected:
  hci::InquiryResult& ir() { return ir_; }
  hci::InquiryResultRSSI& irr() { return irr_; }
  hci::ExtendedInquiryResultEventParams& eirep() { return eirep_; }

  MutableBufferView eir_data() {
    return MutableBufferView(&eirep_.extended_inquiry_response,
                             sizeof(eirep_.extended_inquiry_response));
  }
  bool was_called() const { return was_called_; }
  void ClearWasCalled() { was_called_ = false; }

 private:
  bool was_called_;
  hci::InquiryResult ir_;
  hci::InquiryResultRSSI irr_;
  hci::ExtendedInquiryResultEventParams eirep_;
};

using GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest =
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest<&kAddrBrEdr>;
using GAP_RemoteDeviceCacheTest_LowEnergyUpdateCallbackTest =
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest<&kAddrLeAlias>;

TEST_F(GAP_RemoteDeviceCacheTest_LowEnergyUpdateCallbackTest,
       ChangingLEConnectionStateTriggersUpdateCallback) {
  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_LowEnergyUpdateCallbackTest,
       SetAdvertisingDataTriggersUpdateCallbackOnNameSet) {
  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  EXPECT_TRUE(was_called());
  ASSERT_TRUE(device()->name());
  EXPECT_EQ("Test", *device()->name());
}

TEST_F(GAP_RemoteDeviceCacheTest_LowEnergyUpdateCallbackTest,
       SetLowEnergyAdvertisingDataUpdateCallbackProvidesUpdatedDevice) {
  ASSERT_NE(device()->rssi(), kTestRSSI);
  cache()->set_device_updated_callback([&](const auto& updated_dev) {
    ASSERT_TRUE(updated_dev.le());
    EXPECT_TRUE(common::ContainersEqual(kAdvData,
                                        updated_dev.le()->advertising_data()));
    EXPECT_EQ(updated_dev.rssi(), kTestRSSI);
  });
  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
}

TEST_F(GAP_RemoteDeviceCacheTest_LowEnergyUpdateCallbackTest,
       SetAdvertisingDataDoesNotTriggerUpdateCallbackOnSameName) {
  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  EXPECT_FALSE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_LowEnergyUpdateCallbackTest,
       SetLowEnergyConnectionParamsDoesNotTriggerUpdateCallback) {
  device()->MutLe().SetConnectionParameters({});
  EXPECT_FALSE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_LowEnergyUpdateCallbackTest,
       SetLowEnergyPreferredConnectionParamsDoesNotTriggerUpdateCallback) {
  device()->MutLe().SetPreferredConnectionParameters({});
  EXPECT_FALSE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_LowEnergyUpdateCallbackTest,
       BecomingDualModeTriggersUpdateCallBack) {
  EXPECT_EQ(TechnologyType::kLowEnergy, device()->technology());

  size_t call_count = 0;
  cache()->set_device_updated_callback([&](const auto&) { ++call_count; });
  device()->MutBrEdr();
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());
  EXPECT_EQ(call_count, 1U);

  // Calling MutBrEdr again on doesn't trigger additional callbacks.
  device()->MutBrEdr();
  EXPECT_EQ(call_count, 1U);
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_EQ(call_count, 2U);
}

TEST_F(GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
       ChangingBrEdrConnectionStateTriggersUpdateCallback) {
  device()->MutBrEdr().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultTriggersUpdateCallbackOnDeviceClassSet) {
  ir().class_of_device = kTestDeviceClass;
  device()->MutBrEdr().SetInquiryData(ir());
  EXPECT_TRUE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultUpdateCallbackProvidesUpdatedDevice) {
  ir().class_of_device = kTestDeviceClass;
  cache()->set_device_updated_callback([](const auto& updated_dev) {
    ASSERT_TRUE(updated_dev.bredr());
    ASSERT_TRUE(updated_dev.bredr()->device_class());
    EXPECT_EQ(common::DeviceClass::MajorClass(0x02),
              updated_dev.bredr()->device_class()->major_class());
  });
  device()->MutBrEdr().SetInquiryData(ir());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultDoesNotTriggerUpdateCallbackOnSameDeviceClass) {
  ir().class_of_device = kTestDeviceClass;
  device()->MutBrEdr().SetInquiryData(ir());
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutBrEdr().SetInquiryData(ir());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultRSSITriggersUpdateCallbackOnDeviceClassSet) {
  irr().class_of_device = kTestDeviceClass;
  device()->MutBrEdr().SetInquiryData(irr());
  EXPECT_TRUE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultRSSIUpdateCallbackProvidesUpdatedDevice) {
  irr().class_of_device = kTestDeviceClass;
  cache()->set_device_updated_callback([](const auto& updated_dev) {
    ASSERT_TRUE(updated_dev.bredr()->device_class());
    EXPECT_EQ(common::DeviceClass::MajorClass(0x02),
              updated_dev.bredr()->device_class()->major_class());
  });
  device()->MutBrEdr().SetInquiryData(irr());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultRSSIDoesNotTriggerUpdateCallbackOnSameDeviceClass) {
  irr().class_of_device = kTestDeviceClass;
  device()->MutBrEdr().SetInquiryData(irr());
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutBrEdr().SetInquiryData(irr());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultRSSIDoesNotTriggerUpdateCallbackOnRSSI) {
  irr().rssi = 1;
  device()->MutBrEdr().SetInquiryData(irr());
  ASSERT_TRUE(was_called());  // Callback due to |class_of_device|.

  ClearWasCalled();
  irr().rssi = 20;
  device()->MutBrEdr().SetInquiryData(irr());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsTriggersUpdateCallbackOnDeviceClassSet) {
  eirep().class_of_device = kTestDeviceClass;
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_TRUE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsTriggersUpdateCallbackOnNameSet) {
  device()->MutBrEdr().SetInquiryData(eirep());
  ASSERT_TRUE(was_called());  // Callback due to |class_of_device|.

  ClearWasCalled();
  eir_data().Write(kEirData);
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_TRUE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsUpdateCallbackProvidesUpdatedDevice) {
  eirep().clock_offset = htole16(1);
  eirep().page_scan_repetition_mode = hci::PageScanRepetitionMode::kR1;
  eirep().rssi = kTestRSSI;
  eirep().class_of_device = kTestDeviceClass;
  eir_data().Write(kEirData);
  ASSERT_FALSE(device()->name().has_value());
  ASSERT_EQ(device()->rssi(), hci::kRSSIInvalid);
  cache()->set_device_updated_callback([](const auto& updated_dev) {
    const auto& data = updated_dev.bredr();
    ASSERT_TRUE(data);
    ASSERT_TRUE(data->clock_offset().has_value());
    ASSERT_TRUE(data->page_scan_repetition_mode().has_value());
    ASSERT_TRUE(data->device_class().has_value());
    ASSERT_TRUE(updated_dev.name().has_value());

    EXPECT_EQ(*data->clock_offset(), 0x8001);
    EXPECT_EQ(*data->page_scan_repetition_mode(),
              hci::PageScanRepetitionMode::kR1);
    EXPECT_EQ(common::DeviceClass::MajorClass(0x02),
              updated_dev.bredr()->device_class()->major_class());
    EXPECT_EQ(updated_dev.rssi(), kTestRSSI);
    EXPECT_EQ(*updated_dev.name(), "Test");
  });
  device()->MutBrEdr().SetInquiryData(eirep());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsGeneratesExactlyOneUpdateCallbackRegardlessOfNumberOfFieldsChanged) {
  eirep().clock_offset = htole16(1);
  eirep().page_scan_repetition_mode = hci::PageScanRepetitionMode::kR1;
  eirep().rssi = kTestRSSI;
  eirep().class_of_device = kTestDeviceClass;
  eir_data().Write(kEirData);

  size_t call_count = 0;
  cache()->set_device_updated_callback([&](const auto&) { ++call_count; });
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_EQ(call_count, 1U);
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsDoesNotTriggerUpdateCallbackOnSameDeviceClass) {
  eirep().class_of_device = kTestDeviceClass;
  device()->MutBrEdr().SetInquiryData(eirep());
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsDoesNotTriggerUpdateCallbackOnSameName) {
  eir_data().Write(kEirData);
  device()->MutBrEdr().SetInquiryData(eirep());
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsDoesNotTriggerUpdateCallbackOnRSSI) {
  eirep().rssi = 1;
  device()->MutBrEdr().SetInquiryData(eirep());
  ASSERT_TRUE(was_called());  // Callback due to |class_of_device|.

  ClearWasCalled();
  eirep().rssi = 20;
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_FALSE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
       SetNameTriggersUpdateCallback) {
  device()->SetName("nombre");
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
       SetNameDoesNotTriggerUpdateCallbackOnSameName) {
  device()->SetName("nombre");
  ASSERT_TRUE(was_called());

  bool was_called_again = false;
  cache()->set_device_updated_callback(
      [&](const auto&) { was_called_again = true; });
  device()->SetName("nombre");
  EXPECT_FALSE(was_called_again);
}

TEST_F(GAP_RemoteDeviceCacheTest_BrEdrUpdateCallbackTest,
       BecomingDualModeTriggersUpdateCallBack) {
  EXPECT_EQ(TechnologyType::kClassic, device()->technology());

  size_t call_count = 0;
  cache()->set_device_updated_callback([&](const auto&) { ++call_count; });
  device()->MutLe();
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());
  EXPECT_EQ(call_count, 1U);

  // Calling MutLe again on doesn't trigger additional callbacks.
  device()->MutLe();
  EXPECT_EQ(call_count, 1U);
  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  EXPECT_EQ(call_count, 2U);
}

class GAP_RemoteDeviceCacheExpirationTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() {
    auto* dev = cache_.NewDevice(kAddrLePublic, true /*connectable*/);
    ASSERT_TRUE(dev);
    ASSERT_TRUE(dev->temporary());
    device_addr_ = dev->address();
    device_id_ = dev->identifier();
  }

  void TearDown() { RunLoopUntilIdle(); }

  RemoteDevice* GetDefaultDevice() { return cache_.FindDeviceById(device_id_); }
  RemoteDevice* GetDeviceById(const std::string& id) {
    return cache_.FindDeviceById(id);
  }
  bool IsDefaultDeviceAddressInCache() {
    return cache_.FindDeviceByAddress(device_addr_);
  }
  bool IsDefaultDevicePresent() { return GetDefaultDevice(); }
  RemoteDevice* NewDevice(const common::DeviceAddress& address,
                          bool connectable) {
    return cache_.NewDevice(address, connectable);
  }

 private:
  RemoteDeviceCache cache_;
  common::DeviceAddress device_addr_;
  std::string device_id_;
};

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       TemporaryDiesSixtySecondsAfterBirth) {
  RunLoopFor(kCacheTimeout);
  EXPECT_FALSE(IsDefaultDevicePresent());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       TemporaryLivesForSixtySecondsAfterBirth) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  EXPECT_TRUE(IsDefaultDevicePresent());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       TemporaryLivesForSixtySecondsSinceLastSeen) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_TRUE(IsDefaultDevicePresent());

  // Tickle device, and verify it sticks around for another cache timeout.
  GetDefaultDevice()->SetName("nombre");
  RunLoopFor(kCacheTimeout - zx::msec(1));
  EXPECT_TRUE(IsDefaultDevicePresent());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       TemporaryDiesSixtySecondsAfterLastSeen) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_TRUE(IsDefaultDevicePresent());

  // Tickle device, and verify it expires after cache timeout.
  GetDefaultDevice()->SetName("nombre");
  RunLoopFor(kCacheTimeout);
  EXPECT_FALSE(IsDefaultDevicePresent());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       CanMakeNonTemporaryJustBeforeSixtySeconds) {
  // At last possible moment, make device non-temporary,
  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_TRUE(IsDefaultDevicePresent());
  GetDefaultDevice()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  ASSERT_FALSE(GetDefaultDevice()->temporary());

  // Verify that the device survives.
  RunLoopFor(kCacheTimeout * 10);
  EXPECT_TRUE(IsDefaultDevicePresent());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       LEConnectedDeviceLivesMuchMoreThanSixtySeconds) {
  ASSERT_TRUE(IsDefaultDevicePresent());
  GetDefaultDevice()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  RunLoopFor(kCacheTimeout * 10);
  ASSERT_TRUE(IsDefaultDevicePresent());
  EXPECT_FALSE(GetDefaultDevice()->temporary());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       BREDRConnectedDeviceLivesMuchMoreThanSixtySeconds) {
  ASSERT_TRUE(IsDefaultDevicePresent());
  GetDefaultDevice()->MutBrEdr().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  RunLoopFor(kCacheTimeout * 10);
  ASSERT_TRUE(IsDefaultDevicePresent());
  EXPECT_FALSE(GetDefaultDevice()->temporary());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       LEPublicDeviceRemainsNonTemporaryOnDisconnect) {
  ASSERT_TRUE(IsDefaultDevicePresent());
  ASSERT_EQ(kAddrLePublic, GetDefaultDevice()->address());
  GetDefaultDevice()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  ASSERT_FALSE(GetDefaultDevice()->temporary());

  RunLoopFor(zx::sec(61));
  ASSERT_TRUE(IsDefaultDevicePresent());
  ASSERT_TRUE(GetDefaultDevice()->identity_known());

  GetDefaultDevice()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kNotConnected);
  EXPECT_FALSE(GetDefaultDevice()->temporary());

  RunLoopFor(kCacheTimeout);
  EXPECT_TRUE(IsDefaultDevicePresent());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       LERandomDeviceBecomesTemporaryOnDisconnect) {
  // Create our RemoteDevice, and get it into the kConnected state.
  std::string custom_device_id;
  {
    auto* custom_device = NewDevice(kAddrLeRandom, true);
    ASSERT_TRUE(custom_device);
    ASSERT_TRUE(custom_device->temporary());
    ASSERT_FALSE(custom_device->identity_known());
    custom_device_id = custom_device->identifier();

    custom_device->MutLe().SetConnectionState(
        RemoteDevice::ConnectionState::kConnected);
    ASSERT_FALSE(custom_device->temporary());
    ASSERT_FALSE(custom_device->identity_known());
  }

  // Verify that the connected device does not expire out of the cache.
  // Then disconnect the device, in preparation for the next stage of our test.
  {
    RunLoopFor(zx::sec(61));
    auto* custom_device = GetDeviceById(custom_device_id);
    ASSERT_TRUE(custom_device);
    ASSERT_FALSE(custom_device->identity_known());

    custom_device->MutLe().SetConnectionState(
        RemoteDevice::ConnectionState::kNotConnected);
    EXPECT_TRUE(custom_device->temporary());
    EXPECT_FALSE(custom_device->identity_known());
  }

  // Verify that the disconnected device expires out of the cache.
  RunLoopFor(zx::sec(61));
  EXPECT_FALSE(GetDeviceById(custom_device_id));
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       BREDRDeviceRemainsNonTemporaryOnDisconnect) {
  // Create our RemoteDevice, and get it into the kConnected state.
  std::string custom_device_id;
  {
    auto* custom_device = NewDevice(kAddrBrEdr, true);
    ASSERT_TRUE(custom_device);
    custom_device->MutBrEdr().SetConnectionState(
        RemoteDevice::ConnectionState::kConnected);
    custom_device_id = custom_device->identifier();
  }

  // Verify that the connected device does not expire out of the cache.
  // Then disconnect the device, in preparation for the next stage of our test.
  {
    RunLoopFor(kCacheTimeout * 10);
    auto* custom_device = GetDeviceById(custom_device_id);
    ASSERT_TRUE(custom_device);
    ASSERT_TRUE(custom_device->identity_known());
    EXPECT_FALSE(custom_device->temporary());

    custom_device->MutBrEdr().SetConnectionState(
        RemoteDevice::ConnectionState::kNotConnected);
    ASSERT_TRUE(GetDeviceById(custom_device_id));
    EXPECT_FALSE(custom_device->temporary());
  }

  // Verify that the disconnected device does _not_ expire out of the cache.
  // We expect the device to remain, because BrEdr devices are non-temporary
  // even when disconnected.
  RunLoopFor(kCacheTimeout);
  EXPECT_TRUE(GetDeviceById(custom_device_id));
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest, ExpirationUpdatesAddressMap) {
  ASSERT_TRUE(IsDefaultDeviceAddressInCache());
  RunLoopFor(kCacheTimeout);
  EXPECT_FALSE(IsDefaultDeviceAddressInCache());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       SetAdvertisingDataUpdatesExpiration) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_TRUE(IsDefaultDevicePresent());
  GetDefaultDevice()->MutLe().SetAdvertisingData(kTestRSSI,
                                                 StaticByteBuffer<1>{});
  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(IsDefaultDevicePresent());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       SetBrEdrInquiryDataFromInquiryResultUpdatesExpiration) {
  hci::InquiryResult ir;
  ASSERT_TRUE(IsDefaultDevicePresent());
  ir.bd_addr = GetDefaultDevice()->address().value();

  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_TRUE(IsDefaultDevicePresent());
  GetDefaultDevice()->MutBrEdr().SetInquiryData(ir);

  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(IsDefaultDevicePresent());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest,
       SetBrEdrInquiryDataFromInquiryResultRSSIUpdatesExpiration) {
  hci::InquiryResultRSSI irr;
  ASSERT_TRUE(IsDefaultDevicePresent());
  irr.bd_addr = GetDefaultDevice()->address().value();

  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_TRUE(IsDefaultDevicePresent());
  GetDefaultDevice()->MutBrEdr().SetInquiryData(irr);

  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(IsDefaultDevicePresent());
}

TEST_F(
    GAP_RemoteDeviceCacheExpirationTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsUpdatesExpiration) {
  hci::ExtendedInquiryResultEventParams eirep;
  ASSERT_TRUE(IsDefaultDevicePresent());
  eirep.bd_addr = GetDefaultDevice()->address().value();

  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_TRUE(IsDefaultDevicePresent());
  GetDefaultDevice()->MutBrEdr().SetInquiryData(eirep);

  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(IsDefaultDevicePresent());
}

TEST_F(GAP_RemoteDeviceCacheExpirationTest, SetNameUpdatesExpiration) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_TRUE(IsDefaultDevicePresent());
  GetDefaultDevice()->SetName({});
  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(IsDefaultDevicePresent());
}

}  // namespace
}  // namespace gap
}  // namespace btlib
