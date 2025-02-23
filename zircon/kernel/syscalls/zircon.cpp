// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <explicit-memory/bytes.h>
#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <ktl/atomic.h>
#include <lib/crypto/global_prng.h>
#include <lib/user_copy/user_ptr.h>
#include <object/event_dispatcher.h>
#include <object/event_pair_dispatcher.h>
#include <object/handle.h>
#include <object/log_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resource.h>
#include <object/thread_dispatcher.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>

#include <zircon/syscalls/log.h>
#include <zircon/syscalls/policy.h>
#include <zircon/types.h>

#include "priv.h"

#define LOCAL_TRACE 0

constexpr size_t kMaxCPRNGDraw = ZX_CPRNG_DRAW_MAX_LEN;
constexpr size_t kMaxCPRNGSeed = ZX_CPRNG_ADD_ENTROPY_MAX_LEN;

// zx_status_t zx_nanosleep
zx_status_t sys_nanosleep(zx_time_t deadline) {
    LTRACEF("nseconds %" PRIi64 "\n", deadline);

    if (deadline <= 0) {
        thread_yield();
        return ZX_OK;
    }

    const auto up = ProcessDispatcher::GetCurrent();
    const Deadline slackDeadline(deadline, up->GetTimerSlackPolicy());
    const zx_time_t now = current_time();

    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::SLEEPING);

    // This syscall is declared as "blocking" in syscalls.abigen, so a higher
    // layer will automatically retry if we return ZX_ERR_INTERNAL_INTR_RETRY.
    return thread_sleep_etc(slackDeadline, /* interruptable */ true, now);
}

// This must be accessed atomically from any given thread.
//
// NOTE(abdulla): This is used by pvclock. If logic here is changed, please
// update pvclock too.
ktl::atomic<int64_t> utc_offset;

zx_time_t sys_clock_get(zx_clock_t clock_id) {
    switch (clock_id) {
    case ZX_CLOCK_MONOTONIC:
        return current_time();
    case ZX_CLOCK_UTC:
        return current_time() + utc_offset.load();
    case ZX_CLOCK_THREAD:
        return ThreadDispatcher::GetCurrent()->runtime_ns();
    default:
        //TODO: figure out the best option here
        return 0u;
    }
}

// zx_status_t zx_clock_get_new
zx_status_t sys_clock_get_new(zx_clock_t clock_id, user_out_ptr<zx_time_t> out_time) {
    zx_time_t time;
    switch (clock_id) {
    case ZX_CLOCK_MONOTONIC:
        time = current_time();
        break;
    case ZX_CLOCK_UTC:
        time = current_time() + utc_offset.load();
        break;
    case ZX_CLOCK_THREAD:
        time = ThreadDispatcher::GetCurrent()->runtime_ns();
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    return out_time.copy_to_user(time);
}

zx_time_t sys_clock_get_monotonic() {
    return current_time();
}

// zx_status_t zx_clock_adjust
zx_status_t sys_clock_adjust(zx_handle_t hrsrc, zx_clock_t clock_id, int64_t offset) {
    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    switch (clock_id) {
    case ZX_CLOCK_MONOTONIC:
        return ZX_ERR_ACCESS_DENIED;
    case ZX_CLOCK_UTC:
        utc_offset.store(offset);
        return ZX_OK;
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

// zx_status_t zx_event_create
zx_status_t sys_event_create(uint32_t options, user_out_handle* event_out) {
    LTRACEF("options 0x%x\n", options);

    if (options != 0u)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryBasicPolicy(ZX_POL_NEW_EVENT);
    if (res != ZX_OK)
        return res;

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;

    zx_status_t result = EventDispatcher::Create(options, &dispatcher, &rights);
    if (result == ZX_OK)
        result = event_out->make(ktl::move(dispatcher), rights);
    return result;
}

// zx_status_t zx_eventpair_create
zx_status_t sys_eventpair_create(uint32_t options,
                                 user_out_handle* out0,
                                 user_out_handle* out1) {
    if (options != 0u) // No options defined/supported yet.
        return ZX_ERR_NOT_SUPPORTED;

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryBasicPolicy(ZX_POL_NEW_EVENTPAIR);
    if (res != ZX_OK)
        return res;

    fbl::RefPtr<Dispatcher> epd0, epd1;
    zx_rights_t rights;
    zx_status_t result = EventPairDispatcher::Create(&epd0, &epd1, &rights);

    if (result == ZX_OK)
        result = out0->make(ktl::move(epd0), rights);
    if (result == ZX_OK)
        result = out1->make(ktl::move(epd1), rights);

    return result;
}

// zx_status_t zx_debuglog_create
zx_status_t sys_debuglog_create(zx_handle_t rsrc, uint32_t options,
                                user_out_handle* out) {
    LTRACEF("options 0x%x\n", options);

    // TODO(ZX-2184) Require a non-INVALID handle.
    if (rsrc != ZX_HANDLE_INVALID) {
        // TODO(ZX-971): finer grained validation
        zx_status_t status = validate_resource(rsrc, ZX_RSRC_KIND_ROOT);
        if (status != ZX_OK)
            return status;
    }

    // create a Log dispatcher
    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    zx_status_t result = LogDispatcher::Create(options, &dispatcher, &rights);
    if (result != ZX_OK)
        return result;

    // by default log objects are write-only
    // as readable logs are more expensive
    if (options & ZX_LOG_FLAG_READABLE) {
        rights |= ZX_RIGHT_READ;
    }

    // create a handle and attach the dispatcher to it
    return out->make(ktl::move(dispatcher), rights);
}

// zx_status_t zx_debuglog_write
zx_status_t sys_debuglog_write(zx_handle_t log_handle, uint32_t options,
                               user_in_ptr<const void> ptr, size_t len) {
    LTRACEF("log handle %x, opt %x, ptr 0x%p, len %zu\n", log_handle, options, ptr.get(), len);

    len = len > DLOG_MAX_DATA ? DLOG_MAX_DATA : len;

    if (options & (~ZX_LOG_FLAGS_MASK))
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<LogDispatcher> log;
    zx_status_t status = up->GetDispatcherWithRights(log_handle, ZX_RIGHT_WRITE, &log);
    if (status != ZX_OK)
        return status;

    char buf[DLOG_MAX_RECORD];
    if (ptr.reinterpret<const char>().copy_array_from_user(buf, len) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    return log->Write(options, buf, len);
}

// zx_status_t zx_debuglog_read
zx_status_t sys_debuglog_read(zx_handle_t log_handle, uint32_t options,
                              user_out_ptr<void> ptr, size_t len) {
    LTRACEF("log handle %x, opt %x, ptr 0x%p, len %zu\n", log_handle, options, ptr.get(), len);

    if (options != 0)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<LogDispatcher> log;
    zx_status_t status = up->GetDispatcherWithRights(log_handle, ZX_RIGHT_READ, &log);
    if (status != ZX_OK)
        return status;

    char buf[DLOG_MAX_RECORD];
    size_t actual;
    if ((status = log->Read(options, buf, DLOG_MAX_RECORD, &actual)) < 0)
        return status;

    if (ptr.copy_array_to_user(buf, actual) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    return static_cast<zx_status_t>(actual);
}

// zx_status_t zx_cprng_draw_once
zx_status_t sys_cprng_draw_once(user_out_ptr<void> buffer, size_t len) {
    if (len > kMaxCPRNGDraw)
        return ZX_ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGDraw];
    // Ensure we get rid of the stack copy of the random data as this function returns.
    explicit_memory::ZeroDtor<uint8_t> zero_guard(kernel_buf, sizeof(kernel_buf));

    auto prng = crypto::GlobalPRNG::GetInstance();
    ASSERT(prng->is_thread_safe());
    prng->Draw(kernel_buf, len);

    if (buffer.copy_array_to_user(kernel_buf, len) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;
    return ZX_OK;
}

// zx_status_t zx_cprng_add_entropy
zx_status_t sys_cprng_add_entropy(user_in_ptr<const void> buffer, size_t buffer_size) {
    if (buffer_size > kMaxCPRNGSeed)
        return ZX_ERR_INVALID_ARGS;

    uint8_t kernel_buf[kMaxCPRNGSeed];
    // Ensure we get rid of the stack copy of the entropy as this function
    // returns.
    explicit_memory::ZeroDtor<uint8_t> zero_guard(kernel_buf, sizeof(kernel_buf));

    if (buffer.copy_array_from_user(kernel_buf, buffer_size) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    auto prng = crypto::GlobalPRNG::GetInstance();
    ASSERT(prng->is_thread_safe());
    prng->AddEntropy(kernel_buf, buffer_size);

    return ZX_OK;
}
