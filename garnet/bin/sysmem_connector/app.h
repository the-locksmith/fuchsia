// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMEM_CONNECTOR_APP_H_
#define GARNET_BIN_SYSMEM_CONNECTOR_APP_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/macros.h>
#include <lib/sysmem-connector/sysmem-connector.h>

#include <memory>

// sysmem_connector's app
class App {
 public:
  App();
  ~App();

 private:
  std::unique_ptr<component::StartupContext> startup_context_;

  sysmem_connector_t* sysmem_connector_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

#endif  // GARNET_BIN_SYSMEM_CONNECTOR_APP_H_
