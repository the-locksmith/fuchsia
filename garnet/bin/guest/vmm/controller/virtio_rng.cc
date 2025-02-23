// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/controller/virtio_rng.h"

#include <lib/svc/cpp/services.h>

static constexpr char kVirtioRngUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_rng#meta/virtio_rng.cmx";

VirtioRng::VirtioRng(const PhysMem& phys_mem)
    : VirtioComponentDevice(phys_mem, 0 /* device_features */,
                            fit::bind_member(this, &VirtioRng::ConfigureQueue),
                            fit::bind_member(this, &VirtioRng::Ready)) {}

zx_status_t VirtioRng::Start(const zx::guest& guest,
                             fuchsia::sys::Launcher* launcher,
                             async_dispatcher_t* dispatcher) {
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info{
      .url = kVirtioRngUrl,
      .directory_request = services.NewRequest(),
  };
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  services.ConnectToService(rng_.NewRequest());

  fuchsia::guest::device::StartInfo start_info;
  zx_status_t status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  return rng_->Start(std::move(start_info));
}

zx_status_t VirtioRng::ConfigureQueue(uint16_t queue, uint16_t size,
                                      zx_gpaddr_t desc, zx_gpaddr_t avail,
                                      zx_gpaddr_t used) {
  return rng_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioRng::Ready(uint32_t negotiated_features) {
  return rng_->Ready(negotiated_features);
}
