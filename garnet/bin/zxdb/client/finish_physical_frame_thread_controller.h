// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <vector>

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Frame;
class Stack;
class UntilThreadController;

// Thread controller that runs a given physical stack frame to its completion.
// This can finish more than one frame at once, and there could be a
// combination of physical and inline frames being exited from as long as the
// bottom one being finished is a physical frame (it uses a breakpoint that
// requires knowing the return address which does not exist for inline frames).
//
// See FinishThreadController which uses this as a sub-controller to finish
// any frame.
class FinishPhysicalFrameThreadController : public ThreadController {
 public:
  // Finishes the given frame of the stack, leaving control at frame
  // |frame_to_finish + 1] when the controller is complete. The frame at the
  // given index must be a physical frame.
  //
  // The frame_to_finish must have its fingerprint computable. This means that
  // either you're finishing frame 0, or have synced all frames.
  FinishPhysicalFrameThreadController(Stack& stack, size_t frame_to_finish);

  ~FinishPhysicalFrameThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Finish Physical"; }

 private:
  // Called when both the frame fingerprint and the thread are known. Does
  // final initialization.
  void InitWithFingerprint(FrameFingerprint fingerprint);

  // Index of the frame to finish. Invalid after the thread is resumed.
  size_t frame_to_finish_;

#ifndef NDEBUG
  // IP of the frame to step out of. This is a sanity check to make sure the
  // stack didn't change between construction and InitWithThread.
  uint64_t frame_ip_;
#endif

  // Will be non-null when stepping out. During initialization or when stepping
  // out of the earliest stack frame, this can be null.
  std::unique_ptr<UntilThreadController> until_controller_;

  fxl::WeakPtrFactory<FinishPhysicalFrameThreadController> weak_factory_;
};

}  // namespace zxdb
