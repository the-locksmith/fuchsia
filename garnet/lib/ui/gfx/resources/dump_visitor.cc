// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/dump_visitor.h"

#include <ostream>

#include "garnet/lib/ui/gfx/resources/buffer.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/display_compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/image.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/resources/import.h"
#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/lights/point_light.h"
#include "garnet/lib/ui/gfx/resources/material.h"
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/opacity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/scene.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/mesh_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"
#include "lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

DumpVisitor::DumpVisitor(std::ostream& output) : output_(output) {}

DumpVisitor::~DumpVisitor() = default;

void DumpVisitor::Visit(Memory* r) {
  // To prevent address space layout leakage, we don't print the pointers.
  BeginItem("Memory", r);
  WriteProperty("is_host") << r->is_host();
  WriteProperty("size") << r->size();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::VisitEscherImage(escher::Image* i) {
  WriteProperty("width") << i->width();
  WriteProperty("height") << i->height();
  WriteProperty("format") << static_cast<int>(i->format());
  WriteProperty("has_depth") << i->has_depth();
  WriteProperty("has_stencil") << i->has_stencil();
}

void DumpVisitor::Visit(Image* r) {
  BeginItem("Image", r);
  VisitEscherImage(r->GetEscherImage().get());
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Buffer* r) {
  BeginItem("Buffer", r);
  WriteProperty("size") << r->size();
  VisitResource(r);
  BeginSection("memory");
  if (r->backing_resource()) {
    r->backing_resource()->Accept(this);
  }
  EndSection();
  EndItem();
}

void DumpVisitor::Visit(ImagePipe* r) {
  BeginItem("ImagePipe", r);
  if (r->GetEscherImage()) {
    BeginSection("currently presented image");
    VisitEscherImage(r->GetEscherImage().get());
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(View* r) {
  ViewHolder* vh = r->view_holder();
  WriteProperty("view") << r->global_id() << "->"
                        << (vh ? vh->global_id() : GlobalId());
}

void DumpVisitor::Visit(ViewHolder* r) {
  View* v = r->view();
  WriteProperty("view_holder")
      << r->global_id() << "->" << (v ? v->global_id() : GlobalId());
  WriteProperty("focus_change") << r->view_properties().focus_change;
}

void DumpVisitor::Visit(EntityNode* r) {
  BeginItem("EntityNode", r);
  if (r->view()) {
    Visit(r->view());
  }
  if (!r->view_holders().empty()) {
    for (ViewHolderPtr vh : r->view_holders()) {
      Visit(vh.get());
    }
  }
  VisitNode(r);
  EndItem();
}

void DumpVisitor::Visit(OpacityNode* r) {
  BeginItem("OpacityNode", r);
  WriteProperty("opacity") << r->opacity();
  VisitNode(r);
  EndItem();
}

void DumpVisitor::Visit(ShapeNode* r) {
  BeginItem("ShapeNode", r);
  VisitNode(r);
  if (r->shape()) {
    BeginSection("shape");
    r->shape()->Accept(this);
    EndSection();
  }
  if (r->material()) {
    BeginSection("material");
    r->material()->Accept(this);
    EndSection();
  }
  EndItem();
}

void DumpVisitor::Visit(Scene* r) {
  BeginItem("Scene", r);

  const bool has_lights = !r->ambient_lights().empty() ||
                          !r->directional_lights().empty() ||
                          !r->point_lights().empty();
  if (has_lights) {
    BeginSection("lights");
    for (auto& light : r->ambient_lights()) {
      light->Accept(this);
    }
    for (auto& light : r->directional_lights()) {
      light->Accept(this);
    }
    for (auto& light : r->point_lights()) {
      light->Accept(this);
    }
    EndSection();
  }

  VisitNode(r);
  EndItem();
}

void DumpVisitor::VisitNode(Node* r) {
  if (r->tag_value()) {
    WriteProperty("tag_value") << r->tag_value();
  }
  if (r->hit_test_behavior() != ::fuchsia::ui::gfx::HitTestBehavior::kDefault) {
    WriteProperty("hit_test_behavior")
        << static_cast<int>(r->hit_test_behavior());
  }
  if (r->clip_to_self()) {
    WriteProperty("clip_to_self") << r->clip_to_self();
  }
  if (r->is_exported()) {
    WriteProperty("is_exported") << r->is_exported();
  }
  if (r->transform().IsIdentity()) {
    WriteProperty("transform") << "identity";
  } else {
    WriteProperty("transform") << r->transform();
  }
  if (!r->parts().empty()) {
    BeginSection("parts");
    for (auto& part : r->parts()) {
      part->Accept(this);
    }
    EndSection();
  }
  if (!r->children().empty()) {
    BeginSection("children");
    for (auto& child : r->children()) {
      child->Accept(this);
    }
    EndSection();
  }
  VisitResource(r);
}

void DumpVisitor::Visit(CircleShape* r) {
  BeginItem("CircleShape", r);
  WriteProperty("radius") << r->radius();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(RectangleShape* r) {
  BeginItem("RectangleShape", r);
  WriteProperty("width") << r->width();
  WriteProperty("height") << r->height();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(RoundedRectangleShape* r) {
  BeginItem("RoundedRectangleShape", r);
  WriteProperty("width") << r->width();
  WriteProperty("height") << r->height();
  WriteProperty("top_left_radius") << r->top_left_radius();
  WriteProperty("top_right_radius") << r->top_right_radius();
  WriteProperty("bottom_right_radius") << r->bottom_right_radius();
  WriteProperty("bottom_left_radius") << r->bottom_left_radius();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(MeshShape* r) {
  BeginItem("MeshShape", r);
  if (auto& mesh = r->escher_mesh()) {
    WriteProperty("num_indices") << mesh->num_indices();
    WriteProperty("num_vertices") << mesh->num_vertices();
    WriteProperty("index_buffer_offset") << mesh->index_buffer_offset();
    WriteProperty("vertex_buffer_offset") << mesh->attribute_buffer(0).offset;
    WriteProperty("vertex_buffer_stride") << mesh->attribute_buffer(0).stride;
    BeginSection("index_buffer");
    r->index_buffer()->Accept(this);
    EndSection();
    BeginSection("vertex_buffer");
    r->vertex_buffer()->Accept(this);
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Material* r) {
  BeginItem("Material", r);
  WriteProperty("red") << r->red();
  WriteProperty("green") << r->green();
  WriteProperty("blue") << r->blue();
  if (r->escher_material()->texture()) {
    auto texture = r->escher_material()->texture();
    WriteProperty("texture.width") << texture->width();
    WriteProperty("texture.height") << texture->height();
    WriteProperty("texture.size") << texture->image()->size();
  } else if (r->texture_image()) {
    auto image = r->texture_image()->GetEscherImage();
    if (image) {
      WriteProperty("texture.pending") << "true";
      WriteProperty("texture.width") << image->width();
      WriteProperty("texture.height") << image->height();
      WriteProperty("texture.size") << image->size();
    } else {
      WriteProperty("texture") << "(null)";
    }
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Compositor* r) {
  BeginItem("Compositor", r);
  if (r->layer_stack()) {
    BeginSection("stack");
    r->layer_stack()->Accept(this);
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(DisplayCompositor* r) {
  BeginItem("DisplayCompositor", r);
  if (r->layer_stack()) {
    BeginSection("stack");
    r->layer_stack()->Accept(this);
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(LayerStack* r) {
  BeginItem("LayerStack", r);
  if (!r->layers().empty()) {
    BeginSection("layers");
    for (auto& layer : r->layers()) {
      layer->Accept(this);
    }
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Layer* r) {
  BeginItem("Layer", r);
  WriteProperty("width") << r->width();
  WriteProperty("height") << r->height();
  if (r->renderer()) {
    BeginSection("renderer");
    r->renderer()->Accept(this);
    EndSection();
  } else {
    // TODO(MZ-249): Texture or ImagePipe or whatever.
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Camera* r) {
  using escher::operator<<;
  BeginItem("Camera", r);
  WriteProperty("position") << r->eye_position();
  WriteProperty("look_at") << r->eye_look_at();
  WriteProperty("up") << r->eye_up();
  BeginSection("scene");
  r->scene()->Accept(this);
  EndSection();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Renderer* r) {
  BeginItem("Renderer", r);
  if (r->camera()) {
    BeginSection("camera");
    r->camera()->Accept(this);
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Light* r) {
  FXL_CHECK(false) << "implement Visit() in Light subclasses";
}

void DumpVisitor::Visit(AmbientLight* r) {
  BeginItem("AmbientLight", r);
  escher::operator<<(WriteProperty("color"), r->color());
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(DirectionalLight* r) {
  BeginItem("DirectionalLight", r);
  escher::operator<<(WriteProperty("direction"), r->direction());
  escher::operator<<(WriteProperty("color"), r->color());
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(PointLight* r) {
  BeginItem("PointLight", r);
  escher::operator<<(WriteProperty("position"), r->position());
  escher::operator<<(WriteProperty("color"), r->color());
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Import* r) {
  BeginItem("Import", r);
  WriteProperty("import_spec") << static_cast<uint32_t>(r->import_spec());
  WriteProperty("is_bound") << r->is_bound();
  WriteProperty("focusable") << r->focusable();
  BeginSection("delegate");
  r->delegate()->Accept(this);
  EndSection();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::VisitResource(Resource* r) {
  if (r->event_mask()) {
    WriteProperty("event_mask") << r->event_mask();
  }
  if (!r->imports().empty()) {
    BeginSection("imports");
    for (auto& import : r->imports()) {
      import->Accept(this);
    }
    EndSection();
  }
}

void DumpVisitor::BeginItem(const char* type, Resource* r) {
  BeginLine();
  if (r) {
    output_ << r->global_id();
    if (!r->label().empty())
      output_ << ":\"" << r->label() << "\"";
    output_ << "> ";
  }
  output_ << type;
  indentation_ += 1;
}

std::ostream& DumpVisitor::WriteProperty(const char* label) {
  property_count_++;
  if (partial_line_) {
    if (property_count_ == 1u)
      output_ << ": ";
    else
      output_ << ", ";
  } else {
    BeginLine();
  }
  output_ << label << "=";
  return output_;
}

void DumpVisitor::EndItem() {
  EndLine();
  indentation_ -= 1;
}

void DumpVisitor::BeginSection(const char* label) {
  BeginLine();
  output_ << label << ":";
  EndLine();
}

void DumpVisitor::EndSection() { FXL_DCHECK(!partial_line_); }

void DumpVisitor::BeginLine() {
  EndLine();
  output_ << std::string(indentation_, ' ');
  partial_line_ = true;
}

void DumpVisitor::EndLine() {
  if (!partial_line_)
    return;
  output_ << std::endl;
  partial_line_ = false;
  property_count_ = 0u;
}

}  // namespace gfx
}  // namespace scenic_impl
