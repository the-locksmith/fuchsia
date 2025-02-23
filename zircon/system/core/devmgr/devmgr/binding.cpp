// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <stdio.h>

#include "coordinator.h"

namespace {

struct BindProgramContext {
    const fbl::Array<const zx_device_prop_t>* props;
    uint32_t protocol_id;
    uint32_t binding_size;
    const zx_bind_inst_t* binding;
    const char* name;
    uint32_t autobind;
};

uint32_t dev_get_prop(BindProgramContext* ctx, uint32_t id) {
    for (const auto prop : *ctx->props) {
        if (prop.id == id) {
            return prop.value;
        }
    }

    // fallback for devices without properties
    switch (id) {
    case BIND_PROTOCOL:
        return ctx->protocol_id;
    case BIND_AUTOBIND:
        return ctx->autobind;
    default:
        // TODO: better process for missing properties
        return 0;
    }
}

bool is_bindable(BindProgramContext* ctx) {
    const zx_bind_inst_t* ip = ctx->binding;
    const zx_bind_inst_t* end = ip + (ctx->binding_size / sizeof(zx_bind_inst_t));
    uint32_t flags = 0;

    while (ip < end) {
        uint32_t inst = ip->op;
        bool cond;

        if (BINDINST_CC(inst) != COND_AL) {
            uint32_t value = ip->arg;
            uint32_t pid = BINDINST_PB(inst);
            uint32_t pval;
            if (pid != BIND_FLAGS) {
                pval = dev_get_prop(ctx, pid);
            } else {
                pval = flags;
            }

            // evaluate condition
            switch (BINDINST_CC(inst)) {
            case COND_EQ:
                cond = (pval == value);
                break;
            case COND_NE:
                cond = (pval != value);
                break;
            case COND_LT:
                cond = (pval < value);
                break;
            case COND_GT:
                cond = (pval > value);
                break;
            case COND_LE:
                cond = (pval <= value);
                break;
            case COND_GE:
                cond = (pval >= value);
                break;
            default:
                // illegal instruction: abort
                printf("devmgr: driver '%s' illegal bindinst 0x%08x\n", ctx->name, inst);
                return false;
            }
        } else {
            cond = true;
        }

        if (cond) {
            switch (BINDINST_OP(inst)) {
            case OP_ABORT:
                return false;
            case OP_MATCH:
                return true;
            case OP_GOTO: {
                uint32_t label = BINDINST_PA(inst);
                while (++ip < end) {
                    if ((BINDINST_OP(ip->op) == OP_LABEL) && (BINDINST_PA(ip->op) == label)) {
                        goto next_instruction;
                    }
                }
                printf("devmgr: driver '%s' illegal GOTO\n", ctx->name);
                return false;
            }
            case OP_LABEL:
                // no op
                break;
            default:
                // illegal instruction: abort
                printf("devmgr: driver '%s' illegal bindinst 0x%08x\n", ctx->name, inst);
                return false;
            }
        }

    next_instruction:
        ip++;
    }

    // default if we leave the program is no-match
    return false;
}

} // namespace

namespace devmgr {

bool dc_is_bindable(const Driver* drv, uint32_t protocol_id,
                    const fbl::Array<const zx_device_prop_t>& props, bool autobind) {
    if (drv->binding_size == 0) {
        return false;
    }
    BindProgramContext ctx;
    ctx.props = &props;
    ctx.protocol_id = protocol_id;
    ctx.binding = drv->binding.get();
    ctx.binding_size = drv->binding_size;
    ctx.name = drv->name.c_str();
    ctx.autobind = autobind ? 1 : 0;
    return is_bindable(&ctx);
}

} // namespace devmgr
