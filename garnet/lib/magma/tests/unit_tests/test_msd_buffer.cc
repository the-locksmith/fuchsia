// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "msd.h"
#include "platform_buffer.h"
#include "gtest/gtest.h"

TEST(MsdBuffer, ImportAndDestroy)
{
    auto platform_buf = magma::PlatformBuffer::Create(4096, "test");
    ASSERT_NE(platform_buf, nullptr);

    uint32_t duplicate_handle;
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));

    auto msd_buffer = msd_buffer_import(duplicate_handle);
    ASSERT_NE(msd_buffer, nullptr);

    msd_buffer_destroy(msd_buffer);
}

TEST(MsdBuffer, MapAndUnmap)
{
    msd_driver_t* driver = msd_driver_create();
    ASSERT_TRUE(driver);

    std::unique_ptr<magma::PlatformHandle> buffer_handle;
    msd_buffer_t* buffer = nullptr;

    constexpr uint32_t kBufferSizeInPages = 1;

    {
        auto platform_buf = magma::PlatformBuffer::Create(kBufferSizeInPages * PAGE_SIZE, "test");
        ASSERT_TRUE(platform_buf);

        uint32_t raw_handle;
        EXPECT_TRUE(platform_buf->duplicate_handle(&raw_handle));
        buffer_handle = magma::PlatformHandle::Create(raw_handle);
        ASSERT_TRUE(buffer_handle);

        EXPECT_TRUE(platform_buf->duplicate_handle(&raw_handle));
        buffer = msd_buffer_import(raw_handle);
        ASSERT_TRUE(buffer);
    }

    // There should be at least two handles, the msd buffer and the "checker handle".
    uint32_t handle_count;
    EXPECT_TRUE(buffer_handle->GetCount(&handle_count));
    EXPECT_GE(2u, handle_count);

    msd_device_t* device = msd_driver_create_device(driver, GetTestDeviceHandle());
    ASSERT_TRUE(device);

    msd_connection_t* connection = msd_device_open(device, 0);
    ASSERT_TRUE(connection);

    std::vector<uint64_t> gpu_addr{0, PAGE_SIZE * 1024};

    // Mapping should keep alive the msd buffer.
    for (uint32_t i = 0; i < gpu_addr.size(); i++) {
        EXPECT_EQ(MAGMA_STATUS_OK, msd_connection_map_buffer_gpu(connection, buffer,
                                                                 gpu_addr[i],        // gpu addr
                                                                 0,                  // page offset
                                                                 kBufferSizeInPages, // page count
                                                                 MAGMA_GPU_MAP_FLAG_READ |
                                                                     MAGMA_GPU_MAP_FLAG_WRITE));
    }

    // Verify we haven't lost any handles.
    EXPECT_TRUE(buffer_handle->GetCount(&handle_count));
    EXPECT_GE(2u, handle_count);

    // Try to unmap a region that doesn't exist.
    EXPECT_NE(MAGMA_STATUS_OK,
              msd_connection_unmap_buffer_gpu(connection, buffer, PAGE_SIZE * 2048));

    // Unmap the valid regions.
    magma_status_t status;
    for (uint32_t i = 0; i < gpu_addr.size(); i++) {
        status = msd_connection_unmap_buffer_gpu(connection, buffer, gpu_addr[i]);
        EXPECT_TRUE(status == MAGMA_STATUS_UNIMPLEMENTED || status == MAGMA_STATUS_OK);
    }

    if (status != MAGMA_STATUS_OK) {
        // If unmap unsupported, mappings should be released here.
        msd_connection_release_buffer(connection, buffer);
    }

    // Mapping should keep alive the msd buffer.
    for (uint32_t i = 0; i < gpu_addr.size(); i++) {
        EXPECT_EQ(MAGMA_STATUS_OK, msd_connection_map_buffer_gpu(connection, buffer,
                                                                 gpu_addr[i],        // gpu addr
                                                                 0,                  // page offset
                                                                 kBufferSizeInPages, // page count
                                                                 MAGMA_GPU_MAP_FLAG_READ |
                                                                     MAGMA_GPU_MAP_FLAG_WRITE));
    }

    msd_buffer_destroy(buffer);
    msd_connection_close(connection);
    msd_device_destroy(device);
    msd_driver_destroy(driver);
}

TEST(MsdBuffer, MapAndAutoUnmap)
{
    msd_driver_t* driver = msd_driver_create();
    ASSERT_TRUE(driver);

    std::unique_ptr<magma::PlatformHandle> buffer_handle;
    msd_buffer_t* buffer = nullptr;

    constexpr uint32_t kBufferSizeInPages = 1;

    {
        auto platform_buf = magma::PlatformBuffer::Create(kBufferSizeInPages * PAGE_SIZE, "test");
        ASSERT_TRUE(platform_buf);

        uint32_t raw_handle;
        EXPECT_TRUE(platform_buf->duplicate_handle(&raw_handle));
        buffer_handle = magma::PlatformHandle::Create(raw_handle);
        ASSERT_TRUE(buffer_handle);

        EXPECT_TRUE(platform_buf->duplicate_handle(&raw_handle));
        buffer = msd_buffer_import(raw_handle);
        ASSERT_TRUE(buffer);
    }

    // There should be at least two handles, the msd buffer and the "checker handle".
    uint32_t handle_count;
    EXPECT_TRUE(buffer_handle->GetCount(&handle_count));
    EXPECT_GE(2u, handle_count);

    msd_device_t* device = msd_driver_create_device(driver, GetTestDeviceHandle());
    ASSERT_TRUE(device);

    msd_connection_t* connection = msd_device_open(device, 0);
    ASSERT_TRUE(connection);

    // Mapping should keep alive the msd buffer.
    EXPECT_EQ(MAGMA_STATUS_OK,
              msd_connection_map_buffer_gpu(connection, buffer,
                                            0,                  // gpu addr
                                            0,                  // page offset
                                            kBufferSizeInPages, // page count
                                            MAGMA_GPU_MAP_FLAG_READ | MAGMA_GPU_MAP_FLAG_WRITE));

    // Verify we haven't lost any handles.
    EXPECT_TRUE(buffer_handle->GetCount(&handle_count));
    EXPECT_GE(2u, handle_count);

    // Mapping auto released either here...
    msd_connection_release_buffer(connection, buffer);

    // OR here.
    msd_buffer_destroy(buffer);

    // Buffer should be now be released.
    EXPECT_TRUE(buffer_handle->GetCount(&handle_count));
    EXPECT_EQ(1u, handle_count);

    msd_connection_close(connection);
    msd_device_destroy(device);
    msd_driver_destroy(driver);
}

TEST(MsdBuffer, Commit)
{
    msd_driver_t* driver = msd_driver_create();
    ASSERT_TRUE(driver);

    constexpr uint32_t kBufferSizeInPages = 1;

    auto platform_buf = magma::PlatformBuffer::Create(kBufferSizeInPages * PAGE_SIZE, "test");
    ASSERT_NE(platform_buf, nullptr);

    uint32_t duplicate_handle;
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));

    msd_buffer_t* buffer = msd_buffer_import(duplicate_handle);
    ASSERT_TRUE(buffer);

    msd_device_t* device = msd_driver_create_device(driver, GetTestDeviceHandle());
    ASSERT_TRUE(device);

    msd_connection_t* connection = msd_device_open(device, 0);
    ASSERT_TRUE(connection);

    // Bad offset
    magma_status_t status;
    status = msd_connection_commit_buffer(connection, buffer,
                                          kBufferSizeInPages, // page offset
                                          1                   // page count
    );
    EXPECT_NE(MAGMA_STATUS_OK, status);

    // Bad page count
    status = msd_connection_commit_buffer(connection, buffer,
                                          0,                     // page offset
                                          kBufferSizeInPages + 1 // page count
    );
    EXPECT_NE(MAGMA_STATUS_OK, status);

    // Full
    status = msd_connection_commit_buffer(connection, buffer,
                                          0,                 // page offset
                                          kBufferSizeInPages // page count
    );
    EXPECT_TRUE(status == MAGMA_STATUS_OK || status == MAGMA_STATUS_UNIMPLEMENTED);

    // Partial
    status = msd_connection_commit_buffer(connection, buffer,
                                          0, // page offset
                                          1  // page count
    );
    EXPECT_TRUE(status == MAGMA_STATUS_OK || status == MAGMA_STATUS_UNIMPLEMENTED);

    msd_buffer_destroy(buffer);
    msd_connection_close(connection);
    msd_device_destroy(device);
    msd_driver_destroy(driver);
}
