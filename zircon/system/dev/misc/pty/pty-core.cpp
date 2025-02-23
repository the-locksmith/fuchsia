// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <zircon/device/pty.h>
#include <zircon/errors.h>

#include "pty-core.h"
#include "pty-fifo.h"

#define CTRL_(n) ((n) - 'A' + 1)

#define CTRL_C CTRL_('C')
#define CTRL_S CTRL_('S')
#define CTRL_Z CTRL_('Z')

// clang-format off
#define PTY_CLI_RAW_MODE    (0x00000001u)
#define PTY_CLI_CONTROL     (0x00010000u)
#define PTY_CLI_ACTIVE      (0x00020000u)
#define PTY_CLI_PEER_CLOSED (0x00040000u)
// clang-format on

struct pty_client {
    zx_device_t* zxdev;
    pty_server_t* srv;
    uint32_t id;
    uint32_t flags;
    pty_fifo_t fifo;
    list_node_t node;
};

static zx_status_t pty_openat(pty_server_t* ps, zx_device_t** out, uint32_t id, uint32_t flags);

// pty client device operations

static zx_status_t pty_client_read(void* ctx, void* buf, size_t count, zx_off_t off,
                                   size_t* actual) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    zxlogf(TRACE, "PTY Client %p (id=%u) read\n", pc, pc->id);

    mtx_lock(&ps->lock);
    bool was_full = pty_fifo_is_full(&pc->fifo);
    size_t length = pty_fifo_read(&pc->fifo, buf, count);
    if (pty_fifo_is_empty(&pc->fifo)) {
        device_state_clr(pc->zxdev, DEV_STATE_READABLE);
    }
    if (was_full && length) {
        device_state_set(ps->zxdev, DEV_STATE_WRITABLE);
    }
    mtx_unlock(&ps->lock);

    if (length > 0) {
        *actual = length;
        return ZX_OK;
    } else {
        return (pc->flags & PTY_CLI_PEER_CLOSED) ? ZX_ERR_PEER_CLOSED : ZX_ERR_SHOULD_WAIT;
    }
}

static zx_status_t pty_client_write(void* ctx, const void* buf, size_t count, zx_off_t off,
                                    size_t* actual) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    zxlogf(TRACE, "PTY Client %p (id=%u) write\n", pc, pc->id);

    zx_status_t status;

    mtx_lock(&ps->lock);
    if (pc->flags & PTY_CLI_ACTIVE) {
        size_t length;
        status = ps->recv(ps, buf, count, &length);
        if (status == ZX_OK) {
            *actual = length;
        } else if (status == ZX_ERR_SHOULD_WAIT) {
            device_state_clr(pc->zxdev, DEV_STATE_WRITABLE);
        }
    } else {
        status = (pc->flags & PTY_CLI_PEER_CLOSED) ? ZX_ERR_PEER_CLOSED : ZX_ERR_SHOULD_WAIT;
    }
    mtx_unlock(&ps->lock);

    return status;
}

// mask of invalid features
#define PTY_FEATURE_BAD (~PTY_FEATURE_RAW)

static void pty_make_active_locked(pty_server_t* ps, pty_client_t* pc) {
    zxlogf(TRACE, "PTY Client %p (id=%u) becomes active\n", pc, pc->id);
    if (ps->active != pc) {
        if (ps->active) {
            ps->active->flags &= (~PTY_CLI_ACTIVE);
            device_state_clr(ps->active->zxdev, DEV_STATE_WRITABLE);
        }
        ps->active = pc;
        pc->flags |= PTY_CLI_ACTIVE;
        device_state_set(pc->zxdev, DEV_STATE_WRITABLE);
        if (pty_fifo_is_full(&pc->fifo)) {
            device_state_clr_set(ps->zxdev, DEV_STATE_WRITABLE | DEV_STATE_HANGUP, 0);
        } else {
            device_state_clr_set(ps->zxdev, DEV_STATE_HANGUP, DEV_STATE_WRITABLE);
        }
    }
}

static void pty_adjust_signals_locked(pty_client_t* pc) {
    uint32_t set = 0;
    uint32_t clr = 0;
    if (pc->flags & PTY_CLI_ACTIVE) {
        set = DEV_STATE_WRITABLE;
    } else {
        clr = DEV_STATE_WRITABLE;
    }
    if (pty_fifo_is_empty(&pc->fifo)) {
        clr = DEV_STATE_READABLE;
    } else {
        set = DEV_STATE_READABLE;
    }
    device_state_clr_set(pc->zxdev, clr, set);
}

static zx_status_t pty_client_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                    void* out_buf, size_t out_len, size_t* out_actual) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);

    switch (op) {
    case IOCTL_PTY_CLR_SET_FEATURE: {
        zxlogf(TRACE, "PTY Client %p (id=%u) ioctl: clear and/or set feature\n", pc, pc->id);
        auto cs = static_cast<const pty_clr_set_t*>(in_buf);
        if ((in_len != sizeof(pty_clr_set_t)) || (cs->clr & PTY_FEATURE_BAD) ||
            (cs->set & PTY_FEATURE_BAD)) {
            return ZX_ERR_INVALID_ARGS;
        }
        mtx_lock(&ps->lock);
        pc->flags = (pc->flags & (~cs->clr)) | cs->set;
        mtx_unlock(&ps->lock);
        return ZX_OK;
    }
    case IOCTL_PTY_GET_WINDOW_SIZE: {
        zxlogf(TRACE, "PTY Client %p (id=%u) ioctl: get window size\n", pc, pc->id);
        auto wsz = static_cast<pty_window_size_t*>(out_buf);
        if (out_len != sizeof(pty_window_size_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        mtx_lock(&ps->lock);
        wsz->width = ps->width;
        wsz->height = ps->height;
        mtx_unlock(&ps->lock);
        *out_actual = sizeof(pty_window_size_t);
        return ZX_OK;
    }
    case IOCTL_PTY_MAKE_ACTIVE: {
        zxlogf(TRACE, "PTY Client %p (id=%u) ioctl: make active\n", pc, pc->id);
        if (in_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (!(pc->flags & PTY_CLI_CONTROL)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        uint32_t id = *((uint32_t*)in_buf);
        mtx_lock(&ps->lock);
        pty_client_t* c;
        list_for_every_entry (&ps->clients, c, pty_client_t, node) {
            if (c->id == id) {
                pty_make_active_locked(ps, c);
                mtx_unlock(&ps->lock);
                return ZX_OK;
            }
        }
        mtx_unlock(&ps->lock);
        return ZX_ERR_NOT_FOUND;
    }
    case IOCTL_PTY_READ_EVENTS: {
        zxlogf(TRACE, "PTY Client %p (id=%u) ioctl: read events\n", pc, pc->id);
        if (!(pc->flags & PTY_CLI_CONTROL)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        if (out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        mtx_lock(&ps->lock);
        auto events = static_cast<uint32_t*>(out_buf);
        *events = ps->events;
        ps->events = 0;
        if (ps->active == NULL) {
            *events |= PTY_EVENT_HANGUP;
        }
        device_state_clr(pc->zxdev, PTY_SIGNAL_EVENT);
        mtx_unlock(&ps->lock);
        *out_actual = sizeof(uint32_t);
        return ZX_OK;
    }
    default:
        if (ps->ioctl != NULL) {
            return ps->ioctl(ps, op, in_buf, in_len, out_buf, out_len, out_actual);
        } else {
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
}

static void pty_client_release(void* ctx) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    zxlogf(TRACE, "PTY Client %p (id=%u) release\n", pc, pc->id);

    mtx_lock(&ps->lock);

    // remove client from list of clients and downref server
    list_delete(&pc->node);
    pc->srv = NULL;
    int refcount = --ps->refcount;

    if (ps->control == pc) {
        ps->control = NULL;
    }
    if (ps->active == pc) {
        // signal controlling client as well, if there is one
        if (ps->control) {
            device_state_set(ps->control->zxdev, PTY_SIGNAL_EVENT | DEV_STATE_HANGUP);
        }
        ps->active = NULL;
    }
    // signal server, if the last client has gone away
    if (list_is_empty(&ps->clients)) {
        device_state_clr_set(ps->zxdev, DEV_STATE_WRITABLE, DEV_STATE_READABLE | DEV_STATE_HANGUP);
    }
    mtx_unlock(&ps->lock);

    if (refcount == 0) {
        zxlogf(TRACE, "PTY Server %p release (from client)\n", ps);
        if (ps->release) {
            ps->release(ps);
        } else {
            free(ps);
        }
    }

    free(pc);
}

zx_status_t pty_client_openat(void* ctx, zx_device_t** out, const char* path, uint32_t flags) {
    auto pc = static_cast<pty_client_t*>(ctx);
    auto ps = static_cast<pty_server_t*>(pc->srv);
    uint32_t id = static_cast<uint32_t>(strtoul(path, NULL, 0));
    zxlogf(TRACE, "PTY Client %p (id=%u) openat %u\n", pc, pc->id, id);
    // only controlling clients may create additional clients
    if (!(pc->flags & PTY_CLI_CONTROL)) {
        return ZX_ERR_ACCESS_DENIED;
    }
    // clients may not create controlling clients
    if (id == 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pty_openat(ps, out, id, flags);
}

zx_protocol_device_t pc_ops = []() {
    zx_protocol_device_t ops = {};
    ops.version = DEVICE_OPS_VERSION;
    // ops.open = default, allow cloning
    ops.open_at = pty_client_openat;
    ops.release = pty_client_release;
    ops.read = pty_client_read;
    ops.write = pty_client_write;
    ops.ioctl = pty_client_ioctl;
    return ops;
}();

// used by both client and server ptys to create new client ptys

static zx_status_t pty_openat(pty_server_t* ps, zx_device_t** out, uint32_t id, uint32_t flags) {
    auto pc = static_cast<pty_client_t*>(calloc(1, sizeof(pty_client_t)));
    if (!pc) {
        return ZX_ERR_NO_MEMORY;
    }

    pc->id = id;
    pc->flags = 0;
    pc->fifo.head = 0;
    pc->fifo.tail = 0;
    zx_status_t status;

    unsigned num_clients = 0;
    mtx_lock(&ps->lock);
    // require that client ID is unique
    pty_client_t* c;
    list_for_every_entry (&ps->clients, c, pty_client_t, node) {
        if (c->id == id) {
            mtx_unlock(&ps->lock);
            free(pc);
            return ZX_ERR_INVALID_ARGS;
        }
        num_clients++;
    }
    list_add_tail(&ps->clients, &pc->node);

    ps->refcount++;
    mtx_unlock(&ps->lock);

    pc->srv = ps;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "pty";
    args.ctx = pc;
    args.ops = &pc_ops;
    args.flags = DEVICE_ADD_INSTANCE;

    status = device_add(ps->zxdev, &args, &pc->zxdev);
    if (status < 0) {
        pty_client_release(pc->zxdev);
        return status;
    }

    if (ps->active == NULL) {
        pty_make_active_locked(ps, pc);
    }
    if (id == 0) {
        ps->control = pc;
        pc->flags |= PTY_CLI_CONTROL;
    }

    zxlogf(TRACE, "PTY Client %p (id=%u) created (server %p)\n", pc, pc->id, ps);

    mtx_lock(&ps->lock);
    if (num_clients == 0) {
        // if there were no clients, make sure we take server
        // out of HANGUP and READABLE, where it landed if all
        // its clients had closed
        device_state_clr(ps->zxdev, DEV_STATE_READABLE | DEV_STATE_HANGUP);
    }
    pty_adjust_signals_locked(pc);
    mtx_unlock(&ps->lock);

    *out = pc->zxdev;
    return ZX_OK;
}

// pty server device operations

void pty_server_resume_locked(pty_server_t* ps) {
    if (ps->active) {
        device_state_set(ps->active->zxdev, DEV_STATE_WRITABLE);
    }
}

zx_status_t pty_server_send(pty_server_t* ps, const void* data, size_t len, bool atomic,
                            size_t* actual) {
    // TODO: rw signals
    zxlogf(TRACE, "PTY Server %p send\n", ps);
    zx_status_t status;
    mtx_lock(&ps->lock);
    if (ps->active) {
        pty_client_t* pc = ps->active;
        bool was_empty = pty_fifo_is_empty(&pc->fifo);
        if (atomic || (pc->flags & PTY_CLI_RAW_MODE)) {
            *actual = pty_fifo_write(&pc->fifo, data, len, atomic);
        } else {
            if (len > PTY_FIFO_SIZE) {
                len = PTY_FIFO_SIZE;
            }
            auto ch = static_cast<const uint8_t*>(data);
            unsigned n = 0;
            unsigned evt = 0;
            while (n < len) {
                if (*ch++ == CTRL_C) {
                    evt = PTY_EVENT_INTERRUPT;
                    break;
                }
                n++;
            }
            size_t r = pty_fifo_write(&pc->fifo, data, n, false);
            if ((r == n) && evt) {
                // consume the event
                r++;
                ps->events |= evt;
                zxlogf(TRACE, "PTY Client %p event %x\n", ps, evt);
                if (ps->control) {
                    static_assert(PTY_SIGNAL_EVENT == DEV_STATE_OOB);
                    device_state_set(ps->control->zxdev, PTY_SIGNAL_EVENT);
                }
            }
            *actual = r;
        }
        if (was_empty && *actual) {
            device_state_set(pc->zxdev, DEV_STATE_READABLE);
        }
        if (pty_fifo_is_full(&pc->fifo)) {
            device_state_clr(ps->zxdev, DEV_STATE_WRITABLE);
        }
        status = ZX_OK;
    } else {
        *actual = 0;
        status = ZX_ERR_PEER_CLOSED;
    }
    mtx_unlock(&ps->lock);
    return status;
}

void pty_server_set_window_size(pty_server_t* ps, uint32_t w, uint32_t h) {
    zxlogf(TRACE, "PTY Server %p set window size %ux%u\n", ps, w, h);
    mtx_lock(&ps->lock);
    ps->width = w;
    ps->height = h;
    // TODO signal?
    mtx_unlock(&ps->lock);
}

zx_status_t pty_server_openat(void* ctx, zx_device_t** out, const char* path, uint32_t flags) {
    auto ps = static_cast<pty_server_t*>(ctx);
    uint32_t id = static_cast<uint32_t>(strtoul(path, NULL, 0));
    zxlogf(TRACE, "PTY Server %p openat %u\n", ps, id);
    return pty_openat(ps, out, id, flags);
}

void pty_server_release(void* ctx) {
    auto ps = static_cast<pty_server_t*>(ctx);

    mtx_lock(&ps->lock);
    // inform clients that server is gone
    pty_client_t* pc;
    list_for_every_entry (&ps->clients, pc, pty_client_t, node) {
        pc->flags = (pc->flags & (~PTY_CLI_ACTIVE)) | PTY_CLI_PEER_CLOSED;
        device_state_set(pc->zxdev, DEV_STATE_HANGUP);
    }
    int32_t refcount = --ps->refcount;
    mtx_unlock(&ps->lock);

    if (refcount == 0) {
        zxlogf(TRACE, "PTY Server %p release (from server)\n", ps);
        if (ps->release) {
            ps->release(ps);
        } else {
            free(ps);
        }
    }
}

void pty_server_init(pty_server_t* ps) {
    zxlogf(TRACE, "PTY Server %p init\n", ps);
    mtx_init(&ps->lock, mtx_plain);
    ps->refcount = 1;
    list_initialize(&ps->clients);
    ps->active = NULL;
    ps->control = NULL;
    ps->events = 0;
    ps->width = 0;
    ps->height = 0;
}
