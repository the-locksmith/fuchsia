// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/finish_physical_frame_thread_controller.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/until_thread_controller.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "lib/fxl/logging.h"

namespace zxdb {

FinishPhysicalFrameThreadController::FinishPhysicalFrameThreadController(
    Stack& stack, size_t frame_to_finish)
    : frame_to_finish_(frame_to_finish), weak_factory_(this) {
  FXL_DCHECK(frame_to_finish < stack.size());
  FXL_DCHECK(!stack[frame_to_finish]->IsInline());

#ifndef NDEBUG
  // Stash for validation later.
  frame_ip_ = stack[frame_to_finish]->GetAddress();
#endif
}

FinishPhysicalFrameThreadController::~FinishPhysicalFrameThreadController() =
    default;

FinishPhysicalFrameThreadController::StopOp
FinishPhysicalFrameThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (until_controller_) {
    if (until_controller_->OnThreadStop(stop_type, hit_breakpoints) ==
        kContinue)
      return kContinue;

    // The until controller said to stop. The CPU is now at the address
    // immediately following the function call. The tricky part is that this
    // could be the first instruction of a new inline function following the
    // call and the stack will now contain that inline expansion. Our caller
    // expects to be in the frame that called the function being stepped out
    // of.
    //
    // Rolling ambiguous frames back to "one before" the frame fingerprint
    // being finished might sound right but isn't because that fingerprint
    // won't exist any more (we just exited it).
    //
    // For a frame to be ambiguous the IP must be at the first instruction
    // of a range of that inline. By virtue of just returning from a function
    // call, we know any inline functions that start immediately after the
    // call weren't in the stack of the original call.
    Stack& stack = thread()->GetStack();
    stack.SetHideAmbiguousInlineFrameCount(
        stack.GetAmbiguousInlineFrameCount());
    return kStop;
  }

  // When there's no "until" controller, this controller just said "continue"
  // to step out of the oldest stack frame. Therefore, any stops at this level
  // aren't ours.
  return kContinue;
}

void FinishPhysicalFrameThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);

  Stack& stack = thread->GetStack();

#ifndef NDEBUG
  // The stack must not have changed from construction to this call. There are
  // no async requests that need to happen during this time, just registration
  // with the thread. Otherwise the frame fingerprint computation needs to be
  // scheduled in the constructor which complicates the async states of this
  // function (though it's possible in the future if necessary).
  FXL_DCHECK(stack.size() > frame_to_finish_);
  FXL_DCHECK(stack[frame_to_finish_]->GetAddress() == frame_ip_);
#endif

#ifdef DEBUG_THREAD_CONTROLLERS
  auto function = stack[frame_to_finish_]->GetLocation().symbol().Get()->AsFunction();
  if (function)
    Log("Finishing %s", function->GetFullName().c_str());
  else
    Log("Finshing unsymbolized function");
#endif

  auto found_fingerprint = stack.GetFrameFingerprint(frame_to_finish_);
  if (!found_fingerprint) {
    // This can happen if the creator of this class requested that we finish
    // the bottom-most stack frame available, without having all stack frames
    // available. That's not allowed and any code doing that should be fixed.
    FXL_NOTREACHED();
    cb(Err("Trying to step out of a frame with insufficient context.\n"
           "Please file a bug with a repro."));
    return;
  }
  InitWithFingerprint(*found_fingerprint);
  cb(Err());
}

ThreadController::ContinueOp
FinishPhysicalFrameThreadController::GetContinueOp() {
  // Once this thread starts running, the frame index is invalid.
  frame_to_finish_ = static_cast<size_t>(-1);

  if (until_controller_)
    return until_controller_->GetContinueOp();

  // This will happen when there's no previous frame so there's no address
  // to return to. Unconditionally continue.
  return ContinueOp::Continue();
}

void FinishPhysicalFrameThreadController::InitWithFingerprint(
    FrameFingerprint fingerprint) {
  if (frame_to_finish_ >= thread()->GetStack().size() - 1) {
    // Finishing the last frame. There is no return address so there's no
    // setup necessary to step, just continue.
    return;
  }

  // The address we're returning to is that of the previous frame,
  uint64_t to_addr = thread()->GetStack()[frame_to_finish_ + 1]->GetAddress();
  if (!to_addr)
    return;  // Previous stack frame is null, just continue.

  until_controller_ = std::make_unique<UntilThreadController>(
      InputLocation(to_addr), fingerprint,
      UntilThreadController::kRunUntilOlderFrame);

  // Give the "until" controller a dummy callback and execute the callback
  // ASAP. The until controller executes the callback once it knows that the
  // breakpoint set has been complete (round-trip to the target system).
  //
  // Since we provide an address there's no weirdness with symbols and we don't
  // have to worry about matching 0 locations. If the breakpoint set fails, the
  // caller address is invalid and stepping is impossible so it doesn't matter.
  // We can run faster without waiting for the round-trip, and the IPC will
  // serialize so the breakpoint set happens before the thread resume.
  until_controller_->InitWithThread(thread(), [](const Err&) {});
}

}  // namespace zxdb
