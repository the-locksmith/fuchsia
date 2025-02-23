// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/ssdo_sampler.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/glsl_compiler.h"
#include "lib/escher/impl/mesh_shader_binding.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/model_pipeline.h"
#include "lib/escher/impl/vk/pipeline.h"
#include "lib/escher/impl/vk/pipeline_spec.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/vk/framebuffer.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/texture.h"

namespace escher {
namespace impl {

namespace {

// Must match the descriptor set index used for textures in the fragment shaders
// below (g_sampler_fragment_src and g_filter_fragment_src).
constexpr char kTextureDescriptorSetBindIndex = 0;

constexpr char g_vertex_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  layout(location = 0) in vec2 in_position;
  layout(location = 2) in vec2 in_uv;

  layout(location = 0) out vec2 fragment_uv;

  out gl_PerVertex {
    vec4 gl_Position;
  };

  void main() {
    gl_Position = vec4(in_position, 0.f, 1.f);
    fragment_uv = in_uv;
  }
)GLSL";

// Samples occlusion in a neighborhood around each pixel.  Unoccluded samples
// are summed in order to obtain a measure of the amount of light that reaches
// this pixel.  The result is noisy, and should be filtered before used as a
// texture in a subsequent render pass.
constexpr char g_sampler_fragment_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  // Texture coordinates generated by the vertex shader.
  layout(location = 0) in vec2 fragment_uv;

  layout(location = 0) out vec4 outColor;

  // Uniform parameters.
  layout(push_constant) uniform SamplerConfig {
    // A description of the directional key light:
    //
    //  * theta, phi: The direction from which the light is received. The first
    //    coordinate is theta (the azimuthal angle, in radians) and the second
    //    coordinate is phi (the polar angle, in radians).
    //  * dispersion: The angular variance in the light, in radians.
    //  * intensity: The amount of light emitted.
    vec4 key_light;

    // The size of the viewing volume in (width, height, depth).
    vec3 viewing_volume;
  } pushed;

  // Depth information about the scene.
  //
  // The shader assumes that the depth information in the r channel.
  layout(set = 0, binding = 0) uniform sampler2D depth_map;

  // A table used to accelerate sampling by allowing an early exit for fragments
  // where no shadows are possible.
  layout(set = 0, binding = 1) uniform sampler2D accelerator;

  // A random texture of size kNoiseSize.
  layout(set = 0, binding = 2) uniform sampler2D noise;

  const float kPi = 3.14159265359;

  // Must match SsdoSampler::kNoiseSize (C++).
  const int kNoiseSize = 5;

  // The number of screen-space samples to use in the computation.
  const int kTapCount = 8;

  // These should be relatively primary to each other and to kTapCount;
  // TODO: only kSpirals.x is used... should .y also be used?
  const vec2 kSpirals = vec2(7.0, 5.0);

  // TODO(abarth): Make the shader less sensitive to this parameter.
  const float kSampleRadius = 16.0;  // screen pixels.

  const float kSsdoAccelDownsampleFactor = 8.0;
  const float kSsdoAccelPackedDownsampleFactor = kSsdoAccelDownsampleFactor * 4;

  float sampleKeyIllumination(vec2 fragment_uv,
                              float fragment_z,
                              float alpha,
                              vec2 seed) {
    float key_light_dispersion = pushed.key_light.z;
    vec2 key_light0 = pushed.key_light.xy - key_light_dispersion / 2.0;
    float theta = key_light0.x + fract(seed.x + alpha * kSpirals.x) * key_light_dispersion;
    float radius = alpha * kSampleRadius;

    vec2 tap_delta_uv = radius * vec2(cos(theta), sin(theta)) / pushed.viewing_volume.xy;
    float tap_depth_uv = texture(depth_map, fragment_uv + tap_delta_uv).r;
    float tap_z = tap_depth_uv * -pushed.viewing_volume.z;

    return 1.0 - clamp((tap_z - fragment_z) / radius, 0.0, 1.0);
  }

  float sampleFillIllumination(vec2 fragment_uv,
                               float fragment_z,
                               float alpha,
                               vec2 seed) {
    float theta = 2.0 * kPi * (seed.x + alpha * kSpirals.x);
    float radius = alpha * kSampleRadius;

    vec2 tap_delta_uv = radius * vec2(cos(theta), sin(theta)) / pushed.viewing_volume.xy;
    float tap_depth_uv = texture(depth_map, fragment_uv + tap_delta_uv).r;
    float tap_z = tap_depth_uv * -pushed.viewing_volume.z;

    return 1.0 - clamp((tap_z - fragment_z) / radius, 0.0, 1.0);
  }

  void main() {
    // gl_FragCoord.x ranges from 0.5 to (screen_width - 0.5), and similarly for
    // height, so we adjust them to range from 0.0 to screen_width/height.
    vec2 accel_coords = (gl_FragCoord.xy - vec2(0.5, 0.5)) / kSsdoAccelPackedDownsampleFactor;

    // Consult the accelerator; exit early if no shadow is possible,
    vec4 accel = texture(accelerator, accel_coords);
    uvec2 cell = uvec2(fract(accel_coords) * 4);
    float component = 0.0;
    switch (cell.y) {
      case 0:
        component = accel.r;
        break;
      case 1:
        component = accel.g;
        break;
      case 2:
        component = accel.b;
        break;
      case 3:
      default:
        component = accel.a;
        break;
    }
    int cell_val = (int(component * 255.0) >> (cell.x * 2)) & 3;
    if (cell_val == 0) {
      outColor = vec4(1.0, 0.0, 0.0, 1.0);
      return;
    }

    vec2 seed = texture(noise, fract(gl_FragCoord.xy / float(kNoiseSize))).rg;

    float sampled_depth = texture(depth_map, fragment_uv).r;
    float fragment_z = sampled_depth * -pushed.viewing_volume.z;
    float key_light_intensity = pushed.key_light.w;
    float fill_light_intensity = 1.0 - key_light_intensity;

    float L = 0.0;
    for (int i = 0; i < kTapCount; ++i) {
      float alpha = (float(i) + 0.5) / float(kTapCount);
      L += key_light_intensity * sampleKeyIllumination(fragment_uv, fragment_z, alpha, seed);
      L += fill_light_intensity * sampleFillIllumination(fragment_uv, fragment_z, alpha, seed);
    }
    L = clamp(L / float(kTapCount), 0.0, 1.0);

    outColor = vec4(L, sampled_depth, 0.0, 1.0);
  }
)GLSL";

// Filters the noisy occlusion data produced by g_sampler_fragment_src.  This
// is run in two passes, one horizontal and one vertical (these are configured
// by the FilterConfig.stride field below).  The resulting filtered image can be
// used to light the scene that was used to generate the depth-texture input
// to g_sampler_fragment_src.
constexpr char g_filter_fragment_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  // Texture coordinates generated by the vertex shader.
  layout(location = 0) in vec2 fragment_uv;

  layout(location = 0) out vec4 outColor;

  // Uniform parameters.
  layout(push_constant) uniform FilterConfig {
    vec2 stride;
    float scene_depth;
  } pushed;

  // Texture containing unfiltered illumination data.
  layout(set = 0, binding = 0) uniform sampler2D illumination;

  // A table used to accelerate sampling by allowing an early exit for fragments
  // where no shadows are possible.
  layout(set = 0, binding = 1) uniform sampler2D accelerator;

  // Related to SsdoSampler::kNoiseSize (== kNoiseSize - 1).
  // We need the reconstruction filter to remove exactly the frequency of the
  // noise, which is why these values need to be coordinated.
  const int kRadius = 4;

  const float kSsdoAccelDownsampleFactor = 8.0;
  const float kSsdoAccelPackedDownsampleFactor = kSsdoAccelDownsampleFactor * 4;

  void main() {
    // gl_FragCoord.x ranges from 0.5 to (screen_width - 0.5), and similarly for
    // height, so we adjust them to range from 0.0 to screen_width/height.
    vec2 accel_coords = (gl_FragCoord.xy - vec2(0.5, 0.5)) / kSsdoAccelPackedDownsampleFactor;

    // Consult the accelerator; exit early if no shadow is possible,
    vec4 accel = texture(accelerator, accel_coords);
    uvec2 cell = uvec2(fract(accel_coords) * 4);
    float component = 0.0;
    switch (cell.y) {
      case 0:
        component = accel.r;
        break;
      case 1:
        component = accel.g;
        break;
      case 2:
        component = accel.b;
        break;
      case 3:
      default:
        component = accel.a;
        break;
    }
    int cell_val = (int(component * 255.0) >> (cell.x * 2)) & 3;
    if (cell_val == 0) {
      outColor = vec4(1.0, 0.0, 0.0, 1.0);
      return;
    }

    vec4 center_tap = texture(illumination, fragment_uv);
    float center_key = center_tap.y * pushed.scene_depth;

    float sum = center_tap.x;
    float total_weight = 1.0;

    for (int r = 1; r <= kRadius; ++r) {
      vec4 left_tap = texture(illumination, fragment_uv + float(-r) * pushed.stride);
      float left_tap_key = left_tap.y * pushed.scene_depth;
      float left_key_weight = max(0.0, 1.0 - abs(left_tap_key - center_key));

      vec4 right_tap = texture(illumination, fragment_uv + float(r) * pushed.stride);
      float right_tap_key = right_tap.y * pushed.scene_depth;
      float right_key_weight = max(0.0, 1.0 - abs(right_tap_key - center_key));

      float position_weight = float(kRadius - r + 1) / float(kRadius + 1);
      float tap_weight = position_weight * left_key_weight * right_key_weight;

      sum += tap_weight * left_tap.x + tap_weight * right_tap.x;
      total_weight += 2.0 * tap_weight;
    }

    float filtered_illumination = sum / total_weight;
    outColor = vec4(filtered_illumination, center_tap.y, 0.0, 1.0);
  }
)GLSL";

// TODO: refactor this into a PipelineBuilder class.
std::pair<PipelinePtr, PipelinePtr> CreatePipelines(
    vk::Device device, vk::RenderPass render_pass,
    const MeshShaderBinding& mesh_shader_binding,
    vk::DescriptorSetLayout descriptor_set_layout,
    GlslToSpirvCompiler* compiler) {
  auto vertex_spirv_future =
      compiler->Compile(vk::ShaderStageFlagBits::eVertex, {{g_vertex_src}},
                        std::string(), "main");
  auto sampler_fragment_spirv_future =
      compiler->Compile(vk::ShaderStageFlagBits::eFragment,
                        {{g_sampler_fragment_src}}, std::string(), "main");
  auto filter_fragment_spirv_future =
      compiler->Compile(vk::ShaderStageFlagBits::eFragment,
                        {{g_filter_fragment_src}}, std::string(), "main");

  vk::ShaderModule vertex_module;
  {
    SpirvData spirv = vertex_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    vertex_module =
        ESCHER_CHECKED_VK_RESULT(device.createShaderModule(module_info));
  }

  constexpr uint32_t kNumShaderStages = 2;
  vk::PipelineShaderStageCreateInfo shader_stages[kNumShaderStages];
  auto& vertex_stage_info = shader_stages[0];
  auto& fragment_stage_info = shader_stages[1];
  // Unlike the vertex shader, the fragment shader differs between pipelines,
  // so we defer setting it.
  vertex_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
  vertex_stage_info.module = vertex_module;
  vertex_stage_info.pName = "main";
  fragment_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
  fragment_stage_info.pName = "main";

  vk::PipelineVertexInputStateCreateInfo vertex_input_info;
  vertex_input_info.vertexBindingDescriptionCount = 1;
  vertex_input_info.pVertexBindingDescriptions = mesh_shader_binding.binding();
  vertex_input_info.vertexAttributeDescriptionCount =
      mesh_shader_binding.attributes().size();
  vertex_input_info.pVertexAttributeDescriptions =
      mesh_shader_binding.attributes().data();

  vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
  input_assembly_info.topology = vk::PrimitiveTopology::eTriangleList;
  input_assembly_info.primitiveRestartEnable = false;

  vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
  depth_stencil_info.depthTestEnable = false;
  depth_stencil_info.depthWriteEnable = false;
  depth_stencil_info.stencilTestEnable = false;

  // This is set dynamically during rendering.
  vk::Viewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = 0.f;
  viewport.height = 0.f;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 0.0f;

  // This is set dynamically during rendering.
  vk::Rect2D scissor;
  scissor.offset = vk::Offset2D{0, 0};
  scissor.extent = vk::Extent2D{0, 0};

  vk::PipelineViewportStateCreateInfo viewport_state;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  vk::PipelineRasterizationStateCreateInfo rasterizer;
  rasterizer.depthClampEnable = false;
  rasterizer.rasterizerDiscardEnable = false;
  rasterizer.polygonMode = vk::PolygonMode::eFill;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = vk::CullModeFlagBits::eBack;
  rasterizer.frontFace = vk::FrontFace::eClockwise;
  rasterizer.depthBiasEnable = false;

  vk::PipelineMultisampleStateCreateInfo multisampling;
  multisampling.sampleShadingEnable = false;
  multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

  // TODO: revisit whether this is what we want
  vk::PipelineColorBlendAttachmentState color_blend_attachment;
  color_blend_attachment.colorWriteMask =
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  color_blend_attachment.blendEnable = false;

  // TODO: revisit whether this is what we want
  vk::PipelineColorBlendStateCreateInfo color_blending;
  color_blending.logicOpEnable = false;
  color_blending.logicOp = vk::LogicOp::eCopy;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
  color_blending.blendConstants[0] = 0.0f;
  color_blending.blendConstants[1] = 0.0f;
  color_blending.blendConstants[2] = 0.0f;
  color_blending.blendConstants[3] = 0.0f;

  vk::PipelineDynamicStateCreateInfo dynamic_state;
  const uint32_t kDynamicStateCount = 2;
  vk::DynamicState dynamic_states[] = {vk::DynamicState::eViewport,
                                       vk::DynamicState::eScissor};
  dynamic_state.dynamicStateCount = kDynamicStateCount;
  dynamic_state.pDynamicStates = dynamic_states;

  vk::PushConstantRange push_constants;
  push_constants.stageFlags = vk::ShaderStageFlagBits::eFragment;
  push_constants.offset = 0;
  // This allows us to share a pipeline-layout between two pipelines.
  push_constants.size = std::max(sizeof(SsdoSampler::SamplerConfig),
                                 sizeof(SsdoSampler::FilterConfig));

  vk::PipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
  pipeline_layout_info.pushConstantRangeCount = 1;
  pipeline_layout_info.pPushConstantRanges = &push_constants;

  auto pipeline_layout = fxl::MakeRefCounted<PipelineLayout>(
      device, ESCHER_CHECKED_VK_RESULT(
                  device.createPipelineLayout(pipeline_layout_info, nullptr)));

  vk::GraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.stageCount = kNumShaderStages;
  pipeline_info.pStages = shader_stages;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly_info;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pDepthStencilState = &depth_stencil_info;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = pipeline_layout->vk();
  pipeline_info.renderPass = render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = vk::Pipeline();

  // Pipeline configuration specific to the SSDO sampler pass.
  vk::ShaderModule sampler_fragment_module;
  {
    SpirvData spirv = sampler_fragment_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    sampler_fragment_module =
        ESCHER_CHECKED_VK_RESULT(device.createShaderModule(module_info));
  }
  fragment_stage_info.module = sampler_fragment_module;
  vk::Pipeline vk_sampler_pipeline = ESCHER_CHECKED_VK_RESULT(
      device.createGraphicsPipeline(nullptr, pipeline_info));
  auto sampler_pipeline = fxl::MakeRefCounted<Pipeline>(
      device, vk_sampler_pipeline, pipeline_layout, PipelineSpec());

  // Pipeline configuration specific to the SSDO filter pass.
  vk::ShaderModule filter_fragment_module;
  {
    SpirvData spirv = filter_fragment_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    filter_fragment_module =
        ESCHER_CHECKED_VK_RESULT(device.createShaderModule(module_info));
  }
  fragment_stage_info.module = filter_fragment_module;
  vk::Pipeline vk_filter_pipeline = ESCHER_CHECKED_VK_RESULT(
      device.createGraphicsPipeline(nullptr, pipeline_info));
  auto filter_pipeline = fxl::MakeRefCounted<Pipeline>(
      device, vk_filter_pipeline, pipeline_layout, PipelineSpec());

  device.destroyShaderModule(vertex_module);
  device.destroyShaderModule(sampler_fragment_module);
  device.destroyShaderModule(filter_fragment_module);

  return {sampler_pipeline, filter_pipeline};
}

vk::Format ChooseColorFormat(vk::PhysicalDevice physical_device) {
  // TODO: eR8G8Srgb would be preferable, but must check if it is supported.
  // TODO: validate this choice via performance profiling.
  vk::Format color_formats[] = {vk::Format::eR8G8Unorm,
                                vk::Format::eR8G8B8A8Unorm};
  for (auto format : color_formats) {
    vk::FormatProperties properties;
    physical_device.getFormatProperties(format, &properties);
    if (properties.optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eStorageImage)
      return format;
  }

  FXL_DCHECK(false);
  return vk::Format::eUndefined;
}

vk::RenderPass CreateRenderPass(vk::Device device, vk::Format color_format) {
  constexpr uint32_t kAttachmentCount = 1;
  vk::AttachmentDescription attachments[kAttachmentCount];

  // Only the color attachment is required; there is no depth buffer (although
  // one from a previous pass will be provided to the shader as a texture).
  const uint32_t kColorAttachment = 0;
  auto& color_attachment = attachments[kColorAttachment];
  color_attachment.format = color_format;
  color_attachment.samples = vk::SampleCountFlagBits::e1;
  color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
  color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
  color_attachment.initialLayout = vk::ImageLayout::eUndefined;
  color_attachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::AttachmentReference color_reference;
  color_reference.attachment = kColorAttachment;
  color_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

  // A vk::RenderPass needs at least one subpass.
  constexpr uint32_t kSubpassCount = 1;
  vk::SubpassDescription subpasses[kSubpassCount];
  subpasses[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpasses[0].colorAttachmentCount = 1;
  subpasses[0].pColorAttachments = &color_reference;

  // Even though we have a single subpass, we need to declare dependencies to
  // support the layout transitions specified by the attachment references.
  constexpr uint32_t kDependencyCount = 2;
  vk::SubpassDependency dependencies[kDependencyCount];
  auto& input_dependency = dependencies[0];
  auto& output_dependency = dependencies[1];

  // The first dependency transitions from the final layout from the previous
  // render pass, to the initial layout of this one.
  input_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // not in vulkan.hpp ?!?
  input_dependency.dstSubpass = 0;
  input_dependency.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  input_dependency.dstStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  // TODO: should srcAccessMask also include eMemoryWrite?
  input_dependency.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
  input_dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                   vk::AccessFlagBits::eColorAttachmentWrite;
  input_dependency.dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // The second dependency describes the transition from the initial to final
  // layout.
  output_dependency.srcSubpass = 0;
  output_dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
  output_dependency.srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  output_dependency.dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  output_dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                    vk::AccessFlagBits::eColorAttachmentWrite;
  output_dependency.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
  output_dependency.dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // Create the render pass, now that we can fully specify it.
  vk::RenderPassCreateInfo info;
  info.attachmentCount = kAttachmentCount;
  info.pAttachments = attachments;
  info.subpassCount = kSubpassCount;
  info.pSubpasses = subpasses;
  info.dependencyCount = kDependencyCount;
  info.pDependencies = dependencies;

  return ESCHER_CHECKED_VK_RESULT(device.createRenderPass(info));
}

}  // namespace

SsdoSampler::SsdoSampler(EscherWeakPtr escher, MeshPtr full_screen,
                         ImagePtr noise_image, ModelData* model_data)
    : device_(escher->vulkan_context().device),
      color_format_(ChooseColorFormat(escher->vk_physical_device())),
      pool_(escher, GetDescriptorSetLayoutCreateInfo(), 6),
      full_screen_(full_screen),
      noise_texture_(fxl::MakeRefCounted<Texture>(
          escher->resource_recycler(), noise_image, vk::Filter::eNearest)),
      // TODO: VulkanProvider should know the swapchain format and we should use
      // it.
      render_pass_(CreateRenderPass(device_, color_format_)) {
  FXL_DCHECK(noise_image->width() == kNoiseSize &&
             noise_image->height() == kNoiseSize);

  FXL_DCHECK(full_screen_->spec() ==
             MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kUV});

  auto pipelines =
      CreatePipelines(device_, render_pass_,
                      model_data->GetMeshShaderBinding(full_screen_->spec()),
                      pool_.layout(), escher->glsl_compiler());
  sampler_pipeline_ = pipelines.first;
  filter_pipeline_ = pipelines.second;
}

SsdoSampler::~SsdoSampler() { device_.destroyRenderPass(render_pass_); }

const vk::DescriptorSetLayoutCreateInfo&
SsdoSampler::GetDescriptorSetLayoutCreateInfo() {
  constexpr uint32_t kNumBindings = 3;
  static vk::DescriptorSetLayoutBinding bindings[kNumBindings];
  static vk::DescriptorSetLayoutCreateInfo info;
  static vk::DescriptorSetLayoutCreateInfo* ptr = nullptr;
  if (!ptr) {
    // TODO: should probably use a texture array instead of multiple bindings.
    auto& depth_texture_binding = bindings[0];
    auto& accelerator_texture_binding = bindings[1];
    auto& noise_texture_binding = bindings[2];

    depth_texture_binding.binding = 0;
    depth_texture_binding.descriptorType =
        vk::DescriptorType::eCombinedImageSampler;
    depth_texture_binding.descriptorCount = 1;
    depth_texture_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    accelerator_texture_binding.binding = 1;
    accelerator_texture_binding.descriptorType =
        vk::DescriptorType::eCombinedImageSampler;
    accelerator_texture_binding.descriptorCount = 1;
    accelerator_texture_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    noise_texture_binding.binding = 2;
    noise_texture_binding.descriptorType =
        vk::DescriptorType::eCombinedImageSampler;
    noise_texture_binding.descriptorCount = 1;
    noise_texture_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    info.bindingCount = kNumBindings;
    info.pBindings = bindings;
    ptr = &info;
  }
  return *ptr;
}

void SsdoSampler::Sample(CommandBuffer* command_buffer,
                         const escher::FramebufferPtr& framebuffer,
                         const TexturePtr& depth_texture,
                         const TexturePtr& accelerator_texture,
                         const SamplerConfig* push_constants) {
  auto vk_command_buffer = command_buffer->vk();
  auto descriptor_set = pool_.Allocate(1, command_buffer)->get(0);

  vk::Viewport viewport;
  viewport.width = framebuffer->width();
  viewport.height = framebuffer->height();
  vk_command_buffer.setViewport(0, 1, &viewport);

  constexpr uint32_t kUpdatedDescriptorCount = 3;
  vk::WriteDescriptorSet writes[kUpdatedDescriptorCount];
  for (uint32_t i = 0; i < kUpdatedDescriptorCount; ++i) {
    // Common to all image descriptors.
    writes[i].dstSet = descriptor_set;
    writes[i].dstArrayElement = 0;
    writes[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[i].descriptorCount = 1;
  }

  // Specific to depth texture.
  vk::DescriptorImageInfo depth_texture_info;
  depth_texture_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  depth_texture_info.imageView = depth_texture->vk_image_view();
  depth_texture_info.sampler = depth_texture->vk_sampler();
  writes[0].dstBinding = 0;
  writes[0].pImageInfo = &depth_texture_info;

  // Specific to accelerator texture.
  vk::DescriptorImageInfo accelerator_texture_info;
  accelerator_texture_info.imageLayout =
      vk::ImageLayout::eShaderReadOnlyOptimal;
  accelerator_texture_info.imageView = accelerator_texture->vk_image_view();
  accelerator_texture_info.sampler = accelerator_texture->vk_sampler();
  writes[1].dstBinding = 1;
  writes[1].pImageInfo = &accelerator_texture_info;

  // Specific to noise texture.
  vk::DescriptorImageInfo noise_texture_info;
  noise_texture_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  noise_texture_info.imageView = noise_texture_->vk_image_view();
  noise_texture_info.sampler = noise_texture_->vk_sampler();
  writes[2].dstBinding = 2;
  writes[2].pImageInfo = &noise_texture_info;

  device_.updateDescriptorSets(kUpdatedDescriptorCount, writes, 0, nullptr);

  vk::ClearValue clear_value(
      vk::ClearColorValue(std::array<uint32_t, 4>{{0, 0, 0, 0}}));
  command_buffer->BeginRenderPass(
      render_pass_, framebuffer, &clear_value, 1,
      Camera::Viewport().vk_rect_2d(framebuffer->width(),
                                    framebuffer->height()));
  {
    auto vk_pipeline_layout = sampler_pipeline_->vk_layout();

    vk_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   sampler_pipeline_->vk());

    vk_command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, vk_pipeline_layout,
        kTextureDescriptorSetBindIndex, 1, &descriptor_set, 0, nullptr);

    vk_command_buffer.pushConstants(vk_pipeline_layout,
                                    vk::ShaderStageFlagBits::eFragment, 0,
                                    sizeof(SamplerConfig), push_constants);

    command_buffer->DrawMesh(full_screen_);
  }
  command_buffer->EndRenderPass();
}

void SsdoSampler::Filter(CommandBuffer* command_buffer,
                         const escher::FramebufferPtr& framebuffer,
                         const TexturePtr& unfiltered_illumination,
                         const TexturePtr& accelerator_texture,
                         const FilterConfig* push_constants) {
  auto vk_command_buffer = command_buffer->vk();
  auto descriptor_set = pool_.Allocate(1, command_buffer)->get(0);

  vk::Viewport viewport;
  viewport.width = framebuffer->width();
  viewport.height = framebuffer->height();
  vk_command_buffer.setViewport(0, 1, &viewport);

  constexpr uint32_t kUpdatedDescriptorCount = 3;
  vk::WriteDescriptorSet writes[kUpdatedDescriptorCount];
  for (uint32_t i = 0; i < kUpdatedDescriptorCount; ++i) {
    // Common to all image descriptors.
    writes[i].dstSet = descriptor_set;
    writes[i].dstArrayElement = 0;
    writes[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[i].descriptorCount = 1;
  }

  // Specific to illumination texture.
  vk::DescriptorImageInfo light_tex_info;
  light_tex_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  light_tex_info.imageView = unfiltered_illumination->vk_image_view();
  light_tex_info.sampler = unfiltered_illumination->vk_sampler();
  writes[0].dstBinding = 0;
  writes[0].pImageInfo = &light_tex_info;

  // Specific to accelerator texture.
  vk::DescriptorImageInfo accelerator_texture_info;
  accelerator_texture_info.imageLayout =
      vk::ImageLayout::eShaderReadOnlyOptimal;
  accelerator_texture_info.imageView = accelerator_texture->vk_image_view();
  accelerator_texture_info.sampler = accelerator_texture->vk_sampler();
  writes[1].dstBinding = 1;
  writes[1].pImageInfo = &accelerator_texture_info;

  // Specific to noise texture.
  // TODO: this is unused by the shader, but we set it anyway so that we can
  // use the same pipeline-layout for the sampler and filter pipelines.
  vk::DescriptorImageInfo noise_texture_info;
  noise_texture_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  noise_texture_info.imageView = noise_texture_->vk_image_view();
  noise_texture_info.sampler = noise_texture_->vk_sampler();
  writes[2].dstBinding = 2;
  writes[2].pImageInfo = &noise_texture_info;

  device_.updateDescriptorSets(kUpdatedDescriptorCount, writes, 0, nullptr);

  vk::ClearValue clear_value(
      vk::ClearColorValue(std::array<uint32_t, 4>{{0, 0, 0, 0}}));
  command_buffer->BeginRenderPass(
      render_pass_, framebuffer, &clear_value, 1,
      Camera::Viewport().vk_rect_2d(framebuffer->width(),
                                    framebuffer->height()));
  {
    auto vk_pipeline_layout = sampler_pipeline_->vk_layout();

    vk_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   filter_pipeline_->vk());

    vk_command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, vk_pipeline_layout,
        kTextureDescriptorSetBindIndex, 1, &descriptor_set, 0, nullptr);

    vk_command_buffer.pushConstants(vk_pipeline_layout,
                                    vk::ShaderStageFlagBits::eFragment, 0,
                                    sizeof(FilterConfig), push_constants);

    command_buffer->DrawMesh(full_screen_);
  }
  command_buffer->EndRenderPass();
}

SsdoSampler::SamplerConfig::SamplerConfig(const Stage& stage)
    : key_light(vec4(stage.key_light().polar_direction(),
                     stage.key_light().dispersion(),
                     stage.key_light().intensity())),
      viewing_volume(vec3(stage.viewing_volume().width(),
                          stage.viewing_volume().height(),
                          stage.viewing_volume().depth())) {}

}  // namespace impl
}  // namespace escher
