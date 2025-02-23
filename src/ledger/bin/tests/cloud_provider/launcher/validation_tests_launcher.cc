// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/cloud_provider/launcher/validation_tests_launcher.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/fxl/logging.h>

namespace cloud_provider {

namespace {
constexpr char kValidationTestsUrl[] =
    "fuchsia-pkg://fuchsia.com/ledger_tests"
    "#meta/cloud_provider_validation_tests.cmx";
}  // namespace

ValidationTestsLauncher::ValidationTestsLauncher(
    component::StartupContext* startup_context,
    std::function<
        void(fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider>)>
        factory)
    : startup_context_(startup_context), factory_(std::move(factory)) {
  service_provider_impl_.AddService<fuchsia::ledger::cloud::CloudProvider>(
      [this](fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider>
                 request) { factory_(std::move(request)); });
}

void ValidationTestsLauncher::Run(const std::vector<std::string>& arguments,
                                  std::function<void(int32_t)> callback) {
  callback_ = std::move(callback);
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kValidationTestsUrl;
  fuchsia::sys::ServiceList service_list;
  service_list.names.push_back(fuchsia::ledger::cloud::CloudProvider::Name_);
  service_provider_impl_.AddBinding(service_list.provider.NewRequest());
  launch_info.additional_services = fidl::MakeOptional(std::move(service_list));
  for (const auto& argument : arguments) {
    launch_info.arguments.push_back(argument);
  }

  startup_context_->launcher()->CreateComponent(
      std::move(launch_info), validation_tests_controller_.NewRequest());

  validation_tests_controller_.events().OnTerminated =
      [this](int32_t return_code, fuchsia::sys::TerminationReason reason) {
        callback_(return_code);
      };
  validation_tests_controller_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Lost connection to validation tests binary.";
    callback_(-1);
  });
}

}  // namespace cloud_provider
