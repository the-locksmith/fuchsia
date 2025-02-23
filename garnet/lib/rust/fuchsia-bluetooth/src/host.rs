// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fdio::{fdio_sys, ioctl, make_ioctl},
    fuchsia_zircon::{self as zircon, Handle},
    std::{
        fs::{File, read_dir},
        mem,
        os::raw,
        path::PathBuf,
    },
};

/// Returns the filesystem paths to the all bt-host devices.
pub fn list_host_devices() -> Vec<PathBuf> {
    let paths = read_dir("/dev/class/bt-host/").unwrap();
    paths
        .filter_map(|entry| entry.ok().and_then(|e| Some(e.path())))
        .collect::<Vec<PathBuf>>()
}

/// Opens a Host Fidl interface on a bt-host device using an Ioctl
pub fn open_host_channel(device: &File) -> Result<zircon::Handle, Error> {
    let mut handle = zircon::sys::ZX_HANDLE_INVALID;
    unsafe {
        ioctl(
            device,
            IOCTL_BT_HOST_OPEN_CHANNEL,
            ::std::ptr::null_mut() as *mut raw::c_void,
            0,
            &mut handle as *mut _ as *mut raw::c_void,
            mem::size_of::<zircon::sys::zx_handle_t>(),
        ).map(|_| Handle::from_raw(handle))
        .map_err(|e| e.into())
    }
}

const IOCTL_BT_HOST_OPEN_CHANNEL: raw::c_int = make_ioctl(
    fdio_sys::IOCTL_KIND_GET_HANDLE,
    fdio_sys::IOCTL_FAMILY_BT_HOST,
    0
);
