// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <ramdevice-client/ramdisk.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/ramdisk/c/fidl.h>
#include <fvm/format.h>
#include <lib/zircon-internal/debug.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>
#include <zircon/assert.h>
#include <zircon/types.h>
#include <zxcrypt/volume.h>

#include "test-device.h"

#define ZXDEBUG 0

namespace zxcrypt {
namespace testing {
namespace {

// No test step should take longer than this
const zx::duration kTimeout = zx::sec(3);

// FVM driver library
const char* kFvmDriver = "/boot/driver/fvm.so";

// Takes a given |result|, e.g. from an ioctl, and translates into a zx_status_t.
zx_status_t ToStatus(ssize_t result) {
    return result < 0 ? static_cast<zx_status_t>(result) : ZX_OK;
}

// Helper function to build error messages
char* Error(const char* fmt, ...) {
    static char err[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, sizeof(err), fmt, ap);
    va_end(ap);
    return err;
}

// Waits for the given |path| to be opened, opens it, and returns the file descriptor via |out|.
bool WaitAndOpen(char* path, fbl::unique_fd* out) {
    BEGIN_HELPER;

    ASSERT_EQ(wait_for_device(path, ZX_SEC(3)), ZX_OK,
              Error("failed while waiting to bind %s", path));
    fbl::unique_fd fd(open(path, O_RDWR));
    ASSERT_TRUE(fd, Error("failed to open %s", path));
    if (out) {
        out->swap(fd);
    }

    END_HELPER;
}

} // namespace

TestDevice::TestDevice()
    : ramdisk_(nullptr), block_count_(0), block_size_(0), client_(nullptr), tid_(0),
      need_join_(false), wake_after_(0), wake_deadline_(0) {
    memset(fvm_part_path_, 0, sizeof(fvm_part_path_));
    memset(&req_, 0, sizeof(req_));
}

TestDevice::~TestDevice() {
    Disconnect();
    DestroyRamdisk();
    if (need_join_) {
        int res;
        thrd_join(tid_, &res);
    }
}

bool TestDevice::Create(size_t device_size, size_t block_size, bool fvm) {
    BEGIN_HELPER;

    ASSERT_LT(device_size, SSIZE_MAX);
    if (fvm) {
        ASSERT_TRUE(CreateFvmPart(device_size, block_size));
    } else {
        ASSERT_TRUE(CreateRamdisk(device_size, block_size));
    }

// TODO(aarongreen): See ZX-1130. The code below should be enabled when that bug is fixed.
#if 0
    crypto::digest::Algorithm digest;
    switch (version) {
    case Volume::kAES256_XTS_SHA256:
        digest = crypto::digest::kSHA256;
        break;
    default:
        digest = crypto::digest::kUninitialized;
        break;
    }

    size_t digest_len;
    key_.Reset();
    if ((rc = crypto::digest::GetDigestLen(digest, &digest_len)) != ZX_OK ||
        (rc = key_.Randomize(digest_len)) != ZX_OK) {
        return rc;
    }
#else
    uint8_t* buf;
    ASSERT_OK(key_.Allocate(kZx1130KeyLen, &buf));
    memset(buf, 0, key_.len());
#endif

    END_HELPER;
}

bool TestDevice::Bind(Volume::Version version, bool fvm) {
    BEGIN_HELPER;
    ASSERT_TRUE(Create(kDeviceSize, kBlockSize, fvm));
    ASSERT_OK(Volume::Create(parent(), key_));
    ASSERT_TRUE(Connect());
    END_HELPER;
}

bool TestDevice::Rebind() {
    BEGIN_HELPER;

    const char* sep = strrchr(ramdisk_get_path(ramdisk_), '/');
    ASSERT_NONNULL(sep);
    fbl::String path(ramdisk_get_path(ramdisk_), sep - ramdisk_get_path(ramdisk_));

    Disconnect();
    zxcrypt_.reset();
    fvm_part_.reset();

    ASSERT_EQ(ramdisk_rebind(ramdisk_), ZX_OK);
    if (strlen(fvm_part_path_) != 0) {
        ASSERT_TRUE(WaitAndOpen(fvm_part_path_, &fvm_part_));
    }
    ASSERT_TRUE(Connect());

    END_HELPER;
}

bool TestDevice::SleepUntil(uint64_t num, bool deferred) {
    BEGIN_HELPER;
    fbl::AutoLock lock(&lock_);
    ASSERT_EQ(wake_after_, 0);
    ASSERT_NE(num, 0);
    wake_after_ = num;
    wake_deadline_ = zx::deadline_after(kTimeout);
    ASSERT_EQ(thrd_create(&tid_, TestDevice::WakeThread, this), thrd_success);
    need_join_ = true;
    if (deferred) {
        uint32_t flags = fuchsia_hardware_ramdisk_RAMDISK_FLAG_RESUME_ON_WAKE;
        ASSERT_OK(ramdisk_set_flags(ramdisk_, flags));
    }
    uint64_t sleep_after = 0;
    ASSERT_OK(ramdisk_sleep_after(ramdisk_, sleep_after));
    END_HELPER;
}

bool TestDevice::WakeUp() {
    BEGIN_HELPER;
    if (need_join_) {
        fbl::AutoLock lock(&lock_);
        ASSERT_NE(wake_after_, 0);
        int res;
        ASSERT_EQ(thrd_join(tid_, &res), thrd_success);
        need_join_ = false;
        wake_after_ = 0;
        EXPECT_EQ(res, 0);
    }
    END_HELPER;
}

int TestDevice::WakeThread(void* arg) {
    TestDevice* device = static_cast<TestDevice*>(arg);
    fbl::AutoLock lock(&device->lock_);

    // Always send a wake-up call; even if we failed to go to sleep.
    auto cleanup = fbl::MakeAutoCall([&] {
        ramdisk_wake(device->ramdisk_);
    });

    // Loop until timeout, |wake_after_| txns received, or error getting counts
    ramdisk_block_write_counts_t counts;
    do {
        zx::nanosleep(zx::deadline_after(zx::msec(100)));
        if (device->wake_deadline_ < zx::clock::get_monotonic()) {
            printf("Received %lu of %lu transactions before timing out.\n", counts.received,
                   device->wake_after_);
            return ZX_ERR_TIMED_OUT;
        }
        zx_status_t status = ramdisk_get_block_counts(device->ramdisk_, &counts);
        if (status != ZX_OK) {
            return status;
        }
    } while (counts.received < device->wake_after_);
    return ZX_OK;
}

bool TestDevice::ReadFd(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    ASSERT_OK(ToStatus(lseek(off)));
    ASSERT_OK(ToStatus(read(off, len)));
    ASSERT_EQ(memcmp(as_read_.get() + off, to_write_.get() + off, len), 0);
    END_HELPER;
}

bool TestDevice::WriteFd(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    ASSERT_OK(ToStatus(lseek(off)));
    ASSERT_OK(ToStatus(write(off, len)));
    END_HELPER;
}

bool TestDevice::ReadVmo(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    ASSERT_OK(block_fifo_txn(BLOCKIO_READ, off, len));
    off *= block_size_;
    len *= block_size_;
    ASSERT_OK(vmo_read(off, len));
    ASSERT_EQ(memcmp(as_read_.get() + off, to_write_.get() + off, len), 0);
    END_HELPER;
}

bool TestDevice::WriteVmo(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    ASSERT_OK(vmo_write(off * block_size_, len * block_size_));
    ASSERT_OK(block_fifo_txn(BLOCKIO_WRITE, off, len));
    END_HELPER;
}

bool TestDevice::Corrupt(uint64_t blkno, key_slot_t slot) {
    BEGIN_HELPER;
    uint8_t block[block_size_];

    fbl::unique_fd fd = parent();
    ASSERT_OK(ToStatus(::lseek(fd.get(), blkno * block_size_, SEEK_SET)));
    ASSERT_OK(ToStatus(::read(fd.get(), block, block_size_)));

    fbl::unique_ptr<Volume> volume;
    ASSERT_OK(Volume::Unlock(parent(), key_, 0, &volume));

    zx_off_t off;
    ASSERT_OK(volume->GetSlotOffset(slot, &off));
    int flip = 1U << (rand() % 8);
    block[off] ^= static_cast<uint8_t>(flip);

    ASSERT_OK(ToStatus(::lseek(fd.get(), blkno * block_size_, SEEK_SET)));
    ASSERT_OK(ToStatus(::write(fd.get(), block, block_size_)));
    END_HELPER;
}

// Private methods

bool TestDevice::CreateRamdisk(size_t device_size, size_t block_size) {
    BEGIN_HELPER;

    fbl::AllocChecker ac;
    size_t count = fbl::round_up(device_size, block_size) / block_size;
    to_write_.reset(new (&ac) uint8_t[device_size]);
    ASSERT_TRUE(ac.check());
    for (size_t i = 0; i < device_size; ++i) {
        to_write_[i] = static_cast<uint8_t>(rand());
    }

    as_read_.reset(new (&ac) uint8_t[device_size]);
    ASSERT_TRUE(ac.check());
    memset(as_read_.get(), 0, block_size);

    ASSERT_EQ(ramdisk_create(block_size, count, &ramdisk_), ZX_OK);

    block_size_ = block_size;
    block_count_ = count;

    END_HELPER;
}

void TestDevice::DestroyRamdisk() {
    if (ramdisk_ != nullptr) {
        ramdisk_destroy(ramdisk_);
        ramdisk_ = nullptr;
    }
}

// Creates a ramdisk, formats it, and binds to it.
bool TestDevice::CreateFvmPart(size_t device_size, size_t block_size) {
    BEGIN_HELPER;

    // Calculate total size of data + metadata.
    device_size = fbl::round_up(device_size, FVM_BLOCK_SIZE);
    size_t old_meta = fvm::MetadataSize(device_size, FVM_BLOCK_SIZE);
    size_t new_meta = fvm::MetadataSize(old_meta + device_size, FVM_BLOCK_SIZE);
    while (old_meta != new_meta) {
        old_meta = new_meta;
        new_meta = fvm::MetadataSize(old_meta + device_size, FVM_BLOCK_SIZE);
    }
    ASSERT_TRUE(CreateRamdisk(device_size + (new_meta * 2), block_size));

    // Format the ramdisk as FVM and bind to it
    ASSERT_OK(fvm_init(ramdisk_get_block_fd(ramdisk_), FVM_BLOCK_SIZE));

    fdio_t* io = fdio_unsafe_fd_to_io(ramdisk_get_block_fd(ramdisk_));
    ASSERT_NONNULL(io);
    zx_status_t call_status;
    zx_status_t status = fuchsia_device_ControllerBind(fdio_unsafe_borrow_channel(io),
                                                       kFvmDriver, strlen(kFvmDriver),
                                                       &call_status);
    fdio_unsafe_release(io);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(call_status, ZX_OK);

    char path[PATH_MAX];
    fbl::unique_fd fvm_fd;
    snprintf(path, sizeof(path), "%s/fvm", ramdisk_get_path(ramdisk_));
    ASSERT_TRUE(WaitAndOpen(path, &fvm_fd));

    // Allocate a FVM partition with the last slice unallocated.
    alloc_req_t req;
    memset(&req, 0, sizeof(alloc_req_t));
    req.slice_count = (kDeviceSize / FVM_BLOCK_SIZE) - 1;
    memcpy(req.type, zxcrypt_magic, sizeof(zxcrypt_magic));
    for (uint8_t i = 0; i < GUID_LEN; ++i) {
        req.guid[i] = i;
    }
    snprintf(req.name, NAME_LEN, "data");
    fvm_part_.reset(fvm_allocate_partition(fvm_fd.get(), &req));
    ASSERT_TRUE(fvm_part_);

    // Save the topological path for rebinding
    io = fdio_unsafe_fd_to_io(fvm_part_.get());
    ASSERT_NONNULL(io);
    size_t out_len;
    status = fuchsia_device_ControllerGetTopologicalPath(fdio_unsafe_borrow_channel(io),
                                                         &call_status, fvm_part_path_,
                                                         sizeof(fvm_part_path_) - 1, &out_len);
    fdio_unsafe_release(io);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(call_status, ZX_OK);
    fvm_part_path_[out_len] = 0;

    END_HELPER;
}

bool TestDevice::Connect() {
    BEGIN_HELPER;
    ZX_DEBUG_ASSERT(!zxcrypt_);

    ASSERT_OK(Volume::Unlock(parent(), key_, 0, &volume_));
    ASSERT_OK(volume_->Open(kTimeout, &zxcrypt_));

    block_info_t blk;
    ASSERT_OK(ToStatus(ioctl_block_get_info(zxcrypt_.get(), &blk)));
    block_size_ = blk.block_size;
    block_count_ = blk.block_count;

    zx_handle_t fifo;
    ASSERT_OK(ToStatus(ioctl_block_get_fifos(zxcrypt_.get(), &fifo)));
    req_.group = 0;
    ASSERT_OK(block_fifo_create_client(fifo, &client_));

    // Create the vmo and get a transferable handle to give to the block server
    ASSERT_OK(zx::vmo::create(size(), 0, &vmo_));
    zx_handle_t xfer;
    ASSERT_OK(zx_handle_duplicate(vmo_.get(), ZX_RIGHT_SAME_RIGHTS, &xfer));
    ASSERT_OK(ToStatus(ioctl_block_attach_vmo(zxcrypt_.get(), &xfer, &req_.vmoid)));

    END_HELPER;
}

void TestDevice::Disconnect() {
    if (client_) {
        memset(&req_, 0, sizeof(req_));
        block_fifo_release_client(client_);
        client_ = nullptr;
    }
    zxcrypt_.reset();
    volume_.reset();
    block_size_ = 0;
    block_count_ = 0;
    vmo_.reset();
}

} // namespace testing
} // namespace zxcrypt
