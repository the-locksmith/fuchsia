// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/tts/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include <utility>

#include "lib/sys/cpp/startup_context.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace {

class TtsClient {
 public:
  TtsClient(fit::closure quit_callback);
  void Say(std::string words);

 private:
  fit::closure quit_callback_;
  fuchsia::tts::TtsServicePtr tts_service_;
};

TtsClient::TtsClient(fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)) {
  FXL_DCHECK(quit_callback_);
  auto app_ctx = sys::StartupContext::CreateFromStartupInfo();
  tts_service_ =
      app_ctx->svc()->Connect<fuchsia::tts::TtsService>();
  tts_service_.set_error_handler([this](zx_status_t status) {
    printf("Connection error when trying to talk to the TtsService\n");
    quit_callback_();
  });
}

void TtsClient::Say(std::string words) {
  tts_service_->Say(std::move(words), 0, [this](uint64_t token) { quit_callback_(); });
}

}  // namespace

int main(int argc, const char** argv) {
  if (argc < 2) {
    printf("usage: %s [words to speak]\n", argv[0]);
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  TtsClient client([&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  });

  std::string words(argv[1]);
  for (int i = 2; i < argc; ++i) {
    words += " ";
    words += argv[i];
  }

  async::PostTask(loop.dispatcher(), [&]() { client.Say(std::move(words)); });

  loop.Run();

  return 0;
}
