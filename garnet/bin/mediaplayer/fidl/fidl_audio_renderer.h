// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FIDL_FIDL_AUDIO_RENDERER_H_
#define GARNET_BIN_MEDIAPLAYER_FIDL_FIDL_AUDIO_RENDERER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/mediaplayer/metrics/packet_timing_tracker.h"
#include "garnet/bin/mediaplayer/render/audio_renderer.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media_player {

// AudioRenderer that renders audio via FIDL services.
//
// This class run single-threaded.
class FidlAudioRenderer : public AudioRenderer {
 public:
  static std::shared_ptr<FidlAudioRenderer> Create(
      fuchsia::media::AudioRendererPtr audio_renderer);

  FidlAudioRenderer(fuchsia::media::AudioRendererPtr audio_renderer);

  ~FidlAudioRenderer() override;

  // AudioRenderer implementation.
  const char* label() const override;

  void Dump(std::ostream& os) const override;

  void OnInputConnectionReady(size_t input_index) override;

  void FlushInput(bool hold_frame, size_t input_index,
                  fit::closure callback) override;

  void PutInputPacket(PacketPtr packet, size_t input_index) override;

  const std::vector<std::unique_ptr<StreamTypeSet>>& GetSupportedStreamTypes()
      override {
    return supported_stream_types_;
  }

  void SetStreamType(const StreamType& stream_type) override;

  void Prime(fit::closure callback) override;

  void SetTimelineFunction(media::TimelineFunction timeline_function,
                           fit::closure callback) override;

  void BindGainControl(fidl::InterfaceRequest<fuchsia::media::GainControl>
                           gain_control_request) override;

 protected:
  // Renderer overrides.
  void OnTimelineTransition() override;

 private:
  // Determines if more packets are needed.
  bool NeedMorePackets();

  // Signals current demand via the stage's |RequestInputPacket| if we need
  // more packets. Return value indicates whether an input packet was requested.
  bool SignalCurrentDemand();

  // Converts a pts in |pts_rate_| units to ns.
  int64_t to_ns(int64_t pts) {
    return pts * (media::TimelineRate::NsPerSecond / pts_rate_);
  }

  // Converts a pts in ns to |pts_rate_| units.
  int64_t from_ns(int64_t pts) {
    return pts * (pts_rate_ / media::TimelineRate::NsPerSecond);
  }

  std::vector<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  fuchsia::media::AudioRendererPtr audio_renderer_;
  media::TimelineRate pts_rate_;
  int64_t last_supplied_pts_ns_ = 0;
  int64_t last_departed_pts_ns_ = 0;
  bool input_packet_request_outstanding_ = false;
  fit::closure prime_callback_;
  uint32_t bytes_per_frame_;
  bool flushed_ = true;
  int64_t min_lead_time_ns_ = ZX_MSEC(100);
  async::TaskClosure demand_task_;

  PacketTimingTracker arrivals_;
  PacketTimingTracker departures_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FidlAudioRenderer);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FIDL_FIDL_AUDIO_RENDERER_H_
