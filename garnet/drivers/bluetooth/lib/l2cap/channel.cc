// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel.h"

#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/run_or_post.h"
#include "garnet/drivers/bluetooth/lib/l2cap/basic_mode_rx_engine.h"
#include "lib/fxl/strings/string_printf.h"

#include "logical_link.h"

namespace btlib {
namespace l2cap {

using common::RunOrPost;

Channel::Channel(ChannelId id, ChannelId remote_id,
                 hci::Connection::LinkType link_type,
                 hci::ConnectionHandle link_handle)
    : id_(id),
      remote_id_(remote_id),
      link_type_(link_type),
      link_handle_(link_handle),

      // TODO(armansito): IWBN if the MTUs could be specified dynamically
      // instead (see NET-308).
      tx_mtu_(kDefaultMTU),
      rx_mtu_(kDefaultMTU) {
  ZX_DEBUG_ASSERT(id_);
  ZX_DEBUG_ASSERT(link_type_ == hci::Connection::LinkType::kLE ||
                  link_type_ == hci::Connection::LinkType::kACL);
}

namespace internal {

ChannelImpl::ChannelImpl(ChannelId id, ChannelId remote_id,
                         fbl::RefPtr<internal::LogicalLink> link,
                         std::list<PDU> buffered_pdus)
    : Channel(id, remote_id, link->type(), link->handle()),
      active_(false),
      dispatcher_(nullptr),
      link_(link),
      rx_engine_(std::make_unique<BasicModeRxEngine>()) {
  ZX_DEBUG_ASSERT(link_);
  for (const auto& pdu : buffered_pdus) {
    auto sdu = std::make_unique<common::DynamicByteBuffer>(pdu.length());
    pdu.Copy(sdu.get());
    pending_rx_sdus_.push(std::move(sdu));
  }
}

const sm::SecurityProperties ChannelImpl::security() {
  std::lock_guard<std::mutex> lock(mtx_);
  if (link_) {
    return link_->security();
  }
  return sm::SecurityProperties();
}

bool ChannelImpl::Activate(RxCallback rx_callback,
                           ClosedCallback closed_callback,
                           async_dispatcher_t* dispatcher) {
  ZX_DEBUG_ASSERT(rx_callback);
  ZX_DEBUG_ASSERT(closed_callback);

  fit::closure task;
  bool run_task = false;

  {
    std::lock_guard<std::mutex> lock(mtx_);

    // Activating on a closed link has no effect. We also clear this on
    // deactivation to prevent a channel from being activated more than once.
    if (!link_)
      return false;

    ZX_DEBUG_ASSERT(!active_);
    active_ = true;
    ZX_DEBUG_ASSERT(!dispatcher_);
    dispatcher_ = dispatcher;
    rx_cb_ = std::move(rx_callback);
    closed_cb_ = std::move(closed_callback);

    // Route the buffered packets.
    if (!pending_rx_sdus_.empty()) {
      run_task = true;
      dispatcher = dispatcher_;
      task = [func = rx_cb_.share(),
              pending = std::move(pending_rx_sdus_)]() mutable {
        while (!pending.empty()) {
          func(std::move(pending.front()));
          pending.pop();
        }
      };
      ZX_DEBUG_ASSERT(pending_rx_sdus_.empty());
    }
  }

  if (run_task) {
    RunOrPost(std::move(task), dispatcher);
  }

  return true;
}

void ChannelImpl::Deactivate() {
  std::lock_guard<std::mutex> lock(mtx_);

  // De-activating on a closed link has no effect.
  if (!link_ || !active_) {
    link_ = nullptr;
    return;
  }

  active_ = false;
  dispatcher_ = nullptr;
  rx_cb_ = {};
  closed_cb_ = {};
  rx_engine_ = {};

  // Tell the link to release this channel on its thread.
  async::PostTask(link_->dispatcher(), [this, link = link_] {
    // |link| is expected to ignore this call if it has been closed.
    link->RemoveChannel(this);
  });

  link_ = nullptr;
}

void ChannelImpl::SignalLinkError() {
  std::lock_guard<std::mutex> lock(mtx_);

  // Cannot signal an error on a closed or deactivated link.
  if (!link_ || !active_)
    return;

  async::PostTask(link_->dispatcher(), [link = link_] {
    // |link| is expected to ignore this call if it has been closed.
    link->SignalError();
  });
}

bool ChannelImpl::Send(common::ByteBufferPtr sdu) {
  ZX_DEBUG_ASSERT(sdu);

  if (sdu->size() > tx_mtu()) {
    bt_log(TRACE, "l2cap", "SDU size exceeds channel TxMTU (channel-id: %#.4x)",
           id());
    return false;
  }

  std::lock_guard<std::mutex> lock(mtx_);

  if (!link_) {
    bt_log(ERROR, "l2cap", "cannot send SDU on a closed link");
    return false;
  }

  // Drop the packet if the channel is inactive.
  if (!active_)
    return false;

  async::PostTask(link_->dispatcher(),
                  [id = remote_id(), link = link_, sdu = std::move(sdu)] {
                    // |link| is expected to ignore this call and drop the
                    // packet if it has been closed.
                    link->SendBasicFrame(id, *sdu);
                  });

  return true;
}

void ChannelImpl::UpgradeSecurity(sm::SecurityLevel level,
                                  sm::StatusCallback callback) {
  ZX_DEBUG_ASSERT(callback);

  std::lock_guard<std::mutex> lock(mtx_);

  if (!link_ || !active_) {
    bt_log(TRACE, "l2cap", "Ignoring security request on inactive channel");
    return;
  }

  ZX_DEBUG_ASSERT(dispatcher_);
  async::PostTask(
      link_->dispatcher(), [link = link_, level, callback = std::move(callback),
                            dispatcher = dispatcher_]() mutable {
        link->UpgradeSecurity(level, std::move(callback), dispatcher);
      });
}

void ChannelImpl::OnClosed() {
  async_dispatcher_t* dispatcher;
  fit::closure task;

  {
    std::lock_guard<std::mutex> lock(mtx_);

    if (!link_ || !active_) {
      link_ = nullptr;
      return;
    }

    ZX_DEBUG_ASSERT(closed_cb_);
    dispatcher = dispatcher_;
    task = std::move(closed_cb_);
    active_ = false;
    link_ = nullptr;
    dispatcher_ = nullptr;
  }

  RunOrPost(std::move(task), dispatcher);
}

void ChannelImpl::HandleRxPdu(PDU&& pdu) {
  async_dispatcher_t* dispatcher;
  fit::closure task;

  {
    std::lock_guard<std::mutex> lock(mtx_);

    // This will only be called on a live link.
    ZX_DEBUG_ASSERT(link_);
    ZX_DEBUG_ASSERT(rx_engine_);

    common::ByteBufferPtr sdu = rx_engine_->ProcessPdu(std::move(pdu));
    if (!sdu) {
      // The PDU may have been invalid, out-of-sequence, or part of a segmented
      // SDU.
      // * If invalid, we drop the PDU (per Core Spec Ver 5, Vol 3, Part A,
      //   Secs. 3.3.6 and/or 3.3.7).
      // * If out-of-sequence or part of a segmented SDU, we expect that some
      //   later call to ProcessPdu() will return us an SDU containing this
      //   PDU's data.
      return;
    }

    // Buffer the packets if the channel hasn't been activated.
    if (!active_) {
      pending_rx_sdus_.emplace(std::move(sdu));
      return;
    }

    dispatcher = dispatcher_;
    task = [func = rx_cb_.share(), sdu = std::move(sdu)]() mutable {
      func(std::move(sdu));
    };

    ZX_DEBUG_ASSERT(rx_cb_);
  }
  RunOrPost(std::move(task), dispatcher);
}

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
