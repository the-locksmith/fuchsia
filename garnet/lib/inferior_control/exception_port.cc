// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "exception_port.h"

#include <string>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include "lib/fsl/handles/object_info.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"

#include "thread.h"

namespace inferior_control {

ExceptionPort::ExceptionPort(async_dispatcher_t* dispatcher,
                             PacketCallback exception_callback,
                             PacketCallback signal_callback)
    : keep_running_(false),
      origin_dispatcher_(dispatcher),
      exception_callback_(std::move(exception_callback)),
      signal_callback_(std::move(signal_callback)) {
  FXL_DCHECK(origin_dispatcher_);
  FXL_DCHECK(exception_callback_);
  FXL_DCHECK(signal_callback_);
}

ExceptionPort::~ExceptionPort() {
  if (eport_)
    Quit();
}

bool ExceptionPort::Run() {
  FXL_DCHECK(!eport_);
  FXL_DCHECK(!keep_running_);

  // Create the port used to bind to the inferior's exception port.
  // TODO(dje): We can use a provided async loop once ports are no longer
  // used to bind to exception ports.
  zx_status_t status = zx::port::create(0, &eport_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create the exception port: "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  FXL_DCHECK(eport_);

  keep_running_ = true;
  port_thread_ = std::thread(fit::bind_member(this, &ExceptionPort::Worker));

  return true;
}

void ExceptionPort::Quit() {
  FXL_DCHECK(eport_);
  FXL_DCHECK(keep_running_);

  FXL_LOG(INFO) << "Quitting exception port loop";

  keep_running_ = false;

  // This is called from a different thread than |port_thread_|.
  // Send it a packet waking it up. It will notice |keep_running_ == false|
  // and exit.
  zx_port_packet_t packet{};
  // We don't use USER packets for anything else yet.
  packet.type = ZX_PKT_TYPE_USER;
  eport_.queue(&packet);

  port_thread_.join();

  FXL_LOG(INFO) << "Exception port loop exited";
}

bool ExceptionPort::Bind(zx_handle_t process, Key key) {
  FXL_DCHECK(process != ZX_HANDLE_INVALID);
  FXL_DCHECK(key != 0);
  FXL_DCHECK(eport_);

  zx_koid_t pid = debugger_utils::GetKoid(process);

  zx_status_t status =
    zx_task_bind_exception_port(process, eport_.get(), key,
                                ZX_EXCEPTION_PORT_DEBUGGER);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to bind exception port to process " << pid
                   << ": " << debugger_utils::ZxErrorString(status);
    return false;
  }

  // Also watch for process terminated signals.
  status = zx_object_wait_async(process, eport_.get(), key,
                                ZX_TASK_TERMINATED, ZX_WAIT_ASYNC_ONCE);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to async wait for process " << pid << ": "
                   << debugger_utils::ZxErrorString(status);
    bool success = Unbind(process, key);
    FXL_DCHECK(success);
    return false;
  }

  FXL_VLOG(1) << "Exception port bound to process " << pid
              << " with key " << key;
  return true;
}

bool ExceptionPort::Unbind(zx_handle_t process, Key key) {
  zx_status_t status =
    zx_task_bind_exception_port(process, ZX_HANDLE_INVALID, key,
                                ZX_EXCEPTION_PORT_DEBUGGER);
  if (status != ZX_OK) {
    zx_koid_t pid = debugger_utils::GetKoid(process);
    FXL_LOG(ERROR) << "Unable to unbind process " << pid << ": "
                   << debugger_utils::ZxErrorString(status);
  }
  return status == ZX_OK;
}

void ExceptionPort::Worker() {
  FXL_DCHECK(eport_);

  // Give this thread an identifiable name for debugging purposes.
  fsl::SetCurrentThreadName("exception port reader");

  FXL_VLOG(1) << "Exception port thread started";

  while (keep_running_) {
    zx_port_packet_t packet;
    zx_status_t status = eport_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx_port_wait returned error: "
                     << debugger_utils::ZxErrorString(status);
      // We're no longer running, record it.
      keep_running_ = false;
      break;
    }

    if (ZX_PKT_IS_EXCEPTION(packet.type)) {
      FXL_VLOG(2) << "Received exception: "
                  << debugger_utils::ExceptionName(
                         static_cast<const zx_excp_type_t>(packet.type))
                  << " (" << packet.type << "), key=" << packet.key
                  << " pid=" << packet.exception.pid
                  << " tid=" << packet.exception.tid;
      // Handle the exception on the main thread.
      async::PostTask(origin_dispatcher_, [packet, this] {
        exception_callback_(packet);
      });
    } else if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE) {
      FXL_VLOG(2) << "Received signal:"
                  << " key=" << packet.key
                  << " trigger=0x" << std::hex << packet.signal.trigger
                  << " observed=0x" << std::hex << packet.signal.observed;
      // Handle the signal on the main thread.
      async::PostTask(origin_dispatcher_, [packet, this] {
        signal_callback_(packet);
      });
    } else if (packet.type == ZX_PKT_TYPE_USER) {
      // Sent to wake up the port wait because we're exiting.
      FXL_VLOG(2) << "Received user packet";
      FXL_DCHECK(!keep_running_);
    } else {
      FXL_LOG(WARNING) << "Received unexpected packet: type="
                       << packet.type << ", key=" << packet.key;
    }
  }

  eport_.reset();
}

}  // namespace inferior_control
