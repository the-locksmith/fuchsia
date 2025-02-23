// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_
#define GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_

#include <string>
#include <unordered_map>
#include <vector>

#include <fuchsia/guest/cpp/fidl.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

struct BlockSpec {
  std::string path;
  fuchsia::guest::BlockFormat format = fuchsia::guest::BlockFormat::RAW;
  fuchsia::guest::BlockMode mode = fuchsia::guest::BlockMode::READ_WRITE;
};

enum class MemoryPolicy {
  // Map a VMO as cached memory into the guest physical address space.
  GUEST_CACHED = 0,
  // Map a VMO with 1:1 correspondence with host memory as cached memory into
  // the guest physical address space.
  HOST_CACHED = 1,
  // Map a VMO with 1:1 correspondence with host memory as device memory into
  // the guest physical address space.
  HOST_DEVICE = 2,
};

struct MemorySpec {
  zx_gpaddr_t base;
  size_t size;
  MemoryPolicy policy;
};

struct InterruptSpec {
  uint32_t vector;
  uint32_t options;
};

enum class Kernel {
  ZIRCON,
  LINUX,
};

class GuestConfig {
 public:
  Kernel kernel() const { return kernel_; }
  const std::string& kernel_path() const { return kernel_path_; }
  const std::string& ramdisk_path() const { return ramdisk_path_; }
  const std::string& dtb_overlay_path() const { return dtb_overlay_path_; }
  const std::string& cmdline() const { return cmdline_; }
  const std::vector<BlockSpec>& block_devices() const { return block_devices_; }
  const std::vector<MemorySpec>& memory() const { return memory_; }
  const std::vector<InterruptSpec>& interrupts() const { return interrupts_; }
  uint8_t cpus() const { return cpus_; }
  bool legacy_net() const { return legacy_net_; }
  bool virtio_balloon() const { return virtio_balloon_; }
  bool virtio_console() const { return virtio_console_; }
  bool virtio_gpu() const { return virtio_gpu_; }
  bool virtio_net() const { return virtio_net_; }
  bool virtio_rng() const { return virtio_rng_; }
  bool virtio_vsock() const { return virtio_vsock_; }

 private:
  friend class GuestConfigParser;
  Kernel kernel_ = Kernel::ZIRCON;
  std::string kernel_path_;
  std::string ramdisk_path_;
  std::string dtb_overlay_path_;
  std::string cmdline_;
  std::vector<BlockSpec> block_devices_;
  std::vector<MemorySpec> memory_;
  std::vector<InterruptSpec> interrupts_;
  uint8_t cpus_ = zx_system_get_num_cpus();
  bool legacy_net_ = true;
  bool virtio_balloon_ = true;
  bool virtio_console_ = true;
  bool virtio_gpu_ = true;
  bool virtio_net_ = true;
  bool virtio_rng_ = true;
  bool virtio_vsock_ = true;
};

class GuestConfigParser {
 public:
  using OptionHandler = std::function<zx_status_t(const std::string& name,
                                                  const std::string& value)>;
  GuestConfigParser(GuestConfig* config);

  zx_status_t ParseArgcArgv(int argc, char** argv);
  zx_status_t ParseConfig(const std::string& data);
  void SetDefaults();

 private:
  GuestConfig* cfg_;
  std::unordered_map<std::string, OptionHandler> opts_;
};

#endif  // GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_
