// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/test/fakes/fake_view_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/bin/mediaplayer/test/fakes/fake_scenic.h"
#include "lib/fxl/logging.h"

namespace media_player {
namespace test {

FakeViewManager::FakeViewManager(FakeScenic* fake_scenic)
    : dispatcher_(async_get_default_dispatcher()),
      fake_scenic_(fake_scenic) {}

FakeViewManager::~FakeViewManager() {}

void FakeViewManager::GetScenic(
    fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> request) {
  fake_scenic_->Bind(std::move(request));
}

void FakeViewManager::CreateView(
    fidl::InterfaceRequest<fuchsia::ui::viewsv1::View> view,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> view_owner,
    fidl::InterfaceHandle<fuchsia::ui::viewsv1::ViewListener> view_listener,
    zx::eventpair parent_export_token, fidl::StringPtr label) {
  // "Cast" the ViewOwner channel endpoint to an eventpair endpoint.  Should
  // work for the time being while this interface is being deprecated.
  // TODO(SCN-1018): Remove this along with the interface.
  CreateView2(std::move(view),
              zx::eventpair(view_owner.TakeChannel().release()),
              std::move(view_listener), std::move(parent_export_token), label);
}

void FakeViewManager::CreateView2(
    fidl::InterfaceRequest<fuchsia::ui::viewsv1::View> view,
    zx::eventpair view_token,
    fidl::InterfaceHandle<fuchsia::ui::viewsv1::ViewListener> view_listener,
    zx::eventpair parent_export_token, fidl::StringPtr label) {
  fake_view_.Bind(std::move(view), std::move(view_token), view_listener.Bind(),
                  std::move(parent_export_token), label);

  fuchsia::ui::viewsv1::ViewProperties properties;
  properties.view_layout = fuchsia::ui::viewsv1::ViewLayout::New();
  properties.view_layout->size.width = 1920.0f;
  properties.view_layout->size.height = 1080.0f;
  properties.view_layout->inset.top = 0.0f;
  properties.view_layout->inset.right = 0.0f;
  properties.view_layout->inset.bottom = 0.0f;
  properties.view_layout->inset.left = 0.0f;
  fake_view_.view_listener()->OnPropertiesChanged(std::move(properties),
                                                  []() {});
}

void FakeViewManager::CreateViewTree(
    fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewTree> view_tree,
    fidl::InterfaceHandle<fuchsia::ui::viewsv1::ViewTreeListener>
        view_tree_listener,
    fidl::StringPtr label) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace test
}  // namespace media_player
