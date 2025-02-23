// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_CONTROLLER_VIRTIO_WL_H_
#define GARNET_BIN_GUEST_VMM_CONTROLLER_VIRTIO_WL_H_

#include <lib/zx/vmar.h>
#include <virtio/virtio_ids.h>
#include <virtio/wl.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/bin/guest/vmm/virtio_device.h"

#define VIRTWL_QUEUE_COUNT 4

// Virtio wayland device.
class VirtioWl : public VirtioComponentDevice<VIRTIO_ID_WL, VIRTWL_QUEUE_COUNT,
                                              virtio_wl_config_t> {
 public:
  explicit VirtioWl(const PhysMem& phys_mem);

  zx_status_t Start(
      const zx::guest& guest, zx::vmar vmar,
      fidl::InterfaceHandle<fuchsia::guest::WaylandDispatcher> dispatch_handle,
      fuchsia::sys::Launcher* launcher, async_dispatcher_t* dispatcher);

 private:
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::guest::device::VirtioWaylandSyncPtr wayland_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                             zx_gpaddr_t avail, zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);
};

#endif  // GARNET_BIN_GUEST_VMM_CONTROLLER_VIRTIO_WL_H_
