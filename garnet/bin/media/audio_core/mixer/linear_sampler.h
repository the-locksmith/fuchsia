// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_LINEAR_SAMPLER_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_LINEAR_SAMPLER_H_

#include <fuchsia/media/cpp/fidl.h>

#include "garnet/bin/media/audio_core/mixer/mixer.h"

namespace media::audio::mixer {

class LinearSampler : public Mixer {
 public:
  static MixerPtr Select(const fuchsia::media::AudioStreamType& src_format,
                         const fuchsia::media::AudioStreamType& dest_format);

 protected:
  LinearSampler(uint32_t pos_filter_width, uint32_t neg_filter_width)
      : Mixer(pos_filter_width, neg_filter_width) {}
};

}  // namespace media::audio::mixer

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_LINEAR_SAMPLER_H_
