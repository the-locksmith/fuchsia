// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bindings for the Zircon fdio library

#![deny(warnings)]

#[allow(bad_style)]
pub mod fdio_sys;
pub use self::fdio_sys::fdio_ioctl as ioctl_raw;

use {
    ::bitflags::bitflags,
    fidl_fuchsia_device::ControllerSynchronousProxy,
    fuchsia_zircon::{
        self as zx,
        prelude::*,
        sys::{self, zx_handle_t, zx_status_t},
    },
    std::{
        ffi::{self, CString, CStr},
        fs::File,
        marker::PhantomData,
        os::{
            raw,
            unix::{
                ffi::OsStrExt,
                io::{
                    AsRawFd,
                    IntoRawFd,
                }
            },
        },
        path::Path,
    },
};

pub unsafe fn ioctl(dev: &File, op: raw::c_int, in_buf: *const raw::c_void, in_len: usize,
         out_buf: *mut raw::c_void, out_len: usize) -> Result<i32, zx::Status> {
   match ioctl_raw(dev.as_raw_fd(), op, in_buf, in_len, out_buf, out_len) as i32 {
     e if e < 0 => Err(zx::Status::from_raw(e)),
     e => Ok(e),
   }
}

/// Connects a channel to a named service.
pub fn service_connect(service_path: &str, channel: zx::Channel) -> Result<(), zx::Status> {
    let c_service_path = CString::new(service_path).map_err(|_| zx::Status::INVALID_ARGS)?;

    // TODO(raggi): this should be convered to an asynchronous FDIO
    // client protocol as soon as that is available (post fidl2) as this
    // call can block indefinitely.
    //
    // fdio_service connect takes a *const c_char service path and a channel.
    // On success, the channel is connected, and on failure, it is closed.
    // In either case, we do not need to clean up the channel so we use
    // `into_raw` so that Rust forgets about it.
    zx::ok(unsafe {
        fdio_sys::fdio_service_connect(
            c_service_path.as_ptr(),
            channel.into_raw())
    })
}

/// Connects a channel to a named service relative to a directory `dir`.
/// `dir` must be a directory protocol channel.
pub fn service_connect_at(dir: &zx::Channel, service_path: &str, channel: zx::Channel)
    -> Result<(), zx::Status>
{
    let c_service_path = CString::new(service_path).map_err(|_| zx::Status::INVALID_ARGS)?;

    // TODO(raggi): this should be convered to an asynchronous FDIO
    // client protocol as soon as that is available (post fidl2) as this
    // call can block indefinitely.
    //
    // fdio_service_connect_at takes a directory handle,
    // a *const c_char service path, and a channel to connect.
    // The directory handle is never consumed, so we borrow the raw handle.
    // On success, the channel is connected, and on failure, it is closed.
    // In either case, we do not need to clean up the channel so we use
    // `into_raw` so that Rust forgets about it.
    zx::ok(unsafe {
        fdio_sys::fdio_service_connect_at(
            dir.raw_handle(),
            c_service_path.as_ptr(),
            channel.into_raw())
    })
}

pub fn transfer_fd(file: std::fs::File) -> Result<zx::Handle, zx::Status> {
    unsafe {
        let mut fd_handle = zx::sys::ZX_HANDLE_INVALID;
        let status = fdio_sys::fdio_fd_transfer(file.into_raw_fd(), &mut fd_handle as *mut zx::sys::zx_handle_t);
        if status != zx::sys::ZX_OK {
            return Err(zx::Status::from_raw(status));
        }
        Ok(zx::Handle::from_raw(fd_handle))
    }
}

/// Open a new connection to `file` by sending a request to open
/// a new connection to the sever.
pub fn clone_channel(file: &std::fs::File) -> Result<zx::Channel, zx::Status> {
    unsafe {
        // First, we must open a new connection to the handle, since
        // we must return a newly owned copy.
        let fdio = fdio_sys::fdio_unsafe_fd_to_io(file.as_raw_fd());
        let unowned_handle = fdio_sys::fdio_unsafe_borrow_channel(fdio);
        let handle = fdio_sys::fdio_service_clone(unowned_handle);
        fdio_sys::fdio_unsafe_release(fdio);

        match handle {
            zx::sys::ZX_HANDLE_INVALID => Err(zx::Status::NOT_SUPPORTED),
            _ => Ok(zx::Channel::from(zx::Handle::from_raw(handle))),
        }
    }
}

/// Retrieves the topological path for a device node.
pub fn device_get_topo_path(dev: &File) -> Result<String, zx::Status> {
    let channel = clone_channel(dev)?;
    let mut interface = ControllerSynchronousProxy::new(channel);
    let (status, topo) = interface.get_topological_path(fuchsia_zircon::Time::INFINITE)
        .map_err(|_| zx::Status::IO)?;
    fuchsia_zircon::Status::ok(status)?;

    match topo {
        Some(topo) => Ok(topo),
        None => Err(zx::Status::BAD_STATE),
    }
}

bitflags! {
    /// Options to allow some or all of the environment of the running process
    /// to be shared with the process being spawned.
    pub struct SpawnOptions: u32 {
        /// Provide the spawned process with the job in which the process was created.
        ///
        /// The job will be available to the new process as the PA_JOB_DEFAULT argument
        /// (exposed in Rust as `fuchsia_runtim::job_default()`).
        const CLONE_JOB = fdio_sys::FDIO_SPAWN_CLONE_JOB;

        /// Provide the spawned process with the shared library loader via the
        /// PA_LDSVC_LOADER argument.
        const DEFAULT_LOADER = fdio_sys::FDIO_SPAWN_DEFAULT_LDSVC;

        /// Clones the filesystem namespace into the spawned process.
        const CLONE_NAMESPACE = fdio_sys::FDIO_SPAWN_CLONE_NAMESPACE;

        /// Clones file descriptors 0, 1, and 2 into the spawned process.
        ///
        /// Skips any of these file descriptors that are closed without
        /// generating an error.
        const CLONE_STDIO = fdio_sys::FDIO_SPAWN_CLONE_STDIO;

        /// Clones the environment into the spawned process.
        const CLONE_ENVIRONMENT = fdio_sys::FDIO_SPAWN_CLONE_ENVIRON;

        /// Clones the namespace, stdio, and environment into the spawned process.
        const CLONE_ALL = fdio_sys::FDIO_SPAWN_CLONE_ALL;
    }
}

// TODO: someday we'll have custom DSTs which will make this unnecessary.
fn nul_term_from_slice(argv: &[&CStr]) -> Vec<*const raw::c_char> {
    argv.iter().map(|cstr| cstr.as_ptr())
        .chain(std::iter::once(0 as *const raw::c_char))
        .collect()
}

/// Spawn a process in the given `job`.
pub fn spawn(
    job: &zx::Job,
    options: SpawnOptions,
    path: &CStr,
    argv: &[&CStr],
) -> Result<zx::Process, zx::Status> {
    let job = job.raw_handle();
    let flags = options.bits();
    let path = path.as_ptr();
    let argv = nul_term_from_slice(argv);
    let mut process_out = 0;

    // Safety: spawn consumes no handles and frees no pointers, and only
    // produces a valid process upon success.
    let status = unsafe {
        fdio_sys::fdio_spawn(job, flags, path, argv.as_ptr(), &mut process_out)
    };
    zx::ok(status)?;
    Ok(zx::Process::from(unsafe { zx::Handle::from_raw(process_out) }))
}

/// An action to take in `spawn_etc`.
#[repr(transparent)]
pub struct SpawnAction<'a>(fdio_sys::fdio_spawn_action_t, PhantomData<&'a ()>);

impl<'a> SpawnAction<'a> {
    /// Clone a file descriptor into the new process.
    ///
    /// `local_fd`: File descriptor within the current process.
    /// `target_fd`: File descriptor within the new process that will receive the clone.
    pub fn clone_fd(local_fd: &'a File, target_fd: i32) -> Self {
        // Safety: `local_fd` is a valid file descriptor so long as we're inside the
        // 'a lifetime.
        Self(fdio_sys::fdio_spawn_action_t {
            action_tag: fdio_sys::FDIO_SPAWN_ACTION_CLONE_FD,
            action_value: fdio_sys::fdio_spawn_action_union_t {
                fd: fdio_sys::fdio_spawn_action_fd_t {
                    local_fd: local_fd.as_raw_fd(),
                    target_fd,
                },
            },
        }, PhantomData)
    }

    /// Transfer a file descriptor into the new process.
    ///
    /// `local_fd`: File descriptor within the current process.
    /// `target_fd`: File descriptor within the new process that will receive the transfer.
    pub fn transfer_fd(local_fd: File, target_fd: i32) -> Self {
        // Safety: ownership of `local_fd` is consumed, so `Self` can live arbitrarily long.
        // When the action is executed, the fd will be transferred.
        Self(fdio_sys::fdio_spawn_action_t {
            action_tag: fdio_sys::FDIO_SPAWN_ACTION_TRANSFER_FD,
            action_value: fdio_sys::fdio_spawn_action_union_t {
                fd: fdio_sys::fdio_spawn_action_fd_t {
                    local_fd: local_fd.into_raw_fd(),
                    target_fd,
                },
            }
        }, PhantomData)
    }

    /// Add the given entry to the namespace of the spawned process.
    ///
    /// If `SpawnOptions::CLONE_NAMESPACE` is set, the namespace entry is added
    /// to the cloned namespace from the calling process.
    pub fn add_namespace_entry(prefix: &'a CStr, handle: zx::Handle) -> Self {
        // Safety: ownership of the `handle` is consumed.
        // The prefix string must stay valid through the 'a lifetime.
        Self(fdio_sys::fdio_spawn_action_t {
            action_tag: fdio_sys::FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
            action_value: fdio_sys::fdio_spawn_action_union_t {
                ns: fdio_sys::fdio_spawn_action_ns_t {
                    prefix: prefix.as_ptr(),
                    handle: handle.into_raw(),
                },
            },
        }, PhantomData)
    }

    /// Add the given handle to the process arguments of the spawned process.
    pub fn add_handle(
        kind: fuchsia_runtime::HandleType,
        handle: zx::Handle,
    ) -> Self {
        // Safety: ownership of the `handle` is consumed.
        // The prefix string must stay valid through the 'a lifetime.
        Self(fdio_sys::fdio_spawn_action_t {
            action_tag: fdio_sys::FDIO_SPAWN_ACTION_ADD_HANDLE,
            action_value: fdio_sys::fdio_spawn_action_union_t {
                h: fdio_sys::fdio_spawn_action_h_t {
                    id: kind as u32,
                    handle: handle.into_raw(),
                },
            },
        }, PhantomData)
    }

    /// Sets the name of the spawned process to the given name.
    pub fn set_name(name: &'a CStr) -> Self {
        // Safety: the `name` pointer must be valid at least as long as `Self`.
        Self(fdio_sys::fdio_spawn_action_t {
            action_tag: fdio_sys::FDIO_SPAWN_ACTION_SET_NAME,
            action_value: fdio_sys::fdio_spawn_action_union_t {
                name: fdio_sys::fdio_spawn_action_name_t {
                    data: name.as_ptr(),
                },
            },
        }, PhantomData)
    }

    fn is_null(&self) -> bool {
        self.0.action_tag == 0
    }

    /// Nullifies the action to prevent the inner contents from being dropped.
    fn nullify(&mut self) {
        // Assert that our null value doesn't conflict with any "real" actions.
        debug_assert!(
            (fdio_sys::FDIO_SPAWN_ACTION_CLONE_FD != 0)
            && (fdio_sys::FDIO_SPAWN_ACTION_TRANSFER_FD != 0)
            && (fdio_sys::FDIO_SPAWN_ACTION_ADD_NS_ENTRY != 0)
            && (fdio_sys::FDIO_SPAWN_ACTION_ADD_HANDLE != 0)
            && (fdio_sys::FDIO_SPAWN_ACTION_SET_NAME != 0)
        );
        self.0.action_tag = 0;
    }
}

fn spawn_with_actions(
    job: &zx::Job,
    options: SpawnOptions,
    argv: &[&CStr],
    environ: &[&CStr],
    actions: &mut [SpawnAction],
    spawn_fn: impl FnOnce(
        zx_handle_t, // job
        u32, // flags
        *const *const raw::c_char, // argv
        *const *const raw::c_char, // environ
        usize, // action_count
        *const fdio_sys::fdio_spawn_action_t, // actions
        *mut zx_handle_t, // process_out,
        *mut [raw::c_char; fdio_sys::FDIO_SPAWN_ERR_MSG_MAX_LENGTH as usize], // err_msg_out
    ) -> zx_status_t,
) -> Result<zx::Process, (zx::Status, String)> {
    let job = job.raw_handle();
    let flags = options.bits();
    let argv = nul_term_from_slice(argv);
    let environ = nul_term_from_slice(environ);

    if actions.iter().any(|a| a.is_null()) {
        return Err((zx::Status::INVALID_ARGS, "null SpawnAction".to_string()));
    }
    // Safety: actions are repr(transparent) wrappers around fdio_spawn_action_t
    let action_count = actions.len();
    let actions_ptr: *const SpawnAction = actions.as_ptr();
    let actions_ptr = actions_ptr as *const fdio_sys::fdio_spawn_action_t;

    let mut process_out = 0;
    let mut err_msg_out = [0 as raw::c_char; fdio_sys::FDIO_SPAWN_ERR_MSG_MAX_LENGTH as usize];

    let status = spawn_fn(
        job,
        flags,
        argv.as_ptr(),
        environ.as_ptr(),
        action_count,
        actions_ptr,
        &mut process_out,
        &mut err_msg_out,
    );

    zx::ok(status)
        .map_err(|status| {
            let err_msg = unsafe { CStr::from_ptr(err_msg_out.as_ptr()) }
                .to_string_lossy()
                .into_owned();
            (status, err_msg)
        })?;

    // Clear out the actions so we can't unsafely re-use them in a future call.
    actions.iter_mut().for_each(|a| a.nullify());
    Ok(zx::Process::from(unsafe { zx::Handle::from_raw(process_out) }))
}

/// Spawn a process in the given `job` using a series of `SpawnAction`s.
/// All `SpawnAction`s are nullified after their use in this function.
pub fn spawn_etc(
    job: &zx::Job,
    options: SpawnOptions,
    path: &CStr,
    argv: &[&CStr],
    environ: &[&CStr],
    actions: &mut [SpawnAction],
) -> Result<zx::Process, (zx::Status, String)> {
    let path = path.as_ptr();
    spawn_with_actions(
        job,
        options,
        argv,
        environ,
        actions,
        |job, flags, argv, environ, action_count, actions_ptr, process_out, err_msg_out| {
            unsafe { fdio_sys::fdio_spawn_etc(
                job,
                flags,
                path,
                argv,
                environ,
                action_count,
                actions_ptr,
                process_out,
                err_msg_out,
            ) }
        }
    )
}

/// Spawn a process in the given job using an executable VMO.
pub fn spawn_vmo(
    job: &zx::Job,
    options: SpawnOptions,
    executable_vmo: zx::Vmo,
    argv: &[&CStr],
    environ: &[&CStr],
    actions: &mut [SpawnAction],
) -> Result<zx::Process, (zx::Status, String)> {
    let executable_vmo = executable_vmo.into_raw();
    spawn_with_actions(
        job,
        options,
        argv,
        environ,
        actions,
        |job, flags, argv, environ, action_count, actions_ptr, process_out, err_msg_out| {
            unsafe { fdio_sys::fdio_spawn_vmo(
                job,
                flags,
                executable_vmo,
                argv,
                environ,
                action_count,
                actions_ptr,
                process_out,
                err_msg_out,
            ) }
        }
    )
}

/// Events that can occur while watching a directory, including files that already exist prior to
/// running a Watcher.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum WatchEvent {
    /// A file was added.
    AddFile,

    /// A file was removed.
    RemoveFile,

    /// The Watcher has enumerated all the existing files and has started to wait for new files to
    /// be added.
    Idle,

    Unknown(i32),

    #[doc(hidden)]
    #[allow(non_camel_case_types)]
    // Try to prevent exhaustive matching since this enum may grow if fdio's events expand.
    __do_not_match,
}

impl From<raw::c_int> for WatchEvent {
    fn from(i: raw::c_int) -> WatchEvent {
        match i {
            fdio_sys::WATCH_EVENT_ADD_FILE => WatchEvent::AddFile,
            fdio_sys::WATCH_EVENT_REMOVE_FILE => WatchEvent::RemoveFile,
            fdio_sys::WATCH_EVENT_IDLE => WatchEvent::Idle,
            _ => WatchEvent::Unknown(i),
        }
    }
}

impl From<WatchEvent> for raw::c_int {
    fn from(i: WatchEvent) -> raw::c_int {
        match i {
            WatchEvent::AddFile => fdio_sys::WATCH_EVENT_ADD_FILE,
            WatchEvent::RemoveFile => fdio_sys::WATCH_EVENT_REMOVE_FILE,
            WatchEvent::Idle => fdio_sys::WATCH_EVENT_IDLE,
            WatchEvent::Unknown(i) => i as raw::c_int,
            _ => -1 as raw::c_int,
        }
    }
}

unsafe extern "C" fn watcher_cb<F>(
    _dirfd: raw::c_int,
    event: raw::c_int,
    fn_: *const raw::c_char,
    watcher: *mut raw::c_void,
) -> sys::zx_status_t
where
    F: Sized + FnMut(WatchEvent, &Path) -> Result<(), zx::Status>,
{
    let cb: &mut F = &mut *(watcher as *mut F);
    let filename = ffi::OsStr::from_bytes(CStr::from_ptr(fn_).to_bytes());
    match cb(WatchEvent::from(event), Path::new(filename)) {
        Ok(()) => sys::ZX_OK,
        Err(e) => e.into_raw(),
    }
}

/// Runs the given callback for each file in the directory and each time a new file is
/// added to the directory.
///
/// If the callback returns an error, the watching stops, and the zx::Status is returned.
///
/// This function blocks for the duration of the watch operation. The deadline parameter will stop
/// the watch at the given (absolute) time and return zx::Status::ErrTimedOut. A deadline of
/// zx::ZX_TIME_INFINITE will never expire.
///
/// The callback may use zx::ErrStop as a way to signal to the caller that it wants to
/// stop because it found what it was looking for. Since this error code is not returned by
/// syscalls or public APIs, the callback does not need to worry about it turning up normally.
pub fn watch_directory<F>(dir: &File, deadline: sys::zx_time_t, mut f: F) -> zx::Status
where
    F: Sized + FnMut(WatchEvent, &Path) -> Result<(), zx::Status>,
{
    let cb_ptr: *mut raw::c_void = &mut f as *mut _ as *mut raw::c_void;
    unsafe {
        zx::Status::from_raw(fdio_sys::fdio_watch_directory(
            dir.as_raw_fd(),
            Some(watcher_cb::<F>),
            deadline,
            cb_ptr,
        ))
    }
}

/// Calculates an IOCTL value from kind, family and number.
pub const fn make_ioctl(kind: raw::c_int, family: raw::c_int, number: raw::c_int) -> raw::c_int {
    ((kind & 0xF) << 20) | ((family & 0xFF) << 8) | (number & 0xFF)
}

pub fn get_vmo_copy_from_file(file: &File) -> Result<zx::Vmo, zx::Status> {
    unsafe {
        let mut vmo_handle: zx::sys::zx_handle_t = zx::sys::ZX_HANDLE_INVALID;
        match fdio_sys::fdio_get_vmo_copy(file.as_raw_fd(), &mut vmo_handle) {
            0 => Ok(zx::Vmo::from(zx::Handle::from_raw(vmo_handle))),
            error_code => Err(zx::Status::from_raw(error_code))
        }
    }
}
