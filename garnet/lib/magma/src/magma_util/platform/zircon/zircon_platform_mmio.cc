// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_mmio.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace magma {

static_assert(ZX_CACHE_POLICY_CACHED == static_cast<int>(PlatformMmio::CACHE_POLICY_CACHED),
              "enum mismatch");
static_assert(ZX_CACHE_POLICY_UNCACHED == static_cast<int>(PlatformMmio::CACHE_POLICY_UNCACHED),
              "enum mismatch");
static_assert(ZX_CACHE_POLICY_UNCACHED_DEVICE ==
                  static_cast<int>(PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE),
              "enum mismatch");
static_assert(ZX_CACHE_POLICY_WRITE_COMBINING ==
                  static_cast<int>(PlatformMmio::CACHE_POLICY_WRITE_COMBINING),
              "enum mismatch");

ZirconPlatformMmio::ZirconPlatformMmio(mmio_buffer_t mmio)
    : PlatformMmio(mmio.vaddr, mmio.size), mmio_(mmio)
{
}

ZirconPlatformMmio::~ZirconPlatformMmio()
{
    DLOG("ZirconPlatformMmio dtor");
    mmio_buffer_release(&mmio_);
}

} // namespace magma
