// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/logging.h>
#include <lib/syslog/cpp/logger.h>

#include "garnet/bin/a11y/a11y_manager/semantic_tree.h"

// DEPRECATED

namespace a11y_manager {
const std::string kNewLine = "\n";
const std::string kFourSpace = "    ";
const int kRootNode = 0;

// Internal helper function to check if a point is within a bounding box.
bool BoxContainsPoint(fuchsia::ui::gfx::BoundingBox& box, escher::vec2& point) {
  return box.min.x <= point.x && box.max.x >= point.x && box.min.y <= point.y &&
         box.max.y >= point.y;
}

void SemanticTree::AddBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::SemanticsRoot> request) {
  bindings_.AddBinding(this, std::move(request));
}

fuchsia::accessibility::NodePtr SemanticTree::GetHitAccessibilityNode(
    zx_koid_t view_id, fuchsia::math::PointF point) {
  auto it = nodes_.find(view_id);
  if (it == nodes_.end()) {
    return nullptr;
  }
  escher::vec4 coordinate(point.x, point.y, 0, 1);
  auto found_node = HitTest(it->second, 0, coordinate);
  if (found_node == nullptr) {
    return nullptr;
  }
  auto node_ptr = fuchsia::accessibility::Node::New();
  found_node->Clone(node_ptr.get());
  return node_ptr;
}

fuchsia::accessibility::NodePtr SemanticTree::GetAccessibilityNode(
    zx_koid_t view_id, int32_t node_id) {
  auto it = nodes_.find(view_id);
  if (it == nodes_.end()) {
    return nullptr;
  }
  auto node_it = it->second.find(node_id);
  if (node_it == it->second.end()) {
    return nullptr;
  }
  auto node_ptr = fuchsia::accessibility::Node::New();
  node_it->second.Clone(node_ptr.get());
  return node_ptr;
}

void SemanticTree::PerformAccessibilityAction(
    zx_koid_t view_id, int32_t node_id, fuchsia::accessibility::Action action) {
  auto it = providers_.find(view_id);
  if (it == providers_.end()) {
    return;
  }
  it->second->PerformAccessibilityAction(node_id, action);
}

void SemanticTree::RegisterSemanticsProvider(
    zx_koid_t view_id,
    fidl::InterfaceHandle<fuchsia::accessibility::SemanticsProvider> handle) {
  auto it = nodes_.find(view_id);
  if (it != nodes_.end()) {
    return;
  }
  nodes_.emplace(
      view_id,
      std::unordered_map<int32_t /*node_id*/, fuchsia::accessibility::Node>());
  uncommitted_nodes_.emplace(view_id,
                             std::vector<fuchsia::accessibility::Node>());
  uncommitted_deletes_.emplace(view_id, std::vector<int32_t>());
  fuchsia::accessibility::SemanticsProviderPtr provider = handle.Bind();
  provider.set_error_handler([this, view_id](zx_status_t status) {
    FXL_LOG(INFO) << "Semantic provider disconnected with id: " << view_id;
    this->nodes_.erase(view_id);
    this->uncommitted_nodes_.erase(view_id);
    this->uncommitted_deletes_.erase(view_id);
    this->providers_.erase(view_id);
  });
  providers_.emplace(view_id, std::move(provider));
}

void SemanticTree::UpdateSemanticNodes(
    zx_koid_t view_id, std::vector<fuchsia::accessibility::Node> nodes) {
  auto it = uncommitted_nodes_.find(view_id);
  if (it == uncommitted_nodes_.end()) {
    return;
  }
  it->second.insert(it->second.end(), std::make_move_iterator(nodes.begin()),
                    std::make_move_iterator(nodes.end()));
}

void SemanticTree::DeleteSemanticNodes(zx_koid_t view_id,
                                       std::vector<int32_t> node_ids) {
  auto it = uncommitted_deletes_.find(view_id);
  if (it == uncommitted_deletes_.end()) {
    return;
  }
  it->second.insert(it->second.end(), std::make_move_iterator(node_ids.begin()),
                    std::make_move_iterator(node_ids.end()));
}

void SemanticTree::Commit(zx_koid_t view_id) {
  auto nodes_it = nodes_.find(view_id);
  auto u_nodes_it = uncommitted_nodes_.find(view_id);
  auto u_delete_it = uncommitted_deletes_.find(view_id);
  if (nodes_it == nodes_.end() || u_nodes_it == uncommitted_nodes_.end() ||
      u_delete_it == uncommitted_deletes_.end()) {
    return;
  }
  for (auto& u_node : u_nodes_it->second) {
    nodes_it->second[u_node.node_id] = std::move(u_node);
  }
  u_nodes_it->second.clear();

  for (auto& u_delete : u_delete_it->second) {
    nodes_it->second.erase(u_delete);
  }
  u_delete_it->second.clear();
}

// Helper function to traverse semantic tree and create log message.
void SemanticTree::LogSemanticTreeHelper(
    zx_koid_t view_id, fuchsia::accessibility::NodePtr root_node,
    std::string* tree_log, int current_level) {
  if (!root_node) {
    return;
  }

  // Add constant spaces proportional to the current treel level, so that
  // child nodes are indented under parent node.
  for (int level = 0; level <= current_level; level++) {
    *tree_log += kFourSpace;
  }

  // Add logs for the current node.
  *tree_log = *tree_log +
              "Node_id: " + std::to_string(root_node.get()->node_id) +
              ", Label:" + root_node.get()->data.label + kNewLine;

  // Iterate through all the children of the current node.
  for (std::vector<int>::iterator it =
           root_node.get()->children_traversal_order.begin();
       it != root_node.get()->children_traversal_order.end(); ++it) {
    fuchsia::accessibility::NodePtr node_ptr =
        GetAccessibilityNode(view_id, *it);
    LogSemanticTreeHelper(view_id, std::move(node_ptr), tree_log,
                          current_level + 1);
  }
}

std::string SemanticTree::LogSemanticTree(zx_koid_t view_id) {
  // TODO(ankitdave): Modify this function, once FIDL APIs are finalized and
  //                  create a client so that this function can be called from
  //                  command line.

  // Get the root node for given view_id.
  fuchsia::accessibility::NodePtr node_ptr =
      GetAccessibilityNode(view_id, kRootNode);
  if (!node_ptr) {
    FX_LOGS(INFO) << "Root Node not found for view id:" << view_id;
    return "";
  }

  std::string tree_log;
  // Start with the root node(i.e: Node - 0).
  LogSemanticTreeHelper(view_id, std::move(node_ptr), &tree_log, 0);
  FX_LOGS(INFO) << "Semantic Tree for View ID:" << view_id << std::endl
                << tree_log;
  return tree_log;
}

fuchsia::accessibility::Node* SemanticTree::HitTest(
    std::unordered_map<int32_t, fuchsia::accessibility::Node>& nodes,
    int32_t node_id, escher::vec4 coordinates) {
  auto it = nodes.find(node_id);
  if (it == nodes.end()) {
    return nullptr;
  }
  escher::mat4 transform = scenic_impl::gfx::Unwrap(it->second.data.transform);
  escher::vec4 local_coordinates = transform * coordinates;
  escher::vec2 point(local_coordinates[0], local_coordinates[1]);

  if (!BoxContainsPoint(it->second.data.location, point)) {
    return nullptr;
  }
  for (auto child : it->second.children_hit_test_order) {
    fuchsia::accessibility::Node* node =
        HitTest(nodes, child, local_coordinates);
    if (node != nullptr) {
      return node;
    }
  }
  return &(it->second);
}

}  // namespace a11y_manager
