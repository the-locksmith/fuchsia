// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_DATA_SOCKET_FACTORY_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_DATA_SOCKET_FACTORY_H_

#include <memory>
#include <unordered_map>

#include <fbl/ref_ptr.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/socket.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"

#include "garnet/drivers/bluetooth/lib/data/socket_channel_relay.h"

namespace btlib::data::internal {

// A SocketFactory vends zx::socket objects that an IPC peer can use to
// communicate with l2cap::Channels.
//
// Over time, the factory may grow more responsibility and intelligence. For
// example, the factory might manage QoS by configuring the number of packets a
// SocketChannelRelay can process before yielding control back to the
// dispatcher.
//
// THREAD-SAFETY: This class is thread-hostile. An instance must be
// created and destroyed on a single thread. Said thread must have a
// single-threaded dispatcher. Failure to follow those rules may cause the
// program to abort.
template <typename ChannelT>
class SocketFactory final {
 public:
  SocketFactory();
  ~SocketFactory();

  // Creates a zx::socket which can be used to read from, and write to,
  // |channel|.
  //
  // |channel| will automatically be Deactivated() when the zx::socket is
  // closed, or the creation thread's dispatcher shuts down.
  //
  // Similarly, the local end corresponding to the returned zx::socket will
  // automatically be closed when |channel| is closed, or the creation thread's
  // dispatcher  shuts down.
  //
  // It is an error to call MakeSocketForChannel() multiple times for
  // the same Channel.
  //
  // Returns the new socket on success, and an invalid socket otherwise.
  zx::socket MakeSocketForChannel(fbl::RefPtr<ChannelT> channel);

 private:
  using RelayT = SocketChannelRelay<ChannelT>;
  using ChannelIdT = typename ChannelT::UniqueId;

  const fxl::ThreadChecker thread_checker_;

  // TODO(NET-1535): Figure out what we need to do handle the possibility that a
  // channel id is recycled. (See comment in LogicalLink::HandleRxPacket.)
  std::unordered_map<ChannelIdT, std::unique_ptr<RelayT>> channel_to_relay_;

  fxl::WeakPtrFactory<SocketFactory> weak_ptr_factory_;  // Keep last.

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketFactory);
};

}  // namespace btlib::data::internal

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_DATA_SOCKET_FACTORY_H_
