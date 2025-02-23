// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_INTERNAL_VIEW_INSPECTOR_H_
#define GARNET_BIN_UI_VIEW_MANAGER_INTERNAL_VIEW_INSPECTOR_H_

#include <vector>

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/fit/function.h>

namespace view_manager {
class InputConnectionImpl;
class InputDispatcherImpl;

// |FocusChain| defines the chain that a keyboard input event will follow.
struct FocusChain {
 public:
  // |version| of the focus chain.
  uint64_t version;

  // |chain| is the ordered list of views that a keyboard event will propagate
  std::vector<uint32_t /* view token */> chain;
};

// Provides information about a view which was hit during a hit tests.
struct ViewHit {
  // The view which was hit.
  uint32_t view_token;

  // The origin of the ray that was used for the hit test, in device
  // coordinates.
  fuchsia::math::Point3F ray_origin;

  // The direction of the ray that was used for the hit test, in device
  // coordinates.
  fuchsia::math::Point3F ray_direction;

  // The distance along the ray that was passed in to the hit test, in the
  // coordinate system of the view.
  float distance;

  // Transforms the view tree coordinate system to the view's coordinate system.
  fuchsia::math::Transform inverse_transform;
};

// Provides a view associate with the ability to inspect and perform operations
// on the contents of views and view trees.
class ViewInspector {
 public:
  using HitTestCallback = fit::function<void(fidl::VectorPtr<ViewHit>)>;

  using OnEventDelivered = fit::function<void(bool handled)>;

  virtual ~ViewInspector() {}

  // Performs a hit test using a vector with the provided ray, and returns the
  // list of views which were hit.
  virtual void HitTest(::fuchsia::ui::viewsv1::ViewTreeToken view_tree_token,
                       const fuchsia::math::Point3F& ray_origin,
                       const fuchsia::math::Point3F& ray_direction,
                       HitTestCallback callback) = 0;

  // Retrieve the IME Service that is the closest to the ViewToken
  // in the associated ViewTree
  virtual void GetImeService(
      uint32_t view_token,
      fidl::InterfaceRequest<fuchsia::ui::input::ImeService> ime_service) = 0;
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_INTERNAL_VIEW_INSPECTOR_H_
