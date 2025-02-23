// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTING_FAKE_LOGGER_H_
#define GARNET_BIN_COBALT_TESTING_FAKE_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>

namespace cobalt {

enum LogMethod {
  kOther = 0,
  kLogEvent = 1,
  kLogEventCount = 2,
  kLogElapsedTime = 3,
  kLogFrameRate = 4,
  kLogMemoryUsage = 5,
  kLogString = 6,
  kLogCustomEvent = 7,
};

class FakeLogger_Sync : public fuchsia::cobalt::Logger_Sync {
 public:
  zx_status_t LogEvent(uint32_t metric_id, uint32_t event_code,
                       fuchsia::cobalt::Status* out_status) override;
  zx_status_t LogEventCount(uint32_t metric_id, uint32_t event_code,
                            ::std::string component,
                            int64_t period_duration_micros, int64_t count,
                            fuchsia::cobalt::Status* out_status) override;
  zx_status_t LogElapsedTime(uint32_t metric_id, uint32_t event_code,
                             ::std::string component, int64_t elapsed_micros,
                             fuchsia::cobalt::Status* out_status) override;
  zx_status_t LogFrameRate(uint32_t metric_id, uint32_t event_code,
                           ::std::string component, float fps,
                           fuchsia::cobalt::Status* out_status) override;
  zx_status_t LogMemoryUsage(uint32_t metric_id, uint32_t event_code,
                             ::std::string component, int64_t bytes,
                             fuchsia::cobalt::Status* out_status) override;
  zx_status_t LogString(uint32_t metric_id, ::std::string s,
                        fuchsia::cobalt::Status* out_status) override;
  zx_status_t StartTimer(uint32_t metric_id, uint32_t event_code,
                         ::std::string component, ::std::string timer_id,
                         uint64_t timestamp, uint32_t timeout_s,
                         fuchsia::cobalt::Status* out_status) override;
  zx_status_t EndTimer(::std::string timer_id, uint64_t timestamp,
                       uint32_t timeout_s,
                       fuchsia::cobalt::Status* out_status) override;
  zx_status_t LogIntHistogram(
      uint32_t metric_id, uint32_t event_code, ::std::string component,
      ::std::vector<fuchsia::cobalt::HistogramBucket> histogram,
      fuchsia::cobalt::Status* out_status) override;
  zx_status_t LogCustomEvent(
      uint32_t metric_id,
      ::std::vector<fuchsia::cobalt::CustomEventValue> event_values,
      fuchsia::cobalt::Status* out_status) override;
  zx_status_t LogCobaltEvent(fuchsia::cobalt::CobaltEvent event,
                             fuchsia::cobalt::Status* out_status) override;
  zx_status_t LogCobaltEvents(
      ::std::vector<fuchsia::cobalt::CobaltEvent> events,
      fuchsia::cobalt::Status* out_status) override;

  uint32_t last_metric_id() { return last_metric_id_; }

  void reset_last_metric_id() { last_metric_id_ = -1; }

  uint32_t last_event_code() { return last_event_code_; }

  void reset_last_event_code() { last_event_code_ = -1; }

  LogMethod last_log_method_invoked() { return last_log_method_invoked_; }

  void reset_last_log_method_invoked() { last_log_method_invoked_ = kOther; }

  size_t call_count() { return call_count_; }

  void reset_call_count() { call_count_ = 0; }

  void reset() {
    reset_last_metric_id();
    reset_last_event_code();
    reset_last_log_method_invoked();
    reset_call_count();
  }

 private:
  uint32_t last_metric_id_ = -1;
  uint32_t last_event_code_ = -1;
  LogMethod last_log_method_invoked_ = kOther;
  size_t call_count_ = 0;
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTING_FAKE_LOGGER_H_