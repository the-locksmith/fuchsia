// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_THROTTLE_OUTPUT_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_THROTTLE_OUTPUT_H_

#include "garnet/bin/media/audio_core/standard_output_base.h"

namespace media::audio {

class ThrottleOutput : public StandardOutputBase {
 public:
  static fbl::RefPtr<AudioOutput> Create(AudioDeviceManager* manager);
  ~ThrottleOutput() override;

 protected:
  explicit ThrottleOutput(AudioDeviceManager* manager);

  // AudioOutput Implementation
  void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) override;

  // StandardOutputBase Implementation
  bool StartMixJob(MixJob* job, fxl::TimePoint process_start) override;
  bool FinishMixJob(const MixJob& job) override;

  // AudioDevice implementation.
  // No one should ever be trying to apply gain limits for a throttle output.
  void ApplyGainLimits(::fuchsia::media::AudioGainInfo* in_out_info,
                       uint32_t set_flags) override {
    FXL_DCHECK(false);
  }

 private:
  fxl::TimePoint last_sched_time_;
  bool uninitialized_ = true;
};

}  // namespace media::audio

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_THROTTLE_OUTPUT_H_
