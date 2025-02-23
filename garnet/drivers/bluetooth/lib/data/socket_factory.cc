// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_factory.h"

#include <zircon/assert.h>
#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"

namespace btlib::data::internal {

template <typename ChannelT>
SocketFactory<ChannelT>::SocketFactory() : weak_ptr_factory_(this) {}

template <typename ChannelT>
SocketFactory<ChannelT>::~SocketFactory() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
}

template <typename ChannelT>
zx::socket SocketFactory<ChannelT>::MakeSocketForChannel(
    fbl::RefPtr<ChannelT> channel) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(channel);

  const auto unique_id = channel->unique_id();
  if (channel_to_relay_.find(unique_id) != channel_to_relay_.end()) {
    bt_log(ERROR, "l2cap", "channel %u @ %u is already bound to a socket",
           channel->link_handle(), channel->id());
    return {};
  }

  zx::socket local_socket, remote_socket;
  const auto status =
      zx::socket::create(ZX_SOCKET_DATAGRAM, &local_socket, &remote_socket);
  if (status != ZX_OK) {
    bt_log(ERROR, "l2cap", "Failed to create socket for channel %u @ %u: %s",
           channel->link_handle(), channel->id(), zx_status_get_string(status));
    return {};
  }

  auto relay = std::make_unique<RelayT>(
      std::move(local_socket), channel,
      typename RelayT::DeactivationCallback(
          [self = weak_ptr_factory_.GetWeakPtr(), id = unique_id]() mutable {
            ZX_DEBUG_ASSERT_MSG(self, "(unique_id=%lu)", id);
            size_t n_erased = self->channel_to_relay_.erase(id);
            ZX_DEBUG_ASSERT_MSG(n_erased == 1, "(n_erased=%zu, unique_id=%lu)",
                                n_erased, id);
          }));

  // Note: Activate() may abort, if |channel| has been Activated() without
  // going through this SocketFactory.
  if (!relay->Activate()) {
    bt_log(ERROR, "l2cap", "Failed to Activate() relay for channel %u",
           channel->id());
    return {};
  }

  channel_to_relay_.emplace(unique_id, std::move(relay));
  return remote_socket;
}

}  // namespace btlib::data::internal
