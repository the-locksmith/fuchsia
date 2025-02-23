// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/step_mode.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "garnet/bin/zxdb/symbols/file_line.h"

namespace zxdb {

class AddressRanges;
class FinishThreadController;
class Frame;
class StepThreadController;

// This controller causes the thread to single-step as long as the CPU is in
// a given address range or any stack frame called from it. Contrast with
// the StepThreadController which does not do the sub-frames.
//
// This class works by:
//   1. Single-stepping in the range.
//   2. When the range is exited, see if the address is in a sub-frame.
//   3. Step out of the sub-frame if so, exit if not.
//   4. Repeat.
class StepOverThreadController : public ThreadController {
 public:
  // Constructor for kSourceLine and kInstruction modes. It will initialize
  // itself to the thread's current position when the thread is attached.
  explicit StepOverThreadController(StepMode mode);

  // Constructor for a kAddressRange mode (the mode is implicit). Continues
  // execution as long as the IP is in range.
  explicit StepOverThreadController(AddressRanges range);

  ~StepOverThreadController() override;

  // Sets a callback that the caller can use to control whether excecution
  // stops in a given subframe. The subframe will be one called directly from
  // the code range being stopped over.
  //
  // This allows implementation of operations like
  // "step until you get to a function". When the callback returns true, the
  // "step over" operation will complete at the current location (this will
  // then destroy the controller and indirectly the callback object).
  //
  // When empty (the default), all subframes will be continued.
  void set_subframe_should_stop_callback(std::function<bool(const Frame*)> cb) {
    subframe_should_stop_callback_ = std::move(cb);
  }

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Step Over"; }

 private:
  StepMode step_mode_;

  // When non-null indicates callback to check for stopping in subframes. See
  // the setter above.
  std::function<bool(const Frame*)> subframe_should_stop_callback_;

  // When construction_mode_ == kSourceLine, this represents the line
  // information of the line we're stepping over.
  FileLine file_line_;

  // The fingerprint of the frame we're stepping in. Anything newer than this
  // is a child frame we should step through, and anything older than this
  // means we exited the function and should stop stepping.
  FrameFingerprint frame_fingerprint_;

  // Always non-null, manages stepping in the original function.
  std::unique_ptr<StepThreadController> step_into_;

  // Only set when we're stepping out to get back to the original function.
  std::unique_ptr<FinishThreadController> finish_;
};

}  // namespace zxdb
