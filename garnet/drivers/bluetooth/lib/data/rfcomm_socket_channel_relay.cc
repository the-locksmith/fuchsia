// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/rfcomm/channel.h"

#include "garnet/drivers/bluetooth/lib/data/socket_channel_relay.cc"

namespace btlib {
namespace data {
namespace internal {

template class SocketChannelRelay<rfcomm::Channel>;

}  // namespace internal
}  // namespace data
}  // namespace btlib
