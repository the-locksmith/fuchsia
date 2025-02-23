// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_MPEG12_DECODER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_MPEG12_DECODER_H_

#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>

#include <thread>
#include <vector>

#include "registers.h"
#include "video_decoder.h"

class Mpeg12Decoder : public VideoDecoder {
 public:
  Mpeg12Decoder(Owner* owner) : owner_(owner) {}

  ~Mpeg12Decoder() override;

  zx_status_t Initialize() override;
  void HandleInterrupt() override;
  void SetFrameReadyNotifier(FrameReadyNotifier notifier) override;
  void ReturnFrame(std::shared_ptr<VideoFrame> video_frame) override;
  void InitializedFrames(std::vector<CodecFrame> frames, uint32_t width,
                         uint32_t height, uint32_t stride) override;

 private:
  struct ReferenceFrame {
    std::shared_ptr<VideoFrame> frame;
    std::unique_ptr<CanvasEntry> y_canvas;
    std::unique_ptr<CanvasEntry> uv_canvas;
  };
  zx_status_t InitializeVideoBuffers();
  void ResetHardware();
  void TryReturnFrames();

  Owner* owner_;

  FrameReadyNotifier notifier_;
  std::vector<ReferenceFrame> video_frames_;
  std::vector<std::shared_ptr<VideoFrame>> returned_frames_;
  io_buffer_t workspace_buffer_ = {};
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_MPEG12_DECODER_H_
