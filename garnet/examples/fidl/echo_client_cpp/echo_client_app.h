// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_FIDL_ECHO_CLIENT_CPP_ECHO_CLIENT_APP_H_
#define GARNET_EXAMPLES_FIDL_ECHO_CLIENT_CPP_ECHO_CLIENT_APP_H_

#include <fidl/examples/echo/cpp/fidl.h>

#include "lib/sys/cpp/startup_context.h"

namespace echo {

class EchoClientApp {
 public:
  EchoClientApp();
  EchoClientApp(std::unique_ptr<sys::StartupContext> context);

  fidl::examples::echo::EchoPtr& echo() { return echo_; }

  void Start(std::string server_url);

 private:
  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

  std::unique_ptr<sys::StartupContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fidl::examples::echo::EchoPtr echo_;
};

}  // namespace echo

#endif  // GARNET_EXAMPLES_FIDL_ECHO_CLIENT_CPP_ECHO_CLIENT_APP_H_
