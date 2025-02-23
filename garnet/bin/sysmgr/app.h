// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMGR_APP_H_
#define GARNET_BIN_SYSMGR_APP_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_set>

#include <fs/managed-vfs.h>
#include <fuchsia/pkg/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include "garnet/bin/sysmgr/config.h"
#include "garnet/bin/sysmgr/package_updating_loader.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_namespace.h"
#include "lib/svc/cpp/services.h"

namespace sysmgr {

// The sysmgr creates a nested environment within which it starts apps
// and wires up the UI services they require.
//
// The nested environment consists of the following system applications
// which are started on demand then retained as singletons for the lifetime
// of the environment.
class App {
 public:
  explicit App(Config config);
  ~App();

 private:
  zx::channel OpenAsDirectory();
  void ConnectToService(const std::string& service_name, zx::channel channel);

  void RegisterSingleton(std::string service_name,
                         fuchsia::sys::LaunchInfoPtr launch_info);
  void RegisterLoader();
  void RegisterDefaultServiceConnector();
  void LaunchApplication(fuchsia::sys::LaunchInfo launch_info);

  std::unique_ptr<component::StartupContext> startup_context_;

  // Keep track of all services, indexed by url.
  std::map<std::string, component::Services> services_;

  // Nested environment within which the apps started by sysmgr will run.
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::LauncherPtr env_launcher_;

  fs::ManagedVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> svc_root_;
  fidl::VectorPtr<std::string> svc_names_;

  std::unique_ptr<PackageUpdatingLoader> package_updating_loader_;
  fuchsia::sys::LoaderPtr loader_;
  fidl::BindingSet<fuchsia::sys::Loader> loader_bindings_;

  bool auto_updates_enabled_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace sysmgr

#endif  // GARNET_BIN_SYSMGR_APP_H_
