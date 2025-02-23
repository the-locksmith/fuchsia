// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/sketchy/client/canvas.h"

namespace sketchy_lib {

Canvas::Canvas(component::StartupContext* context, async::Loop* loop)
    : Canvas(
          context
              ->ConnectToEnvironmentService<::fuchsia::ui::sketchy::Canvas>(),
          loop) {}

Canvas::Canvas(::fuchsia::ui::sketchy::CanvasPtr canvas, async::Loop* loop)
    : canvas_(std::move(canvas)), loop_(loop), next_resource_id_(1) {
  canvas_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(INFO) << "sketchy_lib::Canvas: lost connection to "
                     "::fuchsia::ui::sketchy::Canvas.";
    loop_->Quit();
  });
}

ResourceId Canvas::AllocateResourceId() { return next_resource_id_++; }

void Canvas::Present(uint64_t time, scenic::Session::PresentCallback callback) {
  if (!commands_.empty()) {
    canvas_->Enqueue(std::move(commands_));

    // After being moved, |commands_| is in a "valid but unspecified state";
    // see http://en.cppreference.com/w/cpp/utility/move.  Calling reset() makes
    // it safe to continue using.
    commands_.clear();
  }
  canvas_->Present(time, std::move(callback));
}

}  // namespace sketchy_lib
