// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.libraryb banjo file

#pragma once

#include <banjo/examples/librarya.h>
#include <banjo/examples/libraryb.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "libraryb-internal.h"

// DDK libraryb-protocol support
//
// :: Proxies ::
//
// ddk::ViewProtocolClient is a simple wrapper around
// view_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ViewProtocol is a mixin class that simplifies writing DDK drivers
// that implement the view protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_VIEW device.
// class ViewDevice;
// using ViewDeviceType = ddk::Device<ViewDevice, /* ddk mixins */>;
//
// class ViewDevice : public ViewDeviceType,
//                      public ddk::ViewProtocol<ViewDevice> {
//   public:
//     ViewDevice(zx_device_t* parent)
//         : ViewDeviceType(parent) {}
//
//     void ViewMoveTo(const point_t* p);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class ViewProtocol : public Base {
public:
    ViewProtocol() {
        internal::CheckViewProtocolSubclass<D>();
        view_protocol_ops_.move_to = ViewMoveTo;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_VIEW;
            dev->ddk_proto_ops_ = &view_protocol_ops_;
        }
    }

protected:
    view_protocol_ops_t view_protocol_ops_ = {};

private:
    static void ViewMoveTo(void* ctx, const point_t* p) {
        static_cast<D*>(ctx)->ViewMoveTo(p);
    }
};

class ViewProtocolClient {
public:
    ViewProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    ViewProtocolClient(const view_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    ViewProtocolClient(zx_device_t* parent) {
        view_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_VIEW, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    void GetProto(view_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    void MoveTo(const point_t* p) const {
        ops_->move_to(ctx_, p);
    }

private:
    view_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
