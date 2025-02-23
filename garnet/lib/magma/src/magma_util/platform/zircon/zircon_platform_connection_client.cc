// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_connection.h"

#include <mutex>

#include "platform_connection_client.h"
#include <fuchsia/gpu/magma/c/fidl.h>
#include <fuchsia/gpu/magma/cpp/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/zx/channel.h>

namespace {
// Convert zx channel status to magma status.
static magma_status_t MagmaChannelStatus(const zx_status_t status)
{
    switch (status) {
        case ZX_OK:
            return MAGMA_STATUS_OK;
        case ZX_ERR_PEER_CLOSED:
            return MAGMA_STATUS_CONNECTION_LOST;
        case ZX_ERR_TIMED_OUT:
            return MAGMA_STATUS_TIMED_OUT;
        default:
            return MAGMA_STATUS_INTERNAL_ERROR;
    }
}
} // namespace

namespace magma {

#if MAGMA_FIDL

/**
 *  ZirconPlatformConnectionClient with FIDL
 */
class ZirconPlatformConnectionClient : public PlatformConnectionClient {
public:
    ZirconPlatformConnectionClient(zx::channel channel, zx::channel notification_channel)
        : notification_channel_(std::move(notification_channel))
    {
        magma_fidl_.Bind(std::move(channel));
    }

    // Imports a buffer for use in the system driver
    magma_status_t ImportBuffer(PlatformBuffer* buffer) override
    {
        DLOG("ZirconPlatformConnectionClient: ImportBuffer");
        if (!buffer)
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "attempting to import null buffer");
        uint32_t duplicate_handle;
        if (!buffer->duplicate_handle(&duplicate_handle))
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to get duplicate_handle");

        zx::vmo vmo(duplicate_handle);
        magma_status_t result = MagmaChannelStatus(magma_fidl_->ImportBufferFIDL(std::move(vmo)));
        if (result != MAGMA_STATUS_OK) {
            return DRET_MSG(result, "failed to write to channel");
        }
        return MAGMA_STATUS_OK;
    }

    // Destroys the buffer with |buffer_id| within this connection
    // returns false if the buffer with |buffer_id| has not been imported
    magma_status_t ReleaseBuffer(uint64_t buffer_id) override
    {
        DLOG("ZirconPlatformConnectionClient: ReleaseBuffer");
        magma_status_t result = MagmaChannelStatus(magma_fidl_->ReleaseBufferFIDL(buffer_id));
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type) override
    {
        DLOG("ZirconPlatformConnectionClient: ImportObject");
        zx::handle duplicate_handle(handle);
        magma_status_t result = MagmaChannelStatus(
            magma_fidl_->ImportObjectFIDL(std::move(duplicate_handle), object_type));
        if (result != MAGMA_STATUS_OK) {
            return DRET_MSG(result, "failed to write to channel");
        }
        return MAGMA_STATUS_OK;
    }

    magma_status_t ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) override
    {
        DLOG("ZirconPlatformConnectionClient: ReleaseObject");
        magma_status_t result =
            MagmaChannelStatus(magma_fidl_->ReleaseObjectFIDL(object_id, object_type));
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    // Creates a context and returns the context id
    void CreateContext(uint32_t* context_id_out) override
    {
        DLOG("ZirconPlatformConnectionClient: CreateContext");
        auto context_id = next_context_id_++;
        *context_id_out = context_id;
        magma_status_t result = MagmaChannelStatus(magma_fidl_->CreateContextFIDL(context_id));
        if (result != MAGMA_STATUS_OK)
            SetError(result);
    }

    // Destroys a context for the given id
    void DestroyContext(uint32_t context_id) override
    {
        DLOG("ZirconPlatformConnectionClient: DestroyContext");
        magma_status_t result = MagmaChannelStatus(magma_fidl_->DestroyContextFIDL(context_id));
        if (result != MAGMA_STATUS_OK)
            SetError(result);
    }

    void ExecuteCommandBuffer(uint32_t command_buffer_handle, uint32_t context_id) override
    {
        DLOG("ZirconPlatformConnectionClient: ExecuteCommandBuffer");
        zx::handle duplicate_handle(command_buffer_handle);
        magma_status_t result = MagmaChannelStatus(
            magma_fidl_->ExecuteCommandBufferFIDL(std::move(duplicate_handle), context_id));
        if (result != MAGMA_STATUS_OK)
            SetError(result);
    }

    // Returns the number of commands that will fit within |max_bytes|.
    static int FitCommands(const size_t max_bytes, const int num_buffers,
                           const magma_system_inline_command_buffer* buffers,
                           const int starting_index, uint64_t* command_bytes,
                           uint32_t* num_semaphores)
    {
        int buffer_count = 0;
        uint64_t bytes_used = 0;
        *command_bytes = 0;
        *num_semaphores = 0;
        while (starting_index + buffer_count < num_buffers && bytes_used < max_bytes) {
            *command_bytes += buffers[starting_index + buffer_count].size;
            *num_semaphores += buffers[starting_index + buffer_count].semaphore_count;
            bytes_used = *command_bytes + *num_semaphores * sizeof(uint64_t);
            buffer_count++;
        }
        if (bytes_used > max_bytes)
            return (buffer_count - 1);
        return buffer_count;
    }

    void ExecuteImmediateCommands(uint32_t context_id, uint64_t num_buffers,
                                  magma_system_inline_command_buffer* buffers) override
    {
        DLOG("ZirconPlatformConnectionClient: ExecuteImmediateCommandsFIDL");
        uint64_t buffers_sent = 0;
        while (buffers_sent < num_buffers) {
            // Tally up the number of commands to send in this batch.
            uint64_t command_bytes = 0;
            uint32_t num_semaphores = 0;
            int buffers_to_send =
                FitCommands(fuchsia::gpu::magma::kReceiveBufferSize, num_buffers, buffers,
                            buffers_sent, &command_bytes, &num_semaphores);

            // TODO(MA-536): Figure out how to move command and semaphore bytes across the FIDL
            //               interface without copying.
            std::vector<uint8_t> command_vec;
            command_vec.reserve(command_bytes);
            std::vector<uint64_t> semaphore_vec;
            semaphore_vec.reserve(num_semaphores);
            for (int i = 0; i < buffers_to_send; ++i) {
                const auto& buffer = buffers[buffers_sent + i];
                const auto buffer_data = static_cast<uint8_t*>(buffer.data);
                std::copy(buffer_data, buffer_data + buffer.size, std::back_inserter(command_vec));
                std::copy(buffer.semaphores, buffer.semaphores + buffer.semaphore_count,
                          std::back_inserter(semaphore_vec));
            }
            magma_status_t result = MagmaChannelStatus(magma_fidl_->ExecuteImmediateCommandsFIDL(
                context_id, std::move(command_vec), std::move(semaphore_vec)));
            if (result != MAGMA_STATUS_OK)
                SetError(result);
            buffers_sent += buffers_to_send;
        }
    }

    magma_status_t GetError() override
    {
        // We need a lock around the channel write and read, because otherwise it's possible two
        // threads will send the GetErrorOp, the first WaitError will get a response and read it,
        // and the second WaitError will wake up because of the first response and error out because
        // there's no message available to read yet.
        std::lock_guard<std::mutex> lock(get_error_lock_);
        magma_status_t error = error_;
        error_ = 0;
        if (error != MAGMA_STATUS_OK)
            return error;
        magma_status_t status = MagmaChannelStatus(magma_fidl_->GetErrorFIDL(&error));
        if (status != MAGMA_STATUS_OK)
            return status;
        return error;
    }

    magma_status_t MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                                uint64_t page_count, uint64_t flags) override
    {
        DLOG("ZirconPlatformConnectionClient: MapBufferGpu");
        magma_status_t result = MagmaChannelStatus(
            magma_fidl_->MapBufferGpuFIDL(buffer_id, gpu_va, page_offset, page_count, flags));
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    magma_status_t UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override
    {
        DLOG("ZirconPlatformConnectionClient: UnmapBufferGpu");
        magma_status_t result =
            MagmaChannelStatus(magma_fidl_->UnmapBufferGpuFIDL(buffer_id, gpu_va));
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    magma_status_t CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                uint64_t page_count) override
    {
        DLOG("ZirconPlatformConnectionClient: CommitBuffer");
        magma_status_t result =
            MagmaChannelStatus(magma_fidl_->CommitBufferFIDL(buffer_id, page_offset, page_count));
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    void SetError(magma_status_t error)
    {
        std::lock_guard<std::mutex> lock(get_error_lock_);
        if (!error_)
            error_ = DRET_MSG(error, "ZirconPlatformConnectionClient encountered dispatcher error");
    }

    uint32_t GetNotificationChannelHandle() override { return notification_channel_.get(); }

    magma_status_t ReadNotificationChannel(void* buffer, size_t buffer_size,
                                           size_t* buffer_size_out) override
    {
        uint32_t buffer_actual_size;
        zx_status_t status = notification_channel_.read(0, buffer, buffer_size, &buffer_actual_size,
                                                        nullptr, 0, nullptr);
        *buffer_size_out = buffer_actual_size;
        if (status == ZX_ERR_SHOULD_WAIT) {
            *buffer_size_out = 0;
            return MAGMA_STATUS_OK;
        } else if (status == ZX_OK) {
            return MAGMA_STATUS_OK;
        } else if (status == ZX_ERR_PEER_CLOSED) {
            return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "notification channel, closed");
        } else {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                            "failed to wait on notification channel status %u", status);
        }
    }

    magma_status_t WaitNotificationChannel(int64_t timeout_ns) override
    {
        zx_signals_t pending;
        zx_status_t status =
            notification_channel_.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                           zx::deadline_after(zx::nsec(timeout_ns)), &pending);
        if (status != ZX_OK)
            return DRET(MagmaChannelStatus(status));
        if (pending & ZX_CHANNEL_READABLE)
            return MAGMA_STATUS_OK;
        if (pending & ZX_CHANNEL_PEER_CLOSED)
            return DRET(MAGMA_STATUS_CONNECTION_LOST);
        DASSERT(false);
        return MAGMA_STATUS_INTERNAL_ERROR;
    }

private:
    fuchsia::gpu::magma::PrimarySyncPtr magma_fidl_;
    zx::channel notification_channel_;
    uint32_t next_context_id_ = 1;
    std::mutex get_error_lock_;
    MAGMA_GUARDED(get_error_lock_) magma_status_t error_{};
}; // class ZirconPlatformConnectionClient

#else  // MAGMA_FIDL

/**
 *  ZirconPlatformConnectionClient without FIDL
 */
class ZirconPlatformConnectionClient : public PlatformConnectionClient {
public:
    ZirconPlatformConnectionClient(zx::channel channel, zx::channel notification_channel)
        : channel_(std::move(channel)), notification_channel_(std::move(notification_channel))
    {
    }

    // Imports a buffer for use in the system driver
    magma_status_t ImportBuffer(PlatformBuffer* buffer) override
    {
        DLOG("ZirconPlatformConnectionClient: ImportBuffer");
        if (!buffer)
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "attempting to import null buffer");
        uint32_t duplicate_handle;
        if (!buffer->duplicate_handle(&duplicate_handle))
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to get duplicate_handle");

        ImportBufferOp op;
        zx_handle_t duplicate_handle_zx = duplicate_handle;
        magma_status_t result = channel_write(&op, sizeof(op), &duplicate_handle_zx, 1);
        if (result != MAGMA_STATUS_OK) {
            DLOG("ZirconPlatformConnectionClient: ImportBuffer - failed to write to channel");
            return DRET_MSG(result, "failed to write to channel");
        }
        return MAGMA_STATUS_OK;
    }

    // Destroys the buffer with |buffer_id| within this connection
    // returns false if the buffer with |buffer_id| has not been imported
    magma_status_t ReleaseBuffer(uint64_t buffer_id) override
    {
        DLOG("ZirconPlatformConnectionClient: ReleaseBuffer");
        ReleaseBufferOp op;
        op.buffer_id = buffer_id;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type) override
    {
        DLOG("ZirconPlatformConnectionClient: ImportObject");
        ImportObjectOp op;
        op.object_type = object_type;
        zx_handle_t duplicate_handle_zx = handle;
        magma_status_t result = channel_write(&op, sizeof(op), &duplicate_handle_zx, 1);
        if (result != MAGMA_STATUS_OK) {
            return DRET_MSG(result, "failed to write to channel");
        }
        return MAGMA_STATUS_OK;
    }

    magma_status_t ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) override
    {
        DLOG("ZirconPlatformConnectionClient: ReleaseObject");
        ReleaseObjectOp op;
        op.object_id = object_id;
        op.object_type = object_type;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    // Creates a context and returns the context id
    void CreateContext(uint32_t* context_id_out) override
    {
        DLOG("ZirconPlatformConnectionClient: CreateContext");
        auto context_id = next_context_id_++;
        *context_id_out = context_id;
        CreateContextOp op;
        op.context_id = context_id;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            SetError(result);
    }

    // Destroys a context for the given id
    void DestroyContext(uint32_t context_id) override
    {
        DLOG("ZirconPlatformConnectionClient: DestroyContext");
        DestroyContextOp op;
        op.context_id = context_id;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            SetError(result);
    }

    void ExecuteCommandBuffer(uint32_t command_buffer_handle, uint32_t context_id) override
    {
        DLOG("ZirconPlatformConnectionClient: ExecuteCommandBuffer");
        ExecuteCommandBufferOp op;
        op.context_id = context_id;
        zx_handle_t duplicate_handle_zx = command_buffer_handle;
        magma_status_t result = channel_write(&op, sizeof(op), &duplicate_handle_zx, 1);
        if (result != MAGMA_STATUS_OK)
            SetError(result);
    }

    void ExecuteImmediateCommands(uint32_t context_id, uint64_t command_count,
                                  magma_system_inline_command_buffer* command_buffers) override
    {
        DLOG("ZirconPlatformConnectionClient: ExecuteImmediateCommands");
        uint8_t payload[fuchsia::gpu::magma::kReceiveBufferSize];
        uint64_t commands_sent = 0;
        while (commands_sent < command_count) {
            auto op = new (payload) ExecuteImmediateCommandsOp;
            op->context_id = context_id;

            uint64_t space_used = sizeof(ExecuteImmediateCommandsOp);
            uint64_t semaphores_used = 0;
            uint64_t last_command;
            for (last_command = commands_sent; last_command < command_count; last_command++) {
                uint64_t command_space =
                    command_buffers[last_command].size +
                    command_buffers[last_command].semaphore_count * sizeof(uint64_t);
                space_used += command_space;
                if (space_used > sizeof(payload))
                    break;
                semaphores_used += command_buffers[last_command].semaphore_count;
            }
            op->semaphore_count = semaphores_used;
            uint64_t* semaphore_data = op->semaphores;
            uint8_t* command_data = op->command_data();
            uint64_t command_data_used = 0;
            for (uint64_t i = commands_sent; i < last_command; i++) {
                memcpy(semaphore_data, command_buffers[i].semaphores,
                       command_buffers[i].semaphore_count * sizeof(uint64_t));
                semaphore_data += command_buffers[i].semaphore_count;
                memcpy(command_data, command_buffers[i].data, command_buffers[i].size);
                command_data += command_buffers[i].size;
                command_data_used += command_buffers[i].size;
            }
            op->commands_size = command_data_used;
            commands_sent = last_command;
            uint64_t payload_size =
                ExecuteImmediateCommandsOp::size(op->semaphore_count, op->commands_size);
            DASSERT(payload_size <= sizeof(payload));
            magma_status_t result = channel_write(payload, payload_size, nullptr, 0);
            if (result != MAGMA_STATUS_OK)
                SetError(result);
        }
    }

    magma_status_t GetError() override
    {
        // We need a lock around the channel write and read, because otherwise it's possible two
        // threads will send the GetErrorOp, the first WaitError will get a response and read it,
        // and the second WaitError will wake up because of the first response and error out because
        // there's no message available to read yet.
        std::lock_guard<std::mutex> lock(get_error_lock_);
        magma_status_t error = error_;
        error_ = 0;
        if (error != MAGMA_STATUS_OK)
            return error;
        GetErrorOp op;
        magma_status_t status = channel_write(&op, sizeof(op), nullptr, 0);
        if (status != MAGMA_STATUS_OK)
            return DRET_MSG(status, "failed to write to channel");
        status = WaitError(&error);
        if (status != MAGMA_STATUS_OK)
            return status;
        return error;
    }

    magma_status_t MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                                uint64_t page_count, uint64_t flags) override
    {
        DLOG("ZirconPlatformConnectionClient: MapBufferGpu");
        MapBufferGpuOp op;
        op.buffer_id = buffer_id;
        op.gpu_va = gpu_va;
        op.page_offset = page_offset;
        op.page_count = page_count;
        op.flags = flags;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    magma_status_t UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override
    {
        DLOG("ZirconPlatformConnectionClient: UnmapBufferGpu");
        UnmapBufferGpuOp op;
        op.buffer_id = buffer_id;
        op.gpu_va = gpu_va;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    magma_status_t CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                uint64_t page_count) override
    {
        DLOG("ZirconPlatformConnectionClient: CommitBuffer");
        CommitBufferOp op;
        op.buffer_id = buffer_id;
        op.page_offset = page_offset;
        op.page_count = page_count;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");
        return MAGMA_STATUS_OK;
    }

    void SetError(magma_status_t error)
    {
        std::lock_guard<std::mutex> lock(get_error_lock_);
        if (!error_)
            error_ = DRET_MSG(error, "ZirconPlatformConnectionClient encountered dispatcher error");
    }

    magma_status_t WaitError(magma_status_t* error_out)
    {
        return WaitMessage(reinterpret_cast<uint8_t*>(error_out), sizeof(*error_out), true);
    }

    magma_status_t WaitMessage(uint8_t* msg_out, uint32_t msg_size, bool blocking)
    {
        zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
        zx_signals_t pending = 0;

        zx_status_t status =
            channel_.wait_one(signals, blocking ? zx::time::infinite() : zx::time(), &pending);
        if (status == ZX_ERR_TIMED_OUT) {
            return 0;
        } else if (status == ZX_OK) {
            DLOG("got ZX_OK, blocking: %s, readable: %s, closed %s", blocking ? "true" : "false",
                 pending & ZX_CHANNEL_READABLE ? "true" : "false",
                 pending & ZX_CHANNEL_PEER_CLOSED ? "true" : "false");
            if (pending & ZX_CHANNEL_READABLE) {
                uint32_t actual_bytes;
                zx_status_t status =
                    channel_.read(0, msg_out, msg_size, &actual_bytes, nullptr, 0, nullptr);
                if (status != ZX_OK)
                    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to read from channel");
                if (actual_bytes != msg_size)
                    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                                    "read wrong number of bytes from channel");
            } else if (pending & ZX_CHANNEL_PEER_CLOSED) {
                return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "channel, closed");
            }
            return 0;
        } else {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to wait on channel");
        }
    }

    int GetNotificationChannelFd() override
    {
        return fdio_handle_fd(notification_channel_.get(),
                              ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0, true);
    }

    uint32_t GetNotificationChannelHandle() override { return notification_channel_.get(); }

    magma_status_t ReadNotificationChannel(void* buffer, size_t buffer_size,
                                           size_t* buffer_size_out) override
    {
        uint32_t buffer_actual_size;
        zx_status_t status = notification_channel_.read(0, buffer, buffer_size, &buffer_actual_size,
                                                        nullptr, 0, nullptr);
        *buffer_size_out = buffer_actual_size;
        if (status == ZX_ERR_SHOULD_WAIT) {
            *buffer_size_out = 0;
            return MAGMA_STATUS_OK;
        } else if (status == ZX_OK) {
            return MAGMA_STATUS_OK;
        } else if (status == ZX_ERR_PEER_CLOSED) {
            return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "notification channel, closed");
        } else {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                            "failed to wait on notification channel status %u", status);
        }
    }

private:
    magma_status_t channel_write(const void* bytes, uint32_t num_bytes, const zx_handle_t* handles,
                                 uint32_t num_handles)
    {
        return MagmaChannelStatus(channel_.write(0, bytes, num_bytes, handles, num_handles));
    }

    zx::channel channel_;
    zx::channel notification_channel_;
    uint32_t next_context_id_ = 1;
    std::mutex get_error_lock_;
    MAGMA_GUARDED(get_error_lock_) magma_status_t error_{};
}; // class ZirconPlatformConnectionClient
#endif // MAGMA_FIDL

std::unique_ptr<PlatformConnectionClient>
PlatformConnectionClient::Create(uint32_t device_handle, uint32_t device_notification_handle)
{
    return std::unique_ptr<ZirconPlatformConnectionClient>(new ZirconPlatformConnectionClient(
        zx::channel(device_handle), zx::channel(device_notification_handle)));
}

bool PlatformConnectionClient::Query(int fd, uint64_t query_id, uint64_t* result_out)
{
    fdio_t* fdio = fdio_unsafe_fd_to_io(fd);
    if (!fdio)
        return DRETF(false, "invalid fd: %d", fd);

    zx_status_t status =
        fuchsia_gpu_magma_DeviceQuery(fdio_unsafe_borrow_channel(fdio), query_id, result_out);
    fdio_unsafe_release(fdio);

    if (status != ZX_OK)
        return DRETF(false, "magma_DeviceQuery failed: %d", status);

    return true;
}

bool PlatformConnectionClient::GetHandles(int fd, uint32_t* device_handle_out,
                                          uint32_t* device_notification_handle_out)
{
    fdio_t* fdio = fdio_unsafe_fd_to_io(fd);
    if (!fdio)
        return DRETF(false, "invalid fd: %d", fd);

    zx_status_t status = fuchsia_gpu_magma_DeviceConnect(
        fdio_unsafe_borrow_channel(fdio), magma::PlatformThreadId().id(), device_handle_out,
        device_notification_handle_out);
    fdio_unsafe_release(fdio);

    if (status != ZX_OK)
        return DRETF(false, "magma_DeviceConnect failed: %d", status);

    return true;
}

} // namespace magma
