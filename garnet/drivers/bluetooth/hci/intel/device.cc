// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fbl/string_printf.h>
#include <lib/zx/vmo.h>
#include <zircon/device/bt-hci.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "firmware_loader.h"
#include "logging.h"

namespace btintel {

Device::Device(zx_device_t* device, bt_hci_protocol_t* hci)
    : DeviceType(device), hci_(hci), firmware_loaded_(false) {}

zx_status_t Device::Bind() {
  return DdkAdd("bt_hci_intel", DEVICE_ADD_INVISIBLE);
}

zx_status_t Device::LoadFirmware(bool secure) {
  tracef("LoadFirmware(secure: %s)\n", (secure ? "yes" : "no"));

  // TODO(armansito): Track metrics for initialization failures.

  zx_status_t status;
  zx::channel cmd_channel, acl_channel;

  // Get the channels
  status = BtHciOpenCommandChannel(&cmd_channel);
  if (status != ZX_OK) {
    return Remove(status, "failed to open command channel");
  }

  status = BtHciOpenAclDataChannel(&acl_channel);
  if (status != ZX_OK) {
    return Remove(status, "failed to open ACL channel");
  }

  if (secure) {
    status = LoadSecureFirmware(&cmd_channel, &acl_channel);
  } else {
    status = LoadLegacyFirmware(&cmd_channel, &acl_channel);
  }

  if (status != ZX_OK) {
    return Remove(status, "failed to initialize controller");
  }

  firmware_loaded_ = true;
  DdkMakeVisible();
  return ZX_OK;
}

zx_status_t Device::Remove(zx_status_t status, const char* note) {
  errorf("%s: %s", note, zx_status_get_string(status));
  DdkRemove();
  return status;
}

zx_handle_t Device::MapFirmware(const char* name, uintptr_t* fw_addr,
                                size_t* fw_size) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  size_t size;
  zx_status_t status = load_firmware(zxdev(), name, &vmo, &size);
  if (status != ZX_OK) {
    errorf("failed to load firmware '%s': %s\n", name,
           zx_status_get_string(status));
    return ZX_HANDLE_INVALID;
  }
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size,
                       fw_addr);
  if (status != ZX_OK) {
    errorf("firmware map failed: %s\n", zx_status_get_string(status));
    return ZX_HANDLE_INVALID;
  }
  *fw_size = size;
  return vmo;
}

void Device::DdkUnbind() {
  tracef("unbind\n");
  device_remove(zxdev());
}

void Device::DdkRelease() { delete this; }

zx_status_t Device::DdkGetProtocol(uint32_t proto_id, void* out_proto) {
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  bt_hci_protocol_t* hci_proto = static_cast<bt_hci_protocol_t*>(out_proto);

  // Forward the underlying bt-transport ops.
  hci_.GetProto(hci_proto);

  return ZX_OK;
}

zx_status_t Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* actual) {
  ZX_DEBUG_ASSERT(firmware_loaded_);
  if (out_len < sizeof(zx_handle_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  zx_handle_t* reply = static_cast<zx_handle_t*>(out_buf);
  zx::channel out_channel;

  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  if (op == IOCTL_BT_HCI_GET_COMMAND_CHANNEL) {
    status = BtHciOpenCommandChannel(&out_channel);
  } else if (op == IOCTL_BT_HCI_GET_ACL_DATA_CHANNEL) {
    status = BtHciOpenAclDataChannel(&out_channel);
  } else if (op == IOCTL_BT_HCI_GET_SNOOP_CHANNEL) {
    status = BtHciOpenSnoopChannel(&out_channel);
  }

  if (status != ZX_OK) {
    return status;
  }

  *reply = out_channel.release();
  *actual = sizeof(*reply);
  return ZX_OK;
}

zx_status_t Device::LoadSecureFirmware(zx::channel* cmd, zx::channel* acl) {
  ZX_DEBUG_ASSERT(cmd);
  ZX_DEBUG_ASSERT(acl);

  VendorHci hci(cmd);

  // Bring the controller to a well-defined default state.
  // Send an initial reset. If the controller sends a "command not supported"
  // event on newer models, this likely means that the controller is in
  // bootloader mode and we can ignore the error.
  auto hci_status = hci.SendHciReset();
  if (hci_status == btlib::hci::StatusCode::kUnknownCommand) {
    tracef("Ignoring \"Unknown Command\" error while in bootloader mode\n");
  } else if (hci_status != btlib::hci::StatusCode::kSuccess) {
    errorf("HCI_Reset failed (status: 0x%02x)", hci_status);
    return ZX_ERR_BAD_STATE;
  }

  // Newer Intel controllers that use the "secure send" method can send HCI
  // events over the bulk endpoint. Enable this before sending the initial
  // ReadVersion command.
  hci.enable_events_on_bulk(acl);

  ReadVersionReturnParams version = hci.SendReadVersion();
  if (version.status != btlib::hci::StatusCode::kSuccess) {
    errorf("failed to obtain version information\n");
    return ZX_ERR_BAD_STATE;
  }

  // If we're already in firmware mode, we're done.
  if (version.fw_variant == kFirmwareFirmwareVariant) {
    infof("firmware already loaded\n");
    return ZX_OK;
  }

  // If we reached here then the controller must be in bootloader mode.
  if (version.fw_variant != kBootloaderFirmwareVariant) {
    errorf("unsupported firmware variant (0x%x)\n", version.fw_variant);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ReadBootParamsReturnParams boot_params = hci.SendReadBootParams();
  if (boot_params.status != btlib::hci::StatusCode::kSuccess) {
    errorf("failed to read boot parameters\n");
    return ZX_ERR_BAD_STATE;
  }

  // Map the firmware file into memory.
  auto fw_filename = fbl::StringPrintf("ibt-%d-%d.sfi", version.hw_variant,
                                       boot_params.dev_revid);
  zx::vmo fw_vmo;
  uintptr_t fw_addr;
  size_t fw_size;
  fw_vmo.reset(MapFirmware(fw_filename.c_str(), &fw_addr, &fw_size));
  if (!fw_vmo) {
    errorf("failed to map firmware\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  FirmwareLoader loader(cmd, acl);
  auto status = loader.LoadSfi(reinterpret_cast<void*>(fw_addr), fw_size);
  zx_vmar_unmap(zx_vmar_root_self(), fw_addr, fw_size);
  if (status == FirmwareLoader::LoadStatus::kError) {
    errorf("failed to load SFI firmware\n");
    return ZX_ERR_BAD_STATE;
  }

  // Switch the controller into firmware mode.
  hci.SendVendorReset();

  infof("firmware loaded using %s\n", fw_filename.c_str());
  return ZX_OK;
}

zx_status_t Device::LoadLegacyFirmware(zx::channel* cmd, zx::channel* acl) {
  ZX_DEBUG_ASSERT(cmd);
  ZX_DEBUG_ASSERT(acl);

  VendorHci hci(cmd);

  // Bring the controller to a well-defined default state.
  auto hci_status = hci.SendHciReset();
  if (hci_status != btlib::hci::StatusCode::kSuccess) {
    errorf("HCI_Reset failed (status: 0x%02x)", hci_status);
    return ZX_ERR_BAD_STATE;
  }

  ReadVersionReturnParams version = hci.SendReadVersion();
  if (version.status != btlib::hci::StatusCode::kSuccess) {
    errorf("failed to obtain version information\n");
    return ZX_ERR_BAD_STATE;
  }

  if (version.fw_patch_num > 0) {
    infof("controller already patched\n");
    return ZX_OK;
  }

  auto fw_filename = fbl::StringPrintf(
      "ibt-hw-%x.%x.%x-fw-%x.%x.%x.%x.%x.bseq", version.hw_platform,
      version.hw_variant, version.hw_revision, version.fw_variant,
      version.fw_revision, version.fw_build_num, version.fw_build_week,
      version.fw_build_year);

  zx::vmo fw_vmo;
  uintptr_t fw_addr;
  size_t fw_size;
  fw_vmo.reset(MapFirmware(fw_filename.c_str(), &fw_addr, &fw_size));

  // Try the fallback patch file on initial failure.
  if (!fw_vmo) {
    // Try the fallback patch file
    fw_filename = fbl::StringPrintf("ibt-hw-%x.%x.bseq", version.hw_platform,
                                    version.hw_variant);
    fw_vmo.reset(MapFirmware(fw_filename.c_str(), &fw_addr, &fw_size));
  }

  // Abort if the fallback file failed to load too.
  if (!fw_vmo) {
    errorf("failed to map firmware\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  FirmwareLoader loader(cmd, acl);
  hci.EnterManufacturerMode();
  auto result = loader.LoadBseq(reinterpret_cast<void*>(fw_addr), fw_size);
  hci.ExitManufacturerMode(result == FirmwareLoader::LoadStatus::kPatched
                               ? MfgDisableMode::kPatchesEnabled
                               : MfgDisableMode::kNoPatches);
  zx_vmar_unmap(zx_vmar_root_self(), fw_addr, fw_size);

  if (result == FirmwareLoader::LoadStatus::kError) {
    errorf("failed to patch controller\n");
    return ZX_ERR_BAD_STATE;
  }

  infof("controller patched using %s\n", fw_filename.c_str());
  return ZX_OK;
}

zx_status_t Device::BtHciOpenCommandChannel(zx::channel* out_channel) {
  return hci_.OpenCommandChannel(out_channel);
}

zx_status_t Device::BtHciOpenAclDataChannel(zx::channel* out_channel) {
  return hci_.OpenAclDataChannel(out_channel);
}

zx_status_t Device::BtHciOpenSnoopChannel(zx::channel* out_channel) {
  return hci_.OpenSnoopChannel(out_channel);
}

}  // namespace btintel
