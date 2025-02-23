// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_BASE_VIEW_CPP_V1_BASE_VIEW_H_
#define LIB_UI_BASE_VIEW_CPP_V1_BASE_VIEW_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/svc/cpp/service_namespace.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/zx/eventpair.h>
#include <memory>
#include <string>

#include "base_view.h"

namespace scenic {

// Abstract base implementation of a view for simple applications.
//
// Uses the older V1 view system internally and is only here for transition
// purposes.  Deprecated.
class V1BaseView : private ::fuchsia::ui::viewsv1::ViewListener,
                   private ::fuchsia::ui::viewsv1::ViewContainerListener {
 public:
  V1BaseView(ViewContext context, const std::string& debug_name);

  ~V1BaseView() override;

  // Gets the startup context provided to the View.
  component::StartupContext* startup_context() { return startup_context_; }

  // Gets the view manager.
  ::fuchsia::ui::viewsv1::ViewManager* view_manager() {
    return view_manager_.get();
  }

  // Gets the underlying view interface.
  ::fuchsia::ui::viewsv1::View* view() { return view_.get(); }

  // Gets the service provider for the view.
  fuchsia::sys::ServiceProvider* GetViewServiceProvider();

  // Gets the underlying view container interface.
  ::fuchsia::ui::viewsv1::ViewContainer* GetViewContainer();

  // Gets a wrapper for the view's session.
  Session* session() { return &session_; }

  // Gets the imported parent node to which the session's tree of nodes
  // should be attached.
  ImportNode& parent_node() { return parent_node_; }

  // Gets the current view properties.
  // Returns nullptr if unknown.
  const ::fuchsia::ui::viewsv1::ViewProperties& properties() const {
    return properties_;
  }

  // Returns true if the view has a non-empty size in logical pixels.
  bool has_logical_size() const {
    return logical_size_.width > 0.f && logical_size_.height > 0.f;
  }

  // Gets the size of the view in logical pixels.
  // This value is zero until the view receives a layout from its parent.
  const fuchsia::math::SizeF& logical_size() const { return logical_size_; }

  // Returns true if the view has a non-empty size in physical pixels.
  bool has_physical_size() const {
    return physical_size_.width > 0 && physical_size_.height > 0;
  }

  // Gets the size of the view in physical pixels.
  // This value is zero until the view receives a layout from its parent
  // and metrics from its session.
  const fuchsia::math::Size& physical_size() const { return physical_size_; }

  // When true, the session provided metrics are adjusted such that the
  // X and Y scale factors are made equal before computing the physical size.
  // The default is false in which case the X and Y scale factors may differ.
  bool need_square_metrics() const { return need_square_metrics_; }
  void SetNeedSquareMetrics(bool enable);

  // Returns true if the view has received metrics from its session.
  bool has_metrics() const {
    return adjusted_metrics_.scale_x > 0.f && adjusted_metrics_.scale_y > 0.f &&
           adjusted_metrics_.scale_z > 0.f;
  }

  // Gets the view's metrics.
  // This value is zero until the view receives metrics from its session.
  const fuchsia::ui::gfx::Metrics& metrics() const { return adjusted_metrics_; }

  // Sets a callback which is invoked when the view's owner releases the
  // view causing the view manager to unregister it.
  //
  // This should be used to implement cleanup policies to release resources
  // associated with the view (including the object itself).
  void SetReleaseHandler(fit::function<void(zx_status_t)> callback);

  // Invalidates the scene, causing |OnSceneInvalidated()| to be invoked
  // during the next frame.
  void InvalidateScene();

  // Called when the view's properties have changed.
  //
  // The subclass should compare the old and new properties and make note of
  // whether these property changes will affect the layout or content of
  // the view then update accordingly.
  //
  // The default implementation does nothing.
  virtual void OnPropertiesChanged(
      ::fuchsia::ui::viewsv1::ViewProperties old_properties);

  // Called when it's time for the view to update its scene contents due to
  // invalidation.  The new contents are presented once this function returns.
  //
  // The default implementation does nothing.
  virtual void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info);

  // Called when session events are received.
  //
  // The default implementation does nothing.
  virtual void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events);

  // Called to handle an input event.
  // Returns true if the view will handle the event, false if the event
  // should continue propagating to other views which may handle it themselves.
  //
  // The default implementation returns false.
  virtual bool OnInputEvent(fuchsia::ui::input::InputEvent event);

  // Called when a child is attached.
  //
  // The default implementation does nothing.
  virtual void OnChildAttached(
      uint32_t child_key, ::fuchsia::ui::viewsv1::ViewInfo child_view_info);

  // Called when a child becomes unavailable.
  //
  // The default implementation does nothing.
  virtual void OnChildUnavailable(uint32_t child_key);

 protected:
  fuchsia::sys::ServiceProviderPtr& incoming_services() {
    return incoming_services_;
  }

  component::ServiceNamespace& outgoing_services() {
    return outgoing_services_;
  }

 private:
  // |ViewListener|:
  void OnPropertiesChanged(::fuchsia::ui::viewsv1::ViewProperties properties,
                           OnPropertiesChangedCallback callback) override;

  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       ::fuchsia::ui::viewsv1::ViewInfo child_view_info,
                       OnChildAttachedCallback callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          OnChildUnavailableCallback callback) override;

  void PresentScene(zx_time_t presentation_time);
  void HandleSessionEvents(std::vector<fuchsia::ui::scenic::Event> events);
  void AdjustMetricsAndPhysicalSize();

  component::StartupContext* const startup_context_;

  ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager_;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewListener> view_listener_binding_;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewContainerListener>
      view_container_listener_binding_;
  fuchsia::sys::ServiceProviderPtr incoming_services_;
  component::ServiceNamespace outgoing_services_;

  ::fuchsia::ui::viewsv1::ViewPtr view_;
  fuchsia::sys::ServiceProviderPtr view_service_provider_;
  ::fuchsia::ui::viewsv1::ViewContainerPtr view_container_;
  ::fuchsia::ui::viewsv1::ViewProperties properties_;
  fuchsia::math::SizeF logical_size_;
  fuchsia::math::Size physical_size_;
  bool need_square_metrics_ = false;
  fuchsia::ui::gfx::Metrics original_metrics_;
  fuchsia::ui::gfx::Metrics adjusted_metrics_;
  Session session_;
  ImportNode parent_node_;

  bool invalidate_pending_ = false;
  bool present_pending_ = false;

  zx_time_t last_presentation_time_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(V1BaseView);
};

}  // namespace scenic

#endif  // LIB_UI_BASE_VIEW_CPP_V1_BASE_VIEW_H_
