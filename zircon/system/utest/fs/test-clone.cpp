// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lib/fdio/limits.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

#include "filesystems.h"

bool TestCloneSimple(void) {
    BEGIN_TEST;

    int fd = open("::file", O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);

    zx_handle_t handle = ZX_HANDLE_INVALID;
    ASSERT_EQ(fdio_fd_clone(fd, &handle), ZX_OK);

    int fd2 = -1;
    ASSERT_EQ(fdio_fd_create(handle, &fd2), ZX_OK);

    // Output from one fd...
    char output[5];
    memset(output, 'a', sizeof(output));
    ASSERT_EQ(write(fd, output, sizeof(output)), sizeof(output));

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);

    // ... Should be visible to the other fd.
    char input[5];
    ASSERT_EQ(read(fd2, input, sizeof(input)), sizeof(input));
    ASSERT_EQ(memcmp(input, output, sizeof(input)), 0);

    // Clean up
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(fd2), 0);
    ASSERT_EQ(unlink("::file"), 0);
    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(clone_tests,
    RUN_TEST_MEDIUM(TestCloneSimple)
)
