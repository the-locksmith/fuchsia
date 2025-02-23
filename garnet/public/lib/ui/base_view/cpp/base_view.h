// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_BASE_VIEW_CPP_BASE_VIEW_H_
#define LIB_UI_BASE_VIEW_CPP_BASE_VIEW_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/svc/cpp/services.h"
#include "lib/ui/base_view/cpp/embedded_view_utils.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"
#include "lib/zx/eventpair.h"

namespace scenic {

// Parameters for creating a BaseView.
struct ViewContext {
  scenic::SessionPtrAndListenerRequest session_and_listener_request;
  zx::eventpair view_token;
  fuchsia::ui::views::ViewToken view_token2;
  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services;
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services;
  component::StartupContext* startup_context;
};

// Abstract base implementation of a view for simple applications.
// Subclasses must handle layout and provide content for the scene by
// overriding the virtual methods defined in this class.
//
// It is not necessary to use this class to implement all Views.
// This class is merely intended to make the simple apps easier to write.
class BaseView : private fuchsia::ui::scenic::SessionListener {
 public:
  // Subclasses are typically created by ViewProviderService::CreateView(),
  // which provides the necessary args to pass down to this base class.
  BaseView(ViewContext context, const std::string& debug_name);

  BaseView(const BaseView&) = delete;

  const scenic::View& view() const { return view_; }
  Session* session() { return &session_; }
  component::StartupContext* startup_context() { return startup_context_; }

  fuchsia::ui::gfx::ViewProperties view_properties() const {
    return view_properties_;
  }

  const fuchsia::ui::app::ViewConfig& view_config() const {
    return view_config_;
  }

  // Returns true if the view has a non-empty size in logical pixels.
  bool has_logical_size() const {
    auto& sz = logical_size();
    return sz.x > 0.f && sz.y > 0.f && sz.z > 0.f;
  }

  // Gets the size of the view in logical pixels.
  // This value is zero until the view receives a layout from its parent.
  const fuchsia::ui::gfx::vec3& logical_size() const { return logical_size_; }

  // Returns true if the view has a non-empty size in physical pixels.
  bool has_physical_size() const {
    auto& sz = physical_size();
    return sz.x > 0.f && sz.y > 0.f && sz.z > 0.f;
  }

  // Gets the size of the view in physical pixels.
  // This value is zero until the view receives a layout from its parent
  // and metrics from its session.
  const fuchsia::ui::gfx::vec3& physical_size() const {
    // TODO(SCN-809): use logical size for now.  Needs metrics to be provided
    // by Scenic.
    return logical_size();
  }

  // Sets the view's presentation configuration (i18n profile, etc.). If the new
  // |ViewConfig| differs at all from the existing one, |OnConfigChanged()|
  // will be called.
  //
  // This method should be called at least once before the view is displayed.
  void SetConfig(fuchsia::ui::app::ViewConfig view_config);

  // Sets a callback which is invoked when the view's owner releases the
  // view causing the view manager to unregister it.
  //
  // This should be used to implement cleanup policies to release resources
  // associated with the view (including the object itself).
  void SetReleaseHandler(fit::function<void(zx_status_t)> callback);

  // Invalidates the scene, causing |OnSceneInvalidated()| to be invoked
  // during the next frame.
  void InvalidateScene();

  // Called when it's time for the view to update its scene contents due to
  // invalidation.  The new contents are presented once this function returns.
  //
  // The default implementation does nothing.
  virtual void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) {}

  // Called when the view's properties have changed.
  //
  // The subclass should compare the old and new properties and make note of
  // whether these property changes will affect the layout or content of
  // the view then update accordingly.
  //
  // The default implementation does nothing.
  virtual void OnPropertiesChanged(
      fuchsia::ui::gfx::ViewProperties old_properties) {}

  // Called when the view's |ViewConfig| is first set or has changed.
  //
  // This is not called unless there was an actual value change between the old
  // and the new config. The new config can be accessed at |view_config()|.
  //
  // The default implementation does nothing. Subclasses will usually want to
  // call |InvalidateScene()|.
  virtual void OnConfigChanged(fuchsia::ui::app::ViewConfig old_config) {}

  // Called to handle an input event.
  //
  // The default implementation does nothing.
  virtual void OnInputEvent(fuchsia::ui::input::InputEvent event) {}

  // Called when a command sent by the client was not handled by Scenic.
  //
  // The default implementation does nothing.
  virtual void OnUnhandledCommand(fuchsia::ui::scenic::Command unhandled) {}

  // Called when an event that is not handled directly by BaseView is received.
  // For example, BaseView handles fuchsia::ui::gfx::ViewPropertiesChangedEvent,
  // and notifies the subclass via OnPropertiesChanged(); not all events are
  // handled in this way.
  //
  // The default implementation does nothing.
  virtual void OnScenicEvent(fuchsia::ui::scenic::Event) {}

 protected:
  // An alternative way to update the scene. Provide a faster way to cause a
  // present in comparison to InvalidateScene(). Caller should update the
  // scene contents before calling this method.
  void PresentScene();

  fuchsia::sys::ServiceProviderPtr& incoming_services() {
    return incoming_services_;
  }

  component::ServiceNamespace& outgoing_services() {
    return outgoing_services_;
  }

 private:
  // |scenic::SessionListener|
  //
  // Iterates over the received events and either handles them in a sensible way
  // (e.g. fuchsia::ui::gfx::ViewPropertiesChangedEvent is handled by invoking
  // the virtual method OnPropertiesChanged()), or delegates handling to the
  // subclass via the single-event version of OnEvent() above.
  //
  // Subclasses should not override this.
  void OnScenicEvent(::std::vector<fuchsia::ui::scenic::Event> events) override;

  void PresentScene(zx_time_t presentation_time);

  component::StartupContext* const startup_context_;
  fuchsia::sys::ServiceProviderPtr incoming_services_;
  component::ServiceNamespace outgoing_services_;

  fidl::Binding<fuchsia::ui::scenic::SessionListener> listener_binding_;
  Session session_;
  scenic::View view_;

  fuchsia::ui::gfx::vec3 logical_size_;
  fuchsia::ui::gfx::ViewProperties view_properties_;
  fuchsia::ui::app::ViewConfig view_config_;

  zx_time_t last_presentation_time_ = 0;
  size_t session_present_count_ = 0;
  bool invalidate_pending_ = false;
  bool present_pending_ = false;
};

}  // namespace scenic

#endif  // LIB_UI_BASE_VIEW_CPP_BASE_VIEW_H_
