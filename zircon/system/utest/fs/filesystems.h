// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fs-management/mount.h>
#include <ramdevice-client/ramdisk.h>
#include <unittest/unittest.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>

__BEGIN_CDECLS;

typedef struct fs_info {
    const char* name;
    bool (*should_test)(void);
    int (*mkfs)(const char* disk_path);
    int (*mount)(const char* disk_path, const char* mount_path);
    int (*unmount)(const char* mount_path);
    int (*fsck)(const char* mount_path);
    bool can_be_mounted;
    bool can_mount_sub_filesystems;
    bool supports_hardlinks;
    bool supports_watchers;
    bool supports_create_by_vmo;
    bool supports_mmap;
    bool supports_resize;
    int64_t nsec_granularity;
} fs_info_t;

// Path to mounted filesystem currently being tested
extern const char* kTmpfsPath;
extern const char* kMountPath;

// Path to the mounted filesystem's backing store (if it exists)
extern char test_disk_path[];

// The mounted filesystems's backing ramdisk (if it exists).
extern ramdisk_client_t* test_ramdisk;

// Identify if a real disk is being tested instead of a ramdisk.
extern bool use_real_disk;

// The disk's cached info.
extern block_info_t test_disk_info;

// A filter of the filesystems; indicates which one should be tested.
extern const char* filesystem_name_filter;

// Current filesystem's info
extern fs_info_t* test_info;

extern const fsck_options_t test_fsck_options;

#define NUM_FILESYSTEMS 3
extern fs_info_t FILESYSTEMS[NUM_FILESYSTEMS];

typedef enum fs_test_type {
    // The partition may appear as any generic block device
    FS_TEST_NORMAL,
    // The partition should appear on top of a resizable
    // FVM device
    FS_TEST_FVM,
} fs_test_type_t;

#define TEST_BLOCK_COUNT_DEFAULT (1LLU << 17)
#define TEST_BLOCK_SIZE_DEFAULT (1LLU << 9)
#define TEST_FVM_SLICE_SIZE_DEFAULT (8 * (1 << 20))

typedef struct test_disk {
    uint64_t block_count;
    uint64_t block_size;
    // Only applicable for FVM tests.
    uint64_t slice_size;
} test_disk_t;

extern const test_disk_t default_test_disk;

void setup_fs_test(test_disk_t disk, fs_test_type_t test_class);
void teardown_fs_test(fs_test_type_t test_class);

inline bool can_execute_test(const fs_info_t* info, const test_disk_t* requested_disk,
                             fs_test_type_t t) {

    uint64_t requested_size = requested_disk->block_count * requested_disk->block_size;
    uint64_t real_size = test_disk_info.block_count * test_disk_info.block_size;

    if (use_real_disk && (real_size < requested_size)) {
        printf("Disk too small (is %zu bytes, must be %zu bytes): \n",
               real_size, requested_size);
        return false;
    }

    switch (t) {
    case FS_TEST_NORMAL:
        return info->should_test();
    case FS_TEST_FVM:
        return info->should_test() && info->supports_resize;
    default:
        printf("Unknown filesystem type: ");
        return false;
    }
}

// As a small optimization, avoid even creating a ramdisk
// for filesystem tests when "utest_test_type" is not at
// LEAST size "medium". This avoids the overhead of creating
// a ramdisk when running small tests.
#define BEGIN_FS_TEST_CASE(case_name, disk, fs_type, fs_name, info)  \
    BEGIN_TEST_CASE(case_name##_##fs_name)                           \
    if (utest_test_type & ~TEST_SMALL) {                             \
        test_info = info;                                            \
        if (can_execute_test(test_info, &disk, fs_type)) {           \
            setup_fs_test(disk, fs_type);

#define END_FS_TEST_CASE(case_name, fs_type, fs_name) \
            teardown_fs_test(fs_type);                \
        } else {                                      \
            printf("Filesystem not tested\n");        \
        }                                             \
    }                                                 \
    END_TEST_CASE(case_name##_##fs_name)

#define FS_TEST_CASE(case_name, disk, CASE_TESTS, test_type, fs_type, index)     \
    BEGIN_FS_TEST_CASE(case_name, disk, test_type, fs_type, &FILESYSTEMS[index]) \
    CASE_TESTS                                                                   \
    END_FS_TEST_CASE(case_name, test_type, fs_type)

#define RUN_FOR_ALL_FILESYSTEMS_TYPE(case_name, disk, test_type, CASE_TESTS)     \
    FS_TEST_CASE(case_name, disk, CASE_TESTS, test_type, memfs, 0)  \
    FS_TEST_CASE(case_name, disk, CASE_TESTS, test_type, minfs, 1)

#define RUN_FOR_ALL_FILESYSTEMS_SIZE(case_name, disk, CASE_TESTS)          \
    FS_TEST_CASE(case_name, disk, CASE_TESTS, FS_TEST_NORMAL, memfs, 0)    \
    FS_TEST_CASE(case_name, disk, CASE_TESTS, FS_TEST_NORMAL, minfs, 1)    \
    FS_TEST_CASE(case_name##_fvm, disk, CASE_TESTS, FS_TEST_FVM, minfs, 1)

#define RUN_FOR_ALL_FILESYSTEMS(case_name, CASE_TESTS)                     \
    RUN_FOR_ALL_FILESYSTEMS_SIZE(case_name, default_test_disk, CASE_TESTS)

__END_CDECLS;
