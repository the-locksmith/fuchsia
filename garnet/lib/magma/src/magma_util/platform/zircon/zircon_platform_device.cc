// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <zircon/process.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"
#include "zircon_platform_device.h"
#include "zircon_platform_handle.h"
#include "zircon_platform_interrupt.h"
#include "zircon_platform_mmio.h"

namespace magma {

std::unique_ptr<PlatformMmio>
ZirconPlatformDevice::CpuMapMmio(unsigned int index, PlatformMmio::CachePolicy cache_policy)
{
    DLOG("CpuMapMmio index %d", index);

    zx_status_t status;
    mmio_buffer_t mmio_buffer;

    status = pdev_map_mmio_buffer(&pdev_, index, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio_buffer);
    if (status != ZX_OK) {
        DRETP(nullptr, "mapping resource failed");
    }

    std::unique_ptr<ZirconPlatformMmio> mmio(new ZirconPlatformMmio(mmio_buffer));

    DLOG("map_mmio index %d cache_policy %d returned: 0x%x", index, static_cast<int>(cache_policy),
         mmio_buffer.vmo);

    return mmio;
}

std::unique_ptr<PlatformInterrupt> ZirconPlatformDevice::RegisterInterrupt(unsigned int index)
{
    zx_handle_t interrupt_handle;
    zx_status_t status = pdev_get_interrupt(&pdev_, index, 0, &interrupt_handle);
    if (status != ZX_OK)
        return DRETP(nullptr, "register interrupt failed");

    return std::make_unique<ZirconPlatformInterrupt>(zx::handle(interrupt_handle));
}

std::unique_ptr<PlatformHandle> ZirconPlatformDevice::GetBusTransactionInitiator()
{
    zx_handle_t bti_handle;
    zx_status_t status = pdev_get_bti(&pdev_, 0, &bti_handle);
    if (status != ZX_OK)
        return DRETP(nullptr, "failed to get bus transaction initiator");

    return std::make_unique<ZirconPlatformHandle>(zx::handle(bti_handle));
}

std::unique_ptr<PlatformDevice> PlatformDevice::Create(void* device_handle)
{
    if (!device_handle)
        return DRETP(nullptr, "device_handle is null, cannot create PlatformDevice");
    zx_device_t* zx_device = static_cast<zx_device_t*>(device_handle);
    pdev_protocol_t pdev;
    if (device_get_protocol(zx_device, ZX_PROTOCOL_PDEV, &pdev) != ZX_OK)
        return DRETP(nullptr, "Failed to get protocol\n");

    return std::unique_ptr<PlatformDevice>(new ZirconPlatformDevice(zx_device, pdev));
}

} // namespace magma
