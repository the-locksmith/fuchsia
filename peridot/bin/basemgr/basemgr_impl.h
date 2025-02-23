// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_BASEMGR_IMPL_H_
#define PERIDOT_BIN_BASEMGR_BASEMGR_IMPL_H_

#include <memory>

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/devicesettings/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/basemgr/basemgr_settings.h"
#include "peridot/bin/basemgr/cobalt/cobalt.h"
#include "peridot/bin/basemgr/session_shell_settings/session_shell_settings.h"
#include "peridot/bin/basemgr/user_provider_impl.h"
#include "peridot/lib/fidl/clone.h"

namespace modular {

// Basemgr is the parent process of the modular framework, and it is started by
// the sysmgr as part of the boot sequence.
//
// It has several high-level responsibilites:
// 1) Initializes and owns the system's root view and presentation.
// 2) Sets up the interactive flow for user authentication and login.
// 3) Manages the lifecycle of sessions, represented as |sessionmgr| processes.
class BasemgrImpl : fuchsia::modular::BaseShellContext,
                    fuchsia::auth::AuthenticationContextProvider,
                    fuchsia::modular::internal::BasemgrDebug,
                    fuchsia::ui::policy::KeyboardCaptureListenerHACK,
                    modular::UserProviderImpl::Delegate {
 public:
  // Initializes as BasemgrImpl instance with the given parameters:
  //
  // |settings| Settings that are parsed from command line. Used to configure
  // the modular framework environment.
  // |session_shell_settings| Settings relevant to session shells. Used to
  // configure session shells that are launched.
  // |launcher| Environment service for creating component instances.
  // |presenter| Service to initialize the presentation.
  // |device_settings_manager| Service to look-up whether device needs factory
  // reset.
  // |on_shutdown| Callback invoked when this basemgr instance is shutdown.
  explicit BasemgrImpl(
      const modular::BasemgrSettings& settings,
      const std::vector<modular::SessionShellSettings>& session_shell_settings,
      fuchsia::sys::Launcher* const launcher,
      fuchsia::ui::policy::PresenterPtr presenter,
      fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager,
      std::function<void()> on_shutdown);

  ~BasemgrImpl() override;

  void Connect(
      fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request);

 private:
  void InitializePresentation(
      fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> view_owner);

  void StartBaseShell();

  FuturePtr<> StopBaseShell();

  FuturePtr<> StopTokenManagerFactoryApp();

  void Start();

  // |fuchsia::modular::BaseShellContext|
  void GetUserProvider(
      fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) override;

  // |fuchsia::modular::BaseShellContext|
  void Shutdown() override;

  // |AuthenticationContextProvider|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request)
      override;

  // |UserProviderImpl::Delegate|
  void DidLogin() override;

  // |UserProviderImpl::Delegate|
  void DidLogout() override;

  // |UserProviderImpl::Delegate|
  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
      GetSessionShellViewOwner(
          fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>)
          override;

  // |UserProviderImpl::Delegate|
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>
      GetSessionShellServiceProvider(
          fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) override;

  // |KeyboardCaptureListenerHACK|
  void OnEvent(fuchsia::ui::input::KeyboardEvent event) override;

  void AddGlobalKeyboardShortcuts(
      fuchsia::ui::policy::PresentationPtr& presentation);

  void UpdatePresentation(const SessionShellSettings& settings);

  void SwapSessionShell();

  void SetNextShadowTechnique();

  void SetShadowTechnique(fuchsia::ui::gfx::ShadowTechnique shadow_technique);

  void ToggleClipping();

  void ShowSetupOrLogin();

  // Updates the session shell app config to the active session shell. Done once
  // on initialization and every time the session shells are swapped.
  void UpdateSessionShellConfig();

  // |BasemgrDebug|
  void RestartSession(RestartSessionCallback on_restart_complete) override;

  // |BasemgrDebug|
  void LoginAsGuest() override;

  const modular::BasemgrSettings& settings_;  // Not owned nor copied.
  const std::vector<SessionShellSettings>& session_shell_settings_;
  fuchsia::modular::AppConfig session_shell_config_;
  // Used to indicate which settings in |session_shell_settings_| is currently
  // active.
  std::vector<SessionShellSettings>::size_type
      active_session_shell_settings_index_{};

  // Used to launch component instances, such as the base shell.
  fuchsia::sys::Launcher* const launcher_;  // Not owned.
  // Used to initialize the presentation.
  fuchsia::ui::policy::PresenterPtr presenter_;
  // Used to look-up whether device needs a factory reset.
  fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager_;
  std::function<void()> on_shutdown_;

  AsyncHolder<UserProviderImpl> user_provider_impl_;

  fidl::BindingSet<fuchsia::modular::internal::BasemgrDebug> basemgr_bindings_;
  fidl::Binding<fuchsia::modular::BaseShellContext> base_shell_context_binding_;
  fidl::Binding<fuchsia::auth::AuthenticationContextProvider>
      authentication_context_provider_binding_;

  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>>
      token_manager_factory_app_;
  fuchsia::auth::TokenManagerFactoryPtr token_manager_factory_;

  bool base_shell_running_{};
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> base_shell_app_;
  fuchsia::modular::BaseShellPtr base_shell_;

  fidl::BindingSet<fuchsia::ui::policy::KeyboardCaptureListenerHACK>
      keyboard_capture_listener_bindings_;

  fuchsia::ui::viewsv1token::ViewOwnerPtr session_shell_view_owner_;

  struct {
    fuchsia::ui::policy::PresentationPtr presentation;
    fidl::BindingSet<fuchsia::ui::policy::Presentation> bindings;

    fuchsia::ui::gfx::ShadowTechnique shadow_technique =
        fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED;
    bool clipping_enabled{};
  } presentation_state_;

  component::ServiceNamespace service_namespace_;

  enum class State {
    // normal mode of operation
    RUNNING,
    // basemgr is shutting down.
    SHUTTING_DOWN
  };

  State state_ = State::RUNNING;

  FXL_DISALLOW_COPY_AND_ASSIGN(BasemgrImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_BASEMGR_IMPL_H_
