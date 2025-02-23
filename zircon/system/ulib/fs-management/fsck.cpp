// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_ptr.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/vfs.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace {

zx_status_t FsckNativeFs(const char* device_path, const fsck_options_t* options,
                         LaunchCallback cb, const char* cmd_path) {
    zx_handle_t hnd = ZX_HANDLE_INVALID;
    uint32_t id = PA_HND(PA_FD, FS_FD_BLOCKDEVICE);
    int device_fd;
    if ((device_fd = open(device_path, O_RDWR)) < 0) {
        fprintf(stderr, "Failed to open device\n");
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = fdio_fd_transfer(device_fd, &hnd);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to access device handle\n");
        return status;
    }

    fbl::unique_ptr<const char*[]> argv(new const char*[3 + NUM_FSCK_OPTIONS]);
    int argc = 0;
    argv[argc++] = cmd_path;
    if (options->verbose) {
        argv[argc++] = "-v";
    }
    // TODO(smklein): Add support for modify, force flags. Without them,
    // we have "always_modify=true" and "force=true" effectively on by default.
    argv[argc++] = "fsck";
    argv[argc] = nullptr;
    status = static_cast<zx_status_t>(cb(argc, argv.get(), &hnd, &id, 1));
    return status;
}

zx_status_t FsckFat(const char* device_path, const fsck_options_t* options,
                    LaunchCallback cb) {
    fbl::unique_ptr<const char*[]> argv(new const char*[3 + NUM_FSCK_OPTIONS]);
    int argc = 0;
    argv[argc++] = "/boot/bin/fsck-msdosfs";
    if (options->never_modify) {
        argv[argc++] = "-n";
    } else if (options->always_modify) {
        argv[argc++] = "-y";
    }
    if (options->force) {
        argv[argc++] = "-f";
    }
    argv[argc++] = device_path;
    argv[argc] = nullptr;
    zx_status_t status = static_cast<zx_status_t>(cb(argc, argv.get(), NULL, NULL, 0));
    return status;
}

}  // namespace

zx_status_t fsck(const char* device_path, disk_format_t df,
                 const fsck_options_t* options, LaunchCallback cb) {
    switch (df) {
    case DISK_FORMAT_MINFS:
        return FsckNativeFs(device_path, options, cb, "/boot/bin/minfs");
    case DISK_FORMAT_FAT:
        return FsckFat(device_path, options, cb);
    case DISK_FORMAT_BLOBFS:
        return FsckNativeFs(device_path, options, cb, "/boot/bin/blobfs");
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}
