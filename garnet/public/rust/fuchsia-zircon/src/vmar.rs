// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon vmar objects.

use bitflags::bitflags;
use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Status, Vmo};
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon
/// [virtual memory address region](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/objects/vm_address_region.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq)]
#[repr(transparent)]
pub struct Vmar(Handle);
impl_handle_based!(Vmar);

impl Vmar {
    pub fn allocate(
        &self, offset: usize, size: usize, flags: VmarFlags,
    ) -> Result<(Vmar, usize), Status> {
        let mut mapped = 0;
        let mut handle = 0;
        let status = unsafe {
            sys::zx_vmar_allocate(
                self.raw_handle(),
                flags.bits(),
                offset,
                size,
                &mut handle,
                &mut mapped,
            )
        };
        ok(status)?;
        unsafe { Ok((Vmar::from(Handle::from_raw(handle)), mapped)) }
    }

    pub fn map(
        &self, vmar_offset: usize, vmo: &Vmo, vmo_offset: u64, len: usize, flags: VmarFlags,
    ) -> Result<usize, Status> {
        let flags = VmarFlagsExtended::from_bits_truncate(flags.bits());
        unsafe {
            self.map_unsafe(vmar_offset, vmo, vmo_offset, len, flags)
        }
    }

    /// Directly call zx_vmar_map.
    ///
    /// # Safety
    ///
    /// This function is unsafe because certain flags to `zx_vmar_map` may
    /// replace an existing mapping which is referenced elsewhere.
    pub unsafe fn map_unsafe(
        &self, vmar_offset: usize, vmo: &Vmo, vmo_offset: u64, len: usize,
        flags: VmarFlagsExtended,
    ) -> Result<usize, Status> {
        let mut mapped = 0;
        let status = sys::zx_vmar_map(
            self.0.raw_handle(),
            flags.bits(),
            vmar_offset,
            vmo.raw_handle(),
            vmo_offset,
            len,
            &mut mapped,
        );
        ok(status).map(|_| mapped)
    }

    pub unsafe fn unmap(&self, addr: usize, len: usize) -> Result<(), Status> {
        ok(sys::zx_vmar_unmap(self.0.raw_handle(), addr, len))
    }

    pub unsafe fn protect(&self, addr: usize, len: usize, flags: VmarFlags) -> Result<(), Status> {
        ok(sys::zx_vmar_protect(self.raw_handle(), flags.bits(), addr, len))
    }

    pub unsafe fn destroy(&self) -> Result<(), Status> {
        ok(sys::zx_vmar_destroy(self.raw_handle()))
    }
}

// TODO(smklein): Ideally we would have two separate sets of bitflags,
// and a union of both of them.
bitflags! {
    /// Flags to VMAR routines which are considered safe.
    #[repr(transparent)]
    pub struct VmarFlags: sys::zx_vm_option_t {
        const PERM_READ             = sys::ZX_VM_PERM_READ;
        const PERM_WRITE            = sys::ZX_VM_PERM_WRITE;
        const PERM_EXECUTE          = sys::ZX_VM_PERM_EXECUTE;
        const COMPACT               = sys::ZX_VM_COMPACT;
        const SPECIFIC              = sys::ZX_VM_SPECIFIC;
        const CAN_MAP_SPECIFIC      = sys::ZX_VM_CAN_MAP_SPECIFIC;
        const CAN_MAP_READ          = sys::ZX_VM_CAN_MAP_READ;
        const CAN_MAP_WRITE         = sys::ZX_VM_CAN_MAP_WRITE;
        const CAN_MAP_EXECUTE       = sys::ZX_VM_CAN_MAP_EXECUTE;
        const MAP_RANGE             = sys::ZX_VM_MAP_RANGE;
        const REQUIRE_NON_RESIZABLE = sys::ZX_VM_REQUIRE_NON_RESIZABLE;
    }
}

bitflags! {
    /// Flags to all VMAR routines.
    #[repr(transparent)]
    pub struct VmarFlagsExtended: sys::zx_vm_option_t {
        const PERM_READ          = sys::ZX_VM_PERM_READ;
        const PERM_WRITE         = sys::ZX_VM_PERM_WRITE;
        const PERM_EXECUTE       = sys::ZX_VM_PERM_EXECUTE;
        const COMPACT            = sys::ZX_VM_COMPACT;
        const SPECIFIC           = sys::ZX_VM_SPECIFIC;
        const SPECIFIC_OVERWRITE = sys::ZX_VM_SPECIFIC_OVERWRITE;
        const CAN_MAP_SPECIFIC   = sys::ZX_VM_CAN_MAP_SPECIFIC;
        const CAN_MAP_READ       = sys::ZX_VM_CAN_MAP_READ;
        const CAN_MAP_WRITE      = sys::ZX_VM_CAN_MAP_WRITE;
        const CAN_MAP_EXECUTE    = sys::ZX_VM_CAN_MAP_EXECUTE;
    }
}
