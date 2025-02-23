/*
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Chia-I Wu <olv@lunarg.com>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Ian Elliott <ian@LunarG.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Gwan-gyeong Mun <elongbug@gmail.com>
 * Author: Tony Barbour <tony@LunarG.com>
 */

/*
 * Video frame: bbb_frame.yuv
 * (c) copyright Blender Foundation | www.bigbuckbunny.org
 */

#include "cube.h"

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(VK_USE_PLATFORM_XLIB_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)
#include <X11/Xutil.h>
#endif

#if defined(VK_USE_PLATFORM_FUCHSIA)
#include <chrono>
#endif

#if defined(MAGMA_USE_SHIM)
#include "vulkan_shim.h"
#else
#include <vulkan/vulkan.h>
#endif

#include "platform_trace.h"

#ifdef MAGMA_ENABLE_TRACING
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>
#endif

#include "lib/fxl/log_settings_command_line.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

//#define USE_YUV_TEXTURE 1

#if USE_YUV_TEXTURE
#define TEX_FORMAT VK_FORMAT_G8B8G8R8_422_UNORM_KHR
#else
#define TEX_FORMAT VK_FORMAT_R8G8B8A8_UNORM
#endif

#if defined(NDEBUG) && defined(__GNUC__)
#define U_ASSERT_ONLY __attribute__((unused))
#else
#define U_ASSERT_ONLY
#endif

#if defined(__GNUC__)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

#define ERR_EXIT(err_msg, err_class) \
  do {                               \
    printf(err_msg);                 \
    fflush(stdout);                  \
    exit(1);                         \
  } while (0)

#define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                           \
  {                                                                        \
    demo->fp##entrypoint =                                                 \
        (PFN_vk##entrypoint)vkGetInstanceProcAddr(inst, "vk" #entrypoint); \
    if (demo->fp##entrypoint == NULL) {                                    \
      ERR_EXIT("vkGetInstanceProcAddr failed to find vk" #entrypoint,      \
               "vkGetInstanceProcAddr Failure");                           \
    }                                                                      \
  }

static PFN_vkGetDeviceProcAddr g_gdpa = NULL;

#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                                 \
  {                                                                           \
    if (!g_gdpa)                                                              \
      g_gdpa = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(                \
          demo->inst, "vkGetDeviceProcAddr");                                 \
    demo->fp##entrypoint = (PFN_vk##entrypoint)g_gdpa(dev, "vk" #entrypoint); \
    if (demo->fp##entrypoint == NULL) {                                       \
      ERR_EXIT("vkGetDeviceProcAddr failed to find vk" #entrypoint,           \
               "vkGetDeviceProcAddr Failure");                                \
    }                                                                         \
  }

static const char* tex_files[] = {"/pkg/data/bbb_frame.yuv"};

static int validation_error = 0;

struct vkcube_vs_uniform {
  // Must start with MVP
  float mvp[4][4];
  float position[12 * 3][4];
  float color[12 * 3][4];
};

struct vktexcube_vs_uniform {
  // Must start with MVP
  float mvp[4][4];
  float position[12 * 3][4];
  float attr[12 * 3][4];
};

//--------------------------------------------------------------------------------------
// Mesh and VertexFormat Data
//--------------------------------------------------------------------------------------
// clang-format off
static const float g_vertex_buffer_data[] = {
    -1.0f,-1.0f,-1.0f,  // -X side
    -1.0f,-1.0f, 1.0f,
    -1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f,-1.0f,
    -1.0f,-1.0f,-1.0f,

    -1.0f,-1.0f,-1.0f,  // -Z side
     1.0f, 1.0f,-1.0f,
     1.0f,-1.0f,-1.0f,
    -1.0f,-1.0f,-1.0f,
    -1.0f, 1.0f,-1.0f,
     1.0f, 1.0f,-1.0f,

    -1.0f,-1.0f,-1.0f,  // -Y side
     1.0f,-1.0f,-1.0f,
     1.0f,-1.0f, 1.0f,
    -1.0f,-1.0f,-1.0f,
     1.0f,-1.0f, 1.0f,
    -1.0f,-1.0f, 1.0f,

    -1.0f, 1.0f,-1.0f,  // +Y side
    -1.0f, 1.0f, 1.0f,
     1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f,-1.0f,
     1.0f, 1.0f, 1.0f,
     1.0f, 1.0f,-1.0f,

     1.0f, 1.0f,-1.0f,  // +X side
     1.0f, 1.0f, 1.0f,
     1.0f,-1.0f, 1.0f,
     1.0f,-1.0f, 1.0f,
     1.0f,-1.0f,-1.0f,
     1.0f, 1.0f,-1.0f,

    -1.0f, 1.0f, 1.0f,  // +Z side
    -1.0f,-1.0f, 1.0f,
     1.0f, 1.0f, 1.0f,
    -1.0f,-1.0f, 1.0f,
     1.0f,-1.0f, 1.0f,
     1.0f, 1.0f, 1.0f,
};

static const float g_uv_buffer_data[] = {
    0.0f, 1.0f,  // -X side
    1.0f, 1.0f,
    1.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 0.0f,
    0.0f, 1.0f,

    1.0f, 1.0f,  // -Z side
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 0.0f,

    1.0f, 0.0f,  // -Y side
    1.0f, 1.0f,
    0.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,

    1.0f, 0.0f,  // +Y side
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,

    1.0f, 0.0f,  // +X side
    0.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,

    0.0f, 0.0f,  // +Z side
    0.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,
};
// clang-format on

void dumpMatrix(const char* note, mat4x4 MVP) {
  int i;

  printf("%s: \n", note);
  for (i = 0; i < 4; i++) {
    printf("%f, %f, %f, %f\n", MVP[i][0], MVP[i][1], MVP[i][2], MVP[i][3]);
  }
  printf("\n");
  fflush(stdout);
}

void dumpVec4(const char* note, vec4 vector) {
  printf("%s: \n", note);
  printf("%f, %f, %f, %f\n", vector[0], vector[1], vector[2], vector[3]);
  printf("\n");
  fflush(stdout);
}

VKAPI_ATTR VkBool32 VKAPI_CALL
BreakCallback(VkFlags msgFlags, VkDebugReportObjectTypeEXT objType,
              uint64_t srcObject, size_t location, int32_t msgCode,
              const char* pLayerPrefix, const char* pMsg, void* pUserData) {
  assert(false);

  return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL dbgFunc(VkFlags msgFlags,
                                       VkDebugReportObjectTypeEXT objType,
                                       uint64_t srcObject, size_t location,
                                       int32_t msgCode,
                                       const char* pLayerPrefix,
                                       const char* pMsg, void* pUserData) {
  // clang-format off
    char *message = (char *)malloc(strlen(pMsg) + 100);

    assert(message);

    // We know we're submitting queues without fences, ignore this
    if (strstr(pMsg, "vkQueueSubmit parameter, VkFence fence, is null pointer"))
        return false;

    if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        sprintf(message, "INFORMATION: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        sprintf(message, "WARNING: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        sprintf(message, "PERFORMANCE WARNING: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        sprintf(message, "ERROR: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        sprintf(message, "DEBUG: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else {
        sprintf(message, "INFORMATION: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    }

    printf("%s\n", message);
    fflush(stdout);

    free(message);

  // clang-format on

  /*
   * false indicates that layer should not bail-out of an
   * API call that had validation failures. This may mean that the
   * app dies inside the driver due to invalid parameter(s).
   * That's what would happen without validation layers, so we'll
   * keep that behavior here.
   */
  return false;
}

// Forward declaration:
static void demo_resize(struct demo* demo);

static bool memory_type_from_properties(struct demo* demo, uint32_t typeBits,
                                        VkFlags requirements_mask,
                                        uint32_t* typeIndex) {
  // Search memtypes to find first index with those properties
  for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
    if ((typeBits & 1) == 1) {
      // Type is available, does it match user properties?
      if ((demo->memory_properties.memoryTypes[i].propertyFlags &
           requirements_mask) == requirements_mask) {
        *typeIndex = i;
        return true;
      }
    }
    typeBits >>= 1;
  }
  // No memory types matched, return failure
  return false;
}

static void demo_flush_init_cmd(struct demo* demo) {
  VkResult U_ASSERT_ONLY err;

  // This function could get called twice if the texture uses a staging buffer
  // In that case the second call should be ignored
  if (demo->cmd == VK_NULL_HANDLE)
    return;

  err = vkEndCommandBuffer(demo->cmd);
  assert(!err);

  VkFence fence;
  VkFenceCreateInfo fence_ci = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = NULL, .flags = 0};
  vkCreateFence(demo->device, &fence_ci, NULL, &fence);
  const VkCommandBuffer cmd_bufs[] = {demo->cmd};
  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .pNext = NULL,
                              .waitSemaphoreCount = 0,
                              .pWaitSemaphores = NULL,
                              .pWaitDstStageMask = NULL,
                              .commandBufferCount = 1,
                              .pCommandBuffers = cmd_bufs,
                              .signalSemaphoreCount = 0,
                              .pSignalSemaphores = NULL};

  err = vkQueueSubmit(demo->graphics_queue, 1, &submit_info, fence);
  assert(!err);

  err = vkWaitForFences(demo->device, 1, &fence, VK_TRUE, UINT64_MAX);
  assert(!err);

  vkFreeCommandBuffers(demo->device, demo->cmd_pool, 1, cmd_bufs);
  vkDestroyFence(demo->device, fence, NULL);
  demo->cmd = VK_NULL_HANDLE;
}

static void demo_set_image_layout(struct demo* demo, VkImage image,
                                  VkImageAspectFlags aspectMask,
                                  VkImageLayout old_image_layout,
                                  VkImageLayout new_image_layout,
                                  VkAccessFlagBits srcAccessMask,
                                  VkPipelineStageFlags src_stages,
                                  VkPipelineStageFlags dest_stages) {
  assert(demo->cmd);

  VkImageMemoryBarrier image_memory_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = srcAccessMask,
      .dstAccessMask = 0,
      .oldLayout = old_image_layout,
      .newLayout = new_image_layout,
      .image = image,
      .subresourceRange = {aspectMask, 0, 1, 0, 1}};

  switch (new_image_layout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      /* Make sure anything that was copying from this image has completed */
      image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      image_memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      image_memory_barrier.dstAccessMask =
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      image_memory_barrier.dstAccessMask =
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
      break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      break;

    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      image_memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      break;

    default:
      image_memory_barrier.dstAccessMask = 0;
      break;
  }

  VkImageMemoryBarrier* pmemory_barrier = &image_memory_barrier;

  vkCmdPipelineBarrier(demo->cmd, src_stages, dest_stages, 0, 0, NULL, 0, NULL,
                       1, pmemory_barrier);
}

static void demo_draw_build_cmd(struct demo* demo, VkCommandBuffer cmd_buf) {
  const VkCommandBufferBeginInfo cmd_buf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = NULL,
      .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
      .pInheritanceInfo = NULL,
  };
  const VkClearValue clear_values[2] = {
      [0] = {.color.float32 = {0.2f, 0.2f, 0.2f, 0.2f}},
      [1] = {.depthStencil = {1.0f, 0}},
  };
  const VkRenderPassBeginInfo rp_begin = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .pNext = NULL,
      .renderPass = demo->render_pass,
      .framebuffer = demo->framebuffers[demo->current_buffer],
      .renderArea.offset.x = 0,
      .renderArea.offset.y = 0,
      .renderArea.extent.width = demo->width,
      .renderArea.extent.height = demo->height,
      .clearValueCount = 2,
      .pClearValues = clear_values,
  };
  VkResult U_ASSERT_ONLY err;

  err = vkBeginCommandBuffer(cmd_buf, &cmd_buf_info);
  assert(!err);
  vkCmdBeginRenderPass(cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, demo->pipeline);
  vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          demo->pipeline_layout, 0, 1, &demo->desc_set, 0,
                          NULL);
  VkViewport viewport;
  memset(&viewport, 0, sizeof(viewport));
  viewport.height = (float)demo->height;
  viewport.width = (float)demo->width;
  viewport.minDepth = (float)0.0f;
  viewport.maxDepth = (float)1.0f;
  vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

  VkRect2D scissor;
  memset(&scissor, 0, sizeof(scissor));
  scissor.extent.width = demo->width;
  scissor.extent.height = demo->height;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  vkCmdSetScissor(cmd_buf, 0, 1, &scissor);
  vkCmdDraw(cmd_buf, 12 * 3, 1, 0, 0);
  // Note that ending the renderpass changes the image's layout from
  // COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
  vkCmdEndRenderPass(cmd_buf);

  if (demo->separate_present_queue) {
    // We have to transfer ownership from the graphics queue family to the
    // present queue family to be able to present.  Note that we don't have
    // to transfer from present queue family back to graphics queue family at
    // the start of the next frame because we don't care about the image's
    // contents at that point.
    VkImageMemoryBarrier image_ownership_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = demo->graphics_queue_family_index,
        .dstQueueFamilyIndex = demo->present_queue_family_index,
        .image = demo->buffers[demo->current_buffer].image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0,
                         NULL, 1, &image_ownership_barrier);
  }
  err = vkEndCommandBuffer(cmd_buf);
  assert(!err);
}

void demo_build_image_ownership_cmd(struct demo* demo, int i) {
  VkResult U_ASSERT_ONLY err;

  const VkCommandBufferBeginInfo cmd_buf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = NULL,
      .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
      .pInheritanceInfo = NULL,
  };
  err = vkBeginCommandBuffer(demo->buffers[i].graphics_to_present_cmd,
                             &cmd_buf_info);
  assert(!err);

  VkImageMemoryBarrier image_ownership_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .srcQueueFamilyIndex = demo->graphics_queue_family_index,
      .dstQueueFamilyIndex = demo->present_queue_family_index,
      .image = demo->buffers[i].image,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

  vkCmdPipelineBarrier(demo->buffers[i].graphics_to_present_cmd,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       NULL, 0, NULL, 1, &image_ownership_barrier);
  err = vkEndCommandBuffer(demo->buffers[i].graphics_to_present_cmd);
  assert(!err);
}

void demo_update_data_buffer(struct demo* demo) {
  mat4x4 MVP, Model, VP;
  int matrixSize = sizeof(MVP);
  uint8_t* pData;
  VkResult U_ASSERT_ONLY err;

  mat4x4_mul(VP, demo->projection_matrix, demo->view_matrix);

  // Rotate 22.5 degrees around the Y axis
  mat4x4_dup(Model, demo->model_matrix);
  mat4x4_rotate(demo->model_matrix, Model, 0.0f, 1.0f, 0.0f,
                (float)degreesToRadians(demo->spin_angle));
  mat4x4_mul(MVP, VP, demo->model_matrix);

  err = vkMapMemory(demo->device, demo->uniform_data.mem, 0,
                    demo->uniform_data.mem_alloc.allocationSize, 0,
                    (void**)&pData);
  assert(!err);

  memcpy(pData, (const void*)&MVP[0][0], matrixSize);

  vkUnmapMemory(demo->device, demo->uniform_data.mem);
}

void demo_draw(struct demo* demo) {
  VkResult U_ASSERT_ONLY err;

  // Ensure no more than FRAME_LAG renderings are outstanding
  vkWaitForFences(demo->device, 1, &demo->fences[demo->frame_index], VK_TRUE,
                  UINT64_MAX);
  vkResetFences(demo->device, 1, &demo->fences[demo->frame_index]);

  // We update the data buffer here after waiting for the fence because
  // otherwise the updated uniform buffer can affect the last frame, resulting
  // in jitter.
  demo_update_data_buffer(demo);

  TRACE_NONCE_DECLARE(nonce);
  TRACE_ASYNC_BEGIN("cube", "acquire next image", nonce);

  while (true) {
    VkSemaphore acquire_semaphore =
        demo->image_acquired_semaphores[demo->frame_index];

    // Get the index of the next available swapchain image:
    err = demo->fpAcquireNextImageKHR(demo->device, demo->swapchain,
                                      5000 * 1000 * 1000, acquire_semaphore,
                                      VK_NULL_HANDLE, &demo->current_buffer);

    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
      // demo->swapchain is out of date (e.g. the window was resized) and
      // must be recreated:
      demo->frame_index += 1;
      demo->frame_index %= FRAME_LAG;

      demo_resize(demo);
      demo_draw(demo);
      return;
    } else if (err == VK_SUBOPTIMAL_KHR) {
      // demo->swapchain is not as optimal as it could be, but the platform's
      // presentation engine will still present the image correctly.
    } else if (err == VK_TIMEOUT) {
      printf("timed out waiting for swapchain image acquire, retrying\n");
      continue;
    } else {
      assert(!err);
      break;
    }
  }
  TRACE_ASYNC_END("cube", "acquire next image", nonce);

  // Wait for the image acquired semaphore to be signaled to ensure
  // that the image won't be rendered to until the presentation
  // engine has fully released ownership to the application, and it is
  // okay to render to the image.
  VkFence nullFence = VK_NULL_HANDLE;
  VkPipelineStageFlags pipe_stage_flags;
  VkSubmitInfo submit_info;
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.pNext = NULL;
  submit_info.pWaitDstStageMask = &pipe_stage_flags;
  pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores =
      &demo->image_acquired_semaphores[demo->frame_index];
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &demo->buffers[demo->current_buffer].cmd;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores =
      &demo->draw_complete_semaphores[demo->frame_index];
  err = vkQueueSubmit(demo->graphics_queue, 1, &submit_info,
                      demo->fences[demo->frame_index]);
  assert(!err);

  if (demo->separate_present_queue) {
    // If we are using separate queues, change image ownership to the
    // present queue before presenting, waiting for the draw complete
    // semaphore and signalling the ownership released semaphore when finished
    pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores =
        &demo->draw_complete_semaphores[demo->frame_index];
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers =
        &demo->buffers[demo->current_buffer].graphics_to_present_cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores =
        &demo->image_ownership_semaphores[demo->frame_index];
    err = vkQueueSubmit(demo->present_queue, 1, &submit_info, nullFence);
    assert(!err);
  }

  // If we are using separate queues we have to wait for image ownership,
  // otherwise wait for draw complete
  VkPresentInfoKHR present = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext = NULL,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores =
          (demo->separate_present_queue)
              ? &demo->image_ownership_semaphores[demo->frame_index]
              : &demo->draw_complete_semaphores[demo->frame_index],
      .swapchainCount = 1,
      .pSwapchains = &demo->swapchain,
      .pImageIndices = &demo->current_buffer,
  };

  err = demo->fpQueuePresentKHR(demo->present_queue, &present);
  demo->frame_index += 1;
  demo->frame_index %= FRAME_LAG;

  if (err == VK_ERROR_OUT_OF_DATE_KHR) {
    // demo->swapchain is out of date (e.g. the window was resized) and
    // must be recreated:
    demo_resize(demo);
  } else if (err == VK_SUBOPTIMAL_KHR) {
    // demo->swapchain is not as optimal as it could be, but the platform's
    // presentation engine will still present the image correctly.
  } else {
    assert(!err);
  }
}

void demo_prepare_buffers(struct demo* demo) {
  VkResult U_ASSERT_ONLY err;
  VkSwapchainKHR oldSwapchain = demo->swapchain;

  // Check the surface capabilities and formats
  VkSurfaceCapabilitiesKHR surfCapabilities;
  err = demo->fpGetPhysicalDeviceSurfaceCapabilitiesKHR(
      demo->gpu, demo->surface, &surfCapabilities);
  assert(!err);

  uint32_t presentModeCount;
  err = demo->fpGetPhysicalDeviceSurfacePresentModesKHR(
      demo->gpu, demo->surface, &presentModeCount, NULL);
  assert(!err);
  VkPresentModeKHR* presentModes =
      (VkPresentModeKHR*)malloc(presentModeCount * sizeof(VkPresentModeKHR));
  assert(presentModes);
  err = demo->fpGetPhysicalDeviceSurfacePresentModesKHR(
      demo->gpu, demo->surface, &presentModeCount, presentModes);
  assert(!err);

  VkExtent2D swapchainExtent;
  // width and height are either both 0xFFFFFFFF, or both not 0xFFFFFFFF.
  if (surfCapabilities.currentExtent.width == 0xFFFFFFFF) {
    // If the surface size is undefined, the size is set to the size
    // of the images requested, which must fit within the minimum and
    // maximum values.
    swapchainExtent.width = demo->width;
    swapchainExtent.height = demo->height;

    if (swapchainExtent.width < surfCapabilities.minImageExtent.width) {
      swapchainExtent.width = surfCapabilities.minImageExtent.width;
    } else if (swapchainExtent.width > surfCapabilities.maxImageExtent.width) {
      swapchainExtent.width = surfCapabilities.maxImageExtent.width;
    }

    if (swapchainExtent.height < surfCapabilities.minImageExtent.height) {
      swapchainExtent.height = surfCapabilities.minImageExtent.height;
    } else if (swapchainExtent.height >
               surfCapabilities.maxImageExtent.height) {
      swapchainExtent.height = surfCapabilities.maxImageExtent.height;
    }
  } else {
    // If the surface size is defined, the swap chain size must match
    swapchainExtent = surfCapabilities.currentExtent;
    demo->width = surfCapabilities.currentExtent.width;
    demo->height = surfCapabilities.currentExtent.height;
  }

  printf("using resolution %ux%u\n", demo->width, demo->height);

  mat4x4_perspective(demo->projection_matrix, (float)degreesToRadians(45.0f),
                     (float)demo->width / (float)demo->height, 0.1f, 100.0f);

  // The FIFO present mode is guaranteed by the spec to be supported
  // and to have no tearing.  It's a great default present mode to use.
  VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

  //  There are times when you may wish to use another present mode.  The
  //  following code shows how to select them, and the comments provide some
  //  reasons you may wish to use them.
  //
  // It should be noted that Vulkan 1.0 doesn't provide a method for
  // synchronizing rendering with the presentation engine's display.  There
  // is a method provided for throttling rendering with the display, but
  // there are some presentation engines for which this method will not work.
  // If an application doesn't throttle its rendering, and if it renders much
  // faster than the refresh rate of the display, this can waste power on
  // mobile devices.  That is because power is being spent rendering images
  // that may never be seen.

  // VK_PRESENT_MODE_IMMEDIATE_KHR is for applications that don't care about
  // tearing, or have some way of synchronizing their rendering with the
  // display.
  // VK_PRESENT_MODE_MAILBOX_KHR may be useful for applications that
  // generally render a new presentable image every refresh cycle, but are
  // occasionally early.  In this case, the application wants the new image
  // to be displayed instead of the previously-queued-for-presentation image
  // that has not yet been displayed.
  // VK_PRESENT_MODE_FIFO_RELAXED_KHR is for applications that generally
  // render a new presentable image every refresh cycle, but are occasionally
  // late.  In this case (perhaps because of stuttering/latency concerns),
  // the application wants the late image to be immediately displayed, even
  // though that may mean some tearing.

  if (demo->presentMode != swapchainPresentMode) {
    for (size_t i = 0; i < presentModeCount; ++i) {
      if (presentModes[i] == demo->presentMode) {
        swapchainPresentMode = demo->presentMode;
        break;
      }
    }
  }
  if (swapchainPresentMode != demo->presentMode) {
    ERR_EXIT("Present mode specified is not supported\n",
             "Present mode unsupported");
  }

  // Determine the number of VkImage's to use in the swap chain.
  // Application desires to only acquire 1 image at a time (which is
  // "surfCapabilities.minImageCount").
  uint32_t desiredNumOfSwapchainImages = surfCapabilities.minImageCount;
  // If maxImageCount is 0, we can ask for as many images as we want;
  // otherwise we're limited to maxImageCount
  if ((surfCapabilities.maxImageCount > 0) &&
      (desiredNumOfSwapchainImages > surfCapabilities.maxImageCount)) {
    // Application must settle for fewer images than desired:
    desiredNumOfSwapchainImages = surfCapabilities.maxImageCount;
  }

  VkSurfaceTransformFlagBitsKHR preTransform;
  if (surfCapabilities.supportedTransforms &
      VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  } else {
    preTransform = surfCapabilities.currentTransform;
  }

  VkSwapchainCreateInfoKHR swapchain_ci = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext = NULL,
      .surface = demo->surface,
      .minImageCount = desiredNumOfSwapchainImages,
      .imageFormat = demo->format,
      .imageColorSpace = demo->color_space,
      .imageExtent =
          {
              .width = swapchainExtent.width,
              .height = swapchainExtent.height,
          },
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = preTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .imageArrayLayers = 1,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = NULL,
      .presentMode = swapchainPresentMode,
      .oldSwapchain = oldSwapchain,
      .clipped = true,
  };
  uint32_t i;
  err = demo->fpCreateSwapchainKHR(demo->device, &swapchain_ci, NULL,
                                   &demo->swapchain);
  assert(!err);

  // If we just re-created an existing swapchain, we should destroy the old
  // swapchain at this point.
  // Note: destroying the swapchain also cleans up all its associated
  // presentable images once the platform is done with them.
  if (oldSwapchain != VK_NULL_HANDLE) {
    demo->fpDestroySwapchainKHR(demo->device, oldSwapchain, NULL);
  }

  err = demo->fpGetSwapchainImagesKHR(demo->device, demo->swapchain,
                                      &demo->swapchainImageCount, NULL);
  assert(!err);

  VkImage* swapchainImages =
      (VkImage*)malloc(demo->swapchainImageCount * sizeof(VkImage));
  assert(swapchainImages);
  err = demo->fpGetSwapchainImagesKHR(demo->device, demo->swapchain,
                                      &demo->swapchainImageCount,
                                      swapchainImages);
  assert(!err);

  demo->buffers = (SwapchainBuffers*)malloc(sizeof(SwapchainBuffers) *
                                            demo->swapchainImageCount);
  assert(demo->buffers);

  for (i = 0; i < demo->swapchainImageCount; i++) {
    VkImageViewCreateInfo color_image_view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = NULL,
        .format = demo->format,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_R,
                .g = VK_COMPONENT_SWIZZLE_G,
                .b = VK_COMPONENT_SWIZZLE_B,
                .a = VK_COMPONENT_SWIZZLE_A,
            },
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .flags = 0,
    };

    demo->buffers[i].image = swapchainImages[i];

    color_image_view.image = demo->buffers[i].image;

    err = vkCreateImageView(demo->device, &color_image_view, NULL,
                            &demo->buffers[i].view);
    assert(!err);
  }

  if (NULL != presentModes) {
    free(presentModes);
  }
}

void demo_prepare_depth(struct demo* demo) {
  const VkFormat depth_format = VK_FORMAT_D16_UNORM;
  const VkImageCreateInfo image = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = NULL,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = depth_format,
      .extent = {demo->width, demo->height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .flags = 0,
  };

  VkImageViewCreateInfo view = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = NULL,
      .image = VK_NULL_HANDLE,
      .format = depth_format,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
      .flags = 0,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
  };

  VkMemoryRequirements mem_reqs;
  VkResult U_ASSERT_ONLY err;
  bool U_ASSERT_ONLY pass;

  demo->depth.format = depth_format;

  /* create image */
  err = vkCreateImage(demo->device, &image, NULL, &demo->depth.image);
  assert(!err);

  vkGetImageMemoryRequirements(demo->device, demo->depth.image, &mem_reqs);
  assert(!err);

  demo->depth.mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  demo->depth.mem_alloc.pNext = NULL;
  demo->depth.mem_alloc.allocationSize = mem_reqs.size;
  demo->depth.mem_alloc.memoryTypeIndex = 0;

  pass = memory_type_from_properties(demo, mem_reqs.memoryTypeBits,
                                     0, /* No requirements */
                                     &demo->depth.mem_alloc.memoryTypeIndex);
  assert(pass);

  /* allocate memory */
  err = vkAllocateMemory(demo->device, &demo->depth.mem_alloc, NULL,
                         &demo->depth.mem);
  assert(!err);

  /* bind memory */
  err = vkBindImageMemory(demo->device, demo->depth.image, demo->depth.mem, 0);
  assert(!err);

  /* create image view */
  view.image = demo->depth.image;
  err = vkCreateImageView(demo->device, &view, NULL, &demo->depth.view);
  assert(!err);
}

#include "garnet/lib/vulkan/tests/vkcube/magma.ppm.h"

/* Load a ppm file into memory */
bool loadTextureRGBA(const char* filename, uint8_t* rgba_data,
                     uint32_t data_size, VkSubresourceLayout* layout,
                     uint32_t* width, uint32_t* height) {
  // #include "lunarg.ppm.h"
  //     constexpr bool kDataIsAscii = false;
  //     const char* ppm = reinterpret_cast<const char*>(lunarg_ppm);
  //     const uint32_t len = lunarg_ppm_len;//sizeof(lunarg_ppm);

  constexpr bool kDataIsAscii = true;
  const char* ppm = magma_ppm;
  const uint32_t len = sizeof(magma_ppm);

  const char* cPtr = ppm;
  if (cPtr >= (ppm + len) ||
      (strncmp(cPtr, "P6\n", 3) && strncmp(cPtr, "P3\n", 3))) {
    fprintf(stderr, "ppm invalid %x %x %x\n", cPtr[0], cPtr[1], cPtr[2]);
    return false;
  }
  while (strncmp(cPtr++, "\n", 1))
    ;
  sscanf(cPtr, "%u %u", width, height);

  if (rgba_data == NULL) {
    return true;
  }
  while (strncmp(cPtr++, "\n", 1))
    ;

  if (cPtr >= (ppm + len) || strncmp(cPtr, "255\n", 4)) {
    fprintf(stderr, "ppm invalid %x %x %x %x\n", cPtr[0], cPtr[1], cPtr[2],
            cPtr[3]);
    return false;
  }

  while (strncmp(cPtr++, "\n", 1))
    ;

  for (uint32_t y = 0; y < *height; y++) {
    uint8_t* rowPtr = rgba_data;
    for (uint32_t x = 0; x < *width; x++) {
      if (kDataIsAscii) {
        sscanf(cPtr, "%hhu ", &rowPtr[0]);
        while (strncmp(cPtr++, " ", 1))
          ;
        sscanf(cPtr, "%hhu ", &rowPtr[1]);
        while (strncmp(cPtr++, " ", 1))
          ;
        sscanf(cPtr, "%hhu ", &rowPtr[2]);
        while (strncmp(cPtr++, " ", 1))
          ;
        if (*cPtr == '\n')
          cPtr++;
      } else {
        memcpy(rowPtr, cPtr, 3);
        cPtr += 3;
      }
      rowPtr[3] = 255; /* Alpha of 1 */
      rowPtr += 4;
    }
    rgba_data += layout->rowPitch;
  }

  return true;
}

bool loadTextureYUYV422(const char* filename, uint8_t* rgba_data,
                        uint32_t data_size, VkSubresourceLayout* layout,
                        uint32_t* width, uint32_t* height) {
  *width = 320;
  *height = 180;

  if (!rgba_data)
    return true;

  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    printf("failed to open: %s\n", filename);
    return false;
  }

  printf("reading %u bytes\n", data_size);
  ssize_t ret = read(fd, rgba_data, data_size);
  close(fd);

  if (ret != data_size) {
    printf("failed to read: %zd\n", ret);
    return false;
  }

  return true;
}

#if USE_YUV_TEXTURE
auto loadTexture = loadTextureYUYV422;
#else
auto loadTexture = loadTextureRGBA;
#endif

static void demo_prepare_texture_image(struct demo* demo, const char* filename,
                                       struct texture_object* tex_obj,
                                       VkImageTiling tiling,
                                       VkImageUsageFlags usage,
                                       VkFlags required_props) {
  const VkFormat tex_format = TEX_FORMAT;
  uint32_t tex_width;
  uint32_t tex_height;
  VkResult U_ASSERT_ONLY err;
  bool U_ASSERT_ONLY pass;

  if (!loadTexture(filename, NULL, 0, NULL, &tex_width, &tex_height)) {
    ERR_EXIT("Failed to load textures", "Load Texture Failure");
  }

  tex_obj->tex_width = tex_width;
  tex_obj->tex_height = tex_height;

  const VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = NULL,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = tex_format,
      .extent = {tex_width, tex_height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = tiling,
      .usage = usage,
      .flags = 0,
      .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
  };

  VkMemoryRequirements mem_reqs;

  err = vkCreateImage(demo->device, &image_create_info, NULL, &tex_obj->image);
  assert(!err);

  vkGetImageMemoryRequirements(demo->device, tex_obj->image, &mem_reqs);

  tex_obj->mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  tex_obj->mem_alloc.pNext = NULL;
  tex_obj->mem_alloc.allocationSize = mem_reqs.size;
  tex_obj->mem_alloc.memoryTypeIndex = 0;

  pass =
      memory_type_from_properties(demo, mem_reqs.memoryTypeBits, required_props,
                                  &tex_obj->mem_alloc.memoryTypeIndex);
  assert(pass);

  /* allocate memory */
  err = vkAllocateMemory(demo->device, &tex_obj->mem_alloc, NULL,
                         &(tex_obj->mem));
  assert(!err);

  /* bind memory */
  err = vkBindImageMemory(demo->device, tex_obj->image, tex_obj->mem, 0);
  assert(!err);

  if (required_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    const VkImageSubresource subres = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .arrayLayer = 0,
    };
    VkSubresourceLayout layout;
    void* data;

    vkGetImageSubresourceLayout(demo->device, tex_obj->image, &subres, &layout);

    err = vkMapMemory(demo->device, tex_obj->mem, 0,
                      tex_obj->mem_alloc.allocationSize, 0, &data);
    assert(!err);

    if (!loadTexture(filename, reinterpret_cast<uint8_t*>(data), mem_reqs.size,
                     &layout, &tex_width, &tex_height)) {
      fprintf(stderr, "Error loading texture: %s\n", filename);
    }

    vkUnmapMemory(demo->device, tex_obj->mem);
  }

  tex_obj->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

static void demo_destroy_texture_image(struct demo* demo,
                                       struct texture_object* tex_objs) {
  /* clean up staging resources */
  vkFreeMemory(demo->device, tex_objs->mem, NULL);
  vkDestroyImage(demo->device, tex_objs->image, NULL);
}

static void demo_prepare_textures(struct demo* demo) {
  const VkFormat tex_format = TEX_FORMAT;
  VkFormatProperties props;
  uint32_t i;

  vkGetPhysicalDeviceFormatProperties(demo->gpu, tex_format, &props);

  for (i = 0; i < DEMO_TEXTURE_COUNT; i++) {
    VkResult U_ASSERT_ONLY err;

    if ((props.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) &&
        !demo->use_staging_buffer) {
      /* Device can texture using linear textures */
      demo_prepare_texture_image(demo, tex_files[i], &demo->textures[i],
                                 VK_IMAGE_TILING_LINEAR,
                                 VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      // Nothing in the pipeline needs to be complete to start, and don't allow
      // fragment shader to run until layout transition completes
      demo_set_image_layout(
          demo, demo->textures[i].image, VK_IMAGE_ASPECT_COLOR_BIT,
          VK_IMAGE_LAYOUT_PREINITIALIZED, demo->textures[i].imageLayout,
          VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      demo->staging_texture.image = 0;
    } else if (props.optimalTilingFeatures &
               VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
      /* Must use staging buffer to copy linear texture to optimized */

      memset(&demo->staging_texture, 0, sizeof(demo->staging_texture));
      demo_prepare_texture_image(demo, tex_files[i], &demo->staging_texture,
                                 VK_IMAGE_TILING_LINEAR,
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

      demo_prepare_texture_image(
          demo, tex_files[i], &demo->textures[i], VK_IMAGE_TILING_OPTIMAL,
          (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      demo_set_image_layout(
          demo, demo->staging_texture.image, VK_IMAGE_ASPECT_COLOR_BIT,
          VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT);

      demo_set_image_layout(
          demo, demo->textures[i].image, VK_IMAGE_ASPECT_COLOR_BIT,
          VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT);

      VkImageCopy copy_region = {
          .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
          .srcOffset = {0, 0, 0},
          .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
          .dstOffset = {0, 0, 0},
          .extent = {demo->staging_texture.tex_width,
                     demo->staging_texture.tex_height, 1},
      };
      vkCmdCopyImage(demo->cmd, demo->staging_texture.image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     demo->textures[i].image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

      demo_set_image_layout(
          demo, demo->textures[i].image, VK_IMAGE_ASPECT_COLOR_BIT,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, demo->textures[i].imageLayout,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    } else {
      /* Can't support VK_FORMAT_R8G8B8A8_UNORM !? */
      assert(!"No support for R8G8B8A8_UNORM as texture image format");
    }

#if USE_YUV_TEXTURE
    VkSamplerYcbcrConversionCreateInfoKHR sampler_ycbcr_conversion_create_info =
        {.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO_KHR,
         .pNext = NULL,
         .format = tex_format,
         .ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709_KHR,
         .ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW_KHR,
         .components =
             {
                 VK_COMPONENT_SWIZZLE_IDENTITY,  // R
                 VK_COMPONENT_SWIZZLE_IDENTITY,  // G
                 VK_COMPONENT_SWIZZLE_IDENTITY,  // B
                 VK_COMPONENT_SWIZZLE_IDENTITY,  // A
             },
         .xChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN_KHR,
         .yChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN_KHR,
         .chromaFilter = VK_FILTER_LINEAR,
         .forceExplicitReconstruction = VK_FALSE};
    VkSamplerYcbcrConversionInfoKHR ycbcr_conversion_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO_KHR,
        .pNext = NULL,
        .conversion = 0  // overwritten
    };

    err = demo->fpCreateSamplerYcbcrConversionKHR(
        demo->device, &sampler_ycbcr_conversion_create_info, NULL,
        &ycbcr_conversion_info.conversion);
    assert(!err);

    void* extension_ptr = &ycbcr_conversion_info;
#else
    void* extension_ptr = NULL;
#endif

    const VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = extension_ptr,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1,
        .compareOp = VK_COMPARE_OP_NEVER,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkImageViewCreateInfo view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = extension_ptr,
        .image = VK_NULL_HANDLE,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = tex_format,
        .components =
            {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A,
            },
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .flags = 0,
    };

    /* create sampler */
    err = vkCreateSampler(demo->device, &sampler, NULL,
                          &demo->textures[i].sampler);
    assert(!err);

    /* create image view */
    view.image = demo->textures[i].image;
    err = vkCreateImageView(demo->device, &view, NULL, &demo->textures[i].view);
    assert(!err);
  }
}

void demo_prepare_cube_data_buffer(struct demo* demo) {
  VkBufferCreateInfo buf_info;
  VkMemoryRequirements mem_reqs;
  uint8_t* pData;
  int i;
  mat4x4 MVP, VP;
  VkResult U_ASSERT_ONLY err;
  bool U_ASSERT_ONLY pass;
  struct vktexcube_vs_uniform data;

  mat4x4_mul(VP, demo->projection_matrix, demo->view_matrix);
  mat4x4_mul(MVP, VP, demo->model_matrix);
  memcpy(data.mvp, MVP, sizeof(MVP));
  //    dumpMatrix("MVP", MVP);

  for (i = 0; i < 12 * 3; i++) {
    data.position[i][0] = g_vertex_buffer_data[i * 3];
    data.position[i][1] = g_vertex_buffer_data[i * 3 + 1];
    data.position[i][2] = g_vertex_buffer_data[i * 3 + 2];
    data.position[i][3] = 1.0f;
    data.attr[i][0] = g_uv_buffer_data[2 * i];
    data.attr[i][1] = g_uv_buffer_data[2 * i + 1];
    data.attr[i][2] = 0;
    data.attr[i][3] = 0;
  }

  memset(&buf_info, 0, sizeof(buf_info));
  buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buf_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  buf_info.size = sizeof(data);
  err = vkCreateBuffer(demo->device, &buf_info, NULL, &demo->uniform_data.buf);
  assert(!err);

  vkGetBufferMemoryRequirements(demo->device, demo->uniform_data.buf,
                                &mem_reqs);

  demo->uniform_data.mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  demo->uniform_data.mem_alloc.pNext = NULL;
  demo->uniform_data.mem_alloc.allocationSize = mem_reqs.size;
  demo->uniform_data.mem_alloc.memoryTypeIndex = 0;

  pass = memory_type_from_properties(
      demo, mem_reqs.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      &demo->uniform_data.mem_alloc.memoryTypeIndex);
  assert(pass);

  err = vkAllocateMemory(demo->device, &demo->uniform_data.mem_alloc, NULL,
                         &(demo->uniform_data.mem));
  assert(!err);

  err = vkMapMemory(demo->device, demo->uniform_data.mem, 0,
                    demo->uniform_data.mem_alloc.allocationSize, 0,
                    (void**)&pData);
  assert(!err);

  memcpy(pData, &data, sizeof data);

  vkUnmapMemory(demo->device, demo->uniform_data.mem);

  err = vkBindBufferMemory(demo->device, demo->uniform_data.buf,
                           demo->uniform_data.mem, 0);
  assert(!err);

  demo->uniform_data.buffer_info.buffer = demo->uniform_data.buf;
  demo->uniform_data.buffer_info.offset = 0;
  demo->uniform_data.buffer_info.range = sizeof(data);
}

static void demo_prepare_descriptor_layout(struct demo* demo) {
  const VkDescriptorSetLayoutBinding layout_bindings[2] = {
      [0] =
          {
              .binding = 0,
              .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .descriptorCount = 1,
              .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
              .pImmutableSamplers = NULL,
          },
      [1] =
          {
              .binding = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .descriptorCount = DEMO_TEXTURE_COUNT,
              .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
#if USE_YUV_TEXTURE
              .pImmutableSamplers = &demo->textures[0].sampler,
#else
              .pImmutableSamplers = NULL,
#endif
          },
  };
  const VkDescriptorSetLayoutCreateInfo descriptor_layout = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .bindingCount = 2,
      .pBindings = layout_bindings,
  };
  VkResult U_ASSERT_ONLY err;

  err = vkCreateDescriptorSetLayout(demo->device, &descriptor_layout, NULL,
                                    &demo->desc_layout);
  assert(!err);

  const VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .setLayoutCount = 1,
      .pSetLayouts = &demo->desc_layout,
  };

  err = vkCreatePipelineLayout(demo->device, &pPipelineLayoutCreateInfo, NULL,
                               &demo->pipeline_layout);
  assert(!err);
}

static void demo_prepare_render_pass(struct demo* demo) {
  // The initial layout for the color and depth attachments will be
  // LAYOUT_UNDEFINED because at the start of the renderpass, we don't care
  // about their contents. At the start of the subpass, the color attachment's
  // layout will be transitioned to LAYOUT_COLOR_ATTACHMENT_OPTIMAL and the
  // depth stencil attachment's layout will be transitioned to
  // LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL.  At the end of the renderpass, the
  // color attachment's layout will be transitioned to LAYOUT_PRESENT_SRC_KHR to
  // be ready to present.  This is all done as part of the renderpass, no
  // barriers are necessary.
  const VkAttachmentDescription attachments[2] = {
      [0] =
          {
              .format = demo->format,
              .samples = VK_SAMPLE_COUNT_1_BIT,
              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
              .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          },
      [1] =
          {
              .format = demo->depth.format,
              .samples = VK_SAMPLE_COUNT_1_BIT,
              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
              .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          },
  };
  const VkAttachmentReference color_reference = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  const VkAttachmentReference depth_reference = {
      .attachment = 1,
      .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  };
  const VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .flags = 0,
      .inputAttachmentCount = 0,
      .pInputAttachments = NULL,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_reference,
      .pResolveAttachments = NULL,
      .pDepthStencilAttachment = &depth_reference,
      .preserveAttachmentCount = 0,
      .pPreserveAttachments = NULL,
  };
  const VkRenderPassCreateInfo rp_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = NULL,
      .attachmentCount = 2,
      .pAttachments = attachments,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 0,
      .pDependencies = NULL,
  };
  VkResult U_ASSERT_ONLY err;

  err = vkCreateRenderPass(demo->device, &rp_info, NULL, &demo->render_pass);
  assert(!err);
}

static VkShaderModule demo_prepare_vs(struct demo* demo) {
  VkShaderModuleCreateInfo sh_info = {};
  sh_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

#include "cube.vert.h"
  sh_info.codeSize = sizeof(cube_vert);
  sh_info.pCode = cube_vert;
  VkResult U_ASSERT_ONLY err = vkCreateShaderModule(
      demo->device, &sh_info, NULL, &demo->vert_shader_module);
  assert(!err);

  return demo->vert_shader_module;
}

static VkShaderModule demo_prepare_fs(struct demo* demo) {
  VkShaderModuleCreateInfo sh_info = {};
  sh_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

#include "cube.frag.h"
  sh_info.codeSize = sizeof(cube_frag);
  sh_info.pCode = cube_frag;
  VkResult U_ASSERT_ONLY err = vkCreateShaderModule(
      demo->device, &sh_info, NULL, &demo->frag_shader_module);
  assert(!err);

  return demo->frag_shader_module;
}

static void demo_prepare_pipeline(struct demo* demo) {
  VkGraphicsPipelineCreateInfo pipeline;
  VkPipelineCacheCreateInfo pipelineCache;
  VkPipelineVertexInputStateCreateInfo vi;
  VkPipelineInputAssemblyStateCreateInfo ia;
  VkPipelineRasterizationStateCreateInfo rs;
  VkPipelineColorBlendStateCreateInfo cb;
  VkPipelineDepthStencilStateCreateInfo ds;
  VkPipelineViewportStateCreateInfo vp;
  VkPipelineMultisampleStateCreateInfo ms;
  VkDynamicState dynamicStateEnables[VK_DYNAMIC_STATE_RANGE_SIZE];
  VkPipelineDynamicStateCreateInfo dynamicState;
  VkResult U_ASSERT_ONLY err;

  memset(dynamicStateEnables, 0, sizeof dynamicStateEnables);
  memset(&dynamicState, 0, sizeof dynamicState);
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.pDynamicStates = dynamicStateEnables;

  memset(&pipeline, 0, sizeof(pipeline));
  pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline.layout = demo->pipeline_layout;

  memset(&vi, 0, sizeof(vi));
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  memset(&ia, 0, sizeof(ia));
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  memset(&rs, 0, sizeof(rs));
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_BACK_BIT;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.depthClampEnable = VK_FALSE;
  rs.rasterizerDiscardEnable = VK_FALSE;
  rs.depthBiasEnable = VK_FALSE;
  rs.lineWidth = 1.0f;

  memset(&cb, 0, sizeof(cb));
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  VkPipelineColorBlendAttachmentState att_state[1];
  memset(att_state, 0, sizeof(att_state));
  att_state[0].colorWriteMask = 0xf;
  att_state[0].blendEnable = VK_FALSE;
  cb.attachmentCount = 1;
  cb.pAttachments = att_state;

  memset(&vp, 0, sizeof(vp));
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  dynamicStateEnables[dynamicState.dynamicStateCount++] =
      VK_DYNAMIC_STATE_VIEWPORT;
  vp.scissorCount = 1;
  dynamicStateEnables[dynamicState.dynamicStateCount++] =
      VK_DYNAMIC_STATE_SCISSOR;

  memset(&ds, 0, sizeof(ds));
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  ds.depthBoundsTestEnable = VK_FALSE;
  ds.back.failOp = VK_STENCIL_OP_KEEP;
  ds.back.passOp = VK_STENCIL_OP_KEEP;
  ds.back.compareOp = VK_COMPARE_OP_ALWAYS;
  ds.stencilTestEnable = VK_FALSE;
  ds.front = ds.back;

  memset(&ms, 0, sizeof(ms));
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.pSampleMask = NULL;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Two stages: vs and fs
  pipeline.stageCount = 2;
  VkPipelineShaderStageCreateInfo shaderStages[2];
  memset(&shaderStages, 0, 2 * sizeof(VkPipelineShaderStageCreateInfo));

  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = demo_prepare_vs(demo);
  shaderStages[0].pName = "main";

  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = demo_prepare_fs(demo);
  shaderStages[1].pName = "main";

  memset(&pipelineCache, 0, sizeof(pipelineCache));
  pipelineCache.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

  err = vkCreatePipelineCache(demo->device, &pipelineCache, NULL,
                              &demo->pipelineCache);
  assert(!err);

  pipeline.pVertexInputState = &vi;
  pipeline.pInputAssemblyState = &ia;
  pipeline.pRasterizationState = &rs;
  pipeline.pColorBlendState = &cb;
  pipeline.pMultisampleState = &ms;
  pipeline.pViewportState = &vp;
  pipeline.pDepthStencilState = &ds;
  pipeline.pStages = shaderStages;
  pipeline.renderPass = demo->render_pass;
  pipeline.pDynamicState = &dynamicState;

  pipeline.renderPass = demo->render_pass;

  err = vkCreateGraphicsPipelines(demo->device, demo->pipelineCache, 1,
                                  &pipeline, NULL, &demo->pipeline);
  assert(!err);

  vkDestroyShaderModule(demo->device, demo->frag_shader_module, NULL);
  vkDestroyShaderModule(demo->device, demo->vert_shader_module, NULL);
}

static void demo_prepare_descriptor_pool(struct demo* demo) {
  const VkDescriptorPoolSize type_counts[2] = {
      [0] =
          {
              .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .descriptorCount = 1,
          },
      [1] =
          {
              .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .descriptorCount = DEMO_TEXTURE_COUNT,
          },
  };
  const VkDescriptorPoolCreateInfo descriptor_pool = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = NULL,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = type_counts,
  };
  VkResult U_ASSERT_ONLY err;

  err = vkCreateDescriptorPool(demo->device, &descriptor_pool, NULL,
                               &demo->desc_pool);
  assert(!err);
}

static void demo_prepare_descriptor_set(struct demo* demo) {
  VkDescriptorImageInfo tex_descs[DEMO_TEXTURE_COUNT];
  VkWriteDescriptorSet writes[2];
  VkResult U_ASSERT_ONLY err;
  uint32_t i;

  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = NULL,
      .descriptorPool = demo->desc_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &demo->desc_layout};
  err = vkAllocateDescriptorSets(demo->device, &alloc_info, &demo->desc_set);
  assert(!err);

  memset(&tex_descs, 0, sizeof(tex_descs));
  for (i = 0; i < DEMO_TEXTURE_COUNT; i++) {
    tex_descs[i].sampler = demo->textures[i].sampler;
    tex_descs[i].imageView = demo->textures[i].view;
    tex_descs[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  }

  memset(&writes, 0, sizeof(writes));

  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = demo->desc_set;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[0].pBufferInfo = &demo->uniform_data.buffer_info;

  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = demo->desc_set;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = DEMO_TEXTURE_COUNT;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[1].pImageInfo = tex_descs;

  vkUpdateDescriptorSets(demo->device, 2, writes, 0, NULL);
}

static void demo_prepare_framebuffers(struct demo* demo) {
  VkImageView attachments[2];
  attachments[1] = demo->depth.view;

  const VkFramebufferCreateInfo fb_info = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = NULL,
      .renderPass = demo->render_pass,
      .attachmentCount = 2,
      .pAttachments = attachments,
      .width = demo->width,
      .height = demo->height,
      .layers = 1,
  };
  VkResult U_ASSERT_ONLY err;
  uint32_t i;

  demo->framebuffers =
      (VkFramebuffer*)malloc(demo->swapchainImageCount * sizeof(VkFramebuffer));
  assert(demo->framebuffers);

  for (i = 0; i < demo->swapchainImageCount; i++) {
    attachments[0] = demo->buffers[i].view;
    err = vkCreateFramebuffer(demo->device, &fb_info, NULL,
                              &demo->framebuffers[i]);
    assert(!err);
  }
}

void demo_prepare(struct demo* demo) {
  VkResult U_ASSERT_ONLY err;

  const VkCommandPoolCreateInfo cmd_pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = NULL,
      .queueFamilyIndex = demo->graphics_queue_family_index,
      .flags = 0,
  };
  err =
      vkCreateCommandPool(demo->device, &cmd_pool_info, NULL, &demo->cmd_pool);
  assert(!err);

  const VkCommandBufferAllocateInfo cmd = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = NULL,
      .commandPool = demo->cmd_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  err = vkAllocateCommandBuffers(demo->device, &cmd, &demo->cmd);
  assert(!err);
  VkCommandBufferBeginInfo cmd_buf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = NULL,
      .flags = 0,
      .pInheritanceInfo = NULL,
  };
  err = vkBeginCommandBuffer(demo->cmd, &cmd_buf_info);
  assert(!err);

  demo_prepare_buffers(demo);
  demo_prepare_depth(demo);
  demo_prepare_textures(demo);
  demo_prepare_cube_data_buffer(demo);

  demo_prepare_descriptor_layout(demo);
  demo_prepare_render_pass(demo);
  demo_prepare_pipeline(demo);

  for (uint32_t i = 0; i < demo->swapchainImageCount; i++) {
    err = vkAllocateCommandBuffers(demo->device, &cmd, &demo->buffers[i].cmd);
    assert(!err);
  }

  if (demo->separate_present_queue) {
    const VkCommandPoolCreateInfo cmd_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .queueFamilyIndex = demo->present_queue_family_index,
        .flags = 0,
    };
    err = vkCreateCommandPool(demo->device, &cmd_pool_info, NULL,
                              &demo->present_cmd_pool);
    assert(!err);
    const VkCommandBufferAllocateInfo cmd = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = demo->present_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    for (uint32_t i = 0; i < demo->swapchainImageCount; i++) {
      err = vkAllocateCommandBuffers(demo->device, &cmd,
                                     &demo->buffers[i].graphics_to_present_cmd);
      assert(!err);
      demo_build_image_ownership_cmd(demo, i);
    }
  }

  demo_prepare_descriptor_pool(demo);
  demo_prepare_descriptor_set(demo);

  demo_prepare_framebuffers(demo);

  for (uint32_t i = 0; i < demo->swapchainImageCount; i++) {
    demo->current_buffer = i;
    demo_draw_build_cmd(demo, demo->buffers[i].cmd);
  }

  /*
   * Prepare functions above may generate pipeline commands
   * that need to be flushed before beginning the render loop.
   */
  demo_flush_init_cmd(demo);
  if (demo->staging_texture.image) {
    demo_destroy_texture_image(demo, &demo->staging_texture);
  }

  demo->current_buffer = 0;
  demo->prepared = true;
}

void demo_cleanup(struct demo* demo) {
  uint32_t i;

  demo->prepared = false;
  vkDeviceWaitIdle(demo->device);

  // Wait for fences from present operations
  for (i = 0; i < FRAME_LAG; i++) {
    vkWaitForFences(demo->device, 1, &demo->fences[i], VK_TRUE, UINT64_MAX);
    vkDestroyFence(demo->device, demo->fences[i], NULL);
    vkDestroySemaphore(demo->device, demo->image_acquired_semaphores[i], NULL);
    vkDestroySemaphore(demo->device, demo->draw_complete_semaphores[i], NULL);
    if (demo->separate_present_queue) {
      vkDestroySemaphore(demo->device, demo->image_ownership_semaphores[i],
                         NULL);
    }
  }

  for (i = 0; i < demo->swapchainImageCount; i++) {
    vkDestroyFramebuffer(demo->device, demo->framebuffers[i], NULL);
  }
  free(demo->framebuffers);
  vkDestroyDescriptorPool(demo->device, demo->desc_pool, NULL);

  vkDestroyPipeline(demo->device, demo->pipeline, NULL);
  vkDestroyPipelineCache(demo->device, demo->pipelineCache, NULL);
  vkDestroyRenderPass(demo->device, demo->render_pass, NULL);
  vkDestroyPipelineLayout(demo->device, demo->pipeline_layout, NULL);
  vkDestroyDescriptorSetLayout(demo->device, demo->desc_layout, NULL);

  for (i = 0; i < DEMO_TEXTURE_COUNT; i++) {
    vkDestroyImageView(demo->device, demo->textures[i].view, NULL);
    vkDestroyImage(demo->device, demo->textures[i].image, NULL);
    vkFreeMemory(demo->device, demo->textures[i].mem, NULL);
    vkDestroySampler(demo->device, demo->textures[i].sampler, NULL);
  }
  demo->fpDestroySwapchainKHR(demo->device, demo->swapchain, NULL);

  vkDestroyImageView(demo->device, demo->depth.view, NULL);
  vkDestroyImage(demo->device, demo->depth.image, NULL);
  vkFreeMemory(demo->device, demo->depth.mem, NULL);

  vkDestroyBuffer(demo->device, demo->uniform_data.buf, NULL);
  vkFreeMemory(demo->device, demo->uniform_data.mem, NULL);

  for (i = 0; i < demo->swapchainImageCount; i++) {
    vkDestroyImageView(demo->device, demo->buffers[i].view, NULL);
    vkFreeCommandBuffers(demo->device, demo->cmd_pool, 1,
                         &demo->buffers[i].cmd);
  }
  free(demo->buffers);
  free(demo->queue_props);
  vkDestroyCommandPool(demo->device, demo->cmd_pool, NULL);

  if (demo->separate_present_queue) {
    vkDestroyCommandPool(demo->device, demo->present_cmd_pool, NULL);
  }
  vkDestroyDevice(demo->device, NULL);
  if (demo->validate) {
    demo->DestroyDebugReportCallback(demo->inst, demo->msg_callback, NULL);
  }
  vkDestroySurfaceKHR(demo->inst, demo->surface, NULL);
  vkDestroyInstance(demo->inst, NULL);

#if defined(VK_USE_PLATFORM_XLIB_KHR)
  if (demo->use_xlib) {
    XDestroyWindow(demo->display, demo->xlib_window);
    XCloseDisplay(demo->display);
  } else {
    xcb_destroy_window(demo->connection, demo->xcb_window);
    xcb_disconnect(demo->connection);
  }
  free(demo->atom_wm_delete_window);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
  xcb_destroy_window(demo->connection, demo->xcb_window);
  xcb_disconnect(demo->connection);
  free(demo->atom_wm_delete_window);
#endif
}

static void demo_resize(struct demo* demo) {
  uint32_t i;

  // Don't react to resize until after first initialization.
  if (!demo->prepared) {
    return;
  }
  // In order to properly resize the window, we must re-create the swapchain
  // AND redo the command buffers, etc.
  //
  // First, perform part of the demo_cleanup() function:
  demo->prepared = false;
  vkDeviceWaitIdle(demo->device);

  for (i = 0; i < demo->swapchainImageCount; i++) {
    vkDestroyFramebuffer(demo->device, demo->framebuffers[i], NULL);
  }
  free(demo->framebuffers);
  vkDestroyDescriptorPool(demo->device, demo->desc_pool, NULL);

  vkDestroyPipeline(demo->device, demo->pipeline, NULL);
  vkDestroyPipelineCache(demo->device, demo->pipelineCache, NULL);
  vkDestroyRenderPass(demo->device, demo->render_pass, NULL);
  vkDestroyPipelineLayout(demo->device, demo->pipeline_layout, NULL);
  vkDestroyDescriptorSetLayout(demo->device, demo->desc_layout, NULL);

  for (i = 0; i < DEMO_TEXTURE_COUNT; i++) {
    vkDestroyImageView(demo->device, demo->textures[i].view, NULL);
    vkDestroyImage(demo->device, demo->textures[i].image, NULL);
    vkFreeMemory(demo->device, demo->textures[i].mem, NULL);
    vkDestroySampler(demo->device, demo->textures[i].sampler, NULL);
  }

  vkDestroyImageView(demo->device, demo->depth.view, NULL);
  vkDestroyImage(demo->device, demo->depth.image, NULL);
  vkFreeMemory(demo->device, demo->depth.mem, NULL);

  vkDestroyBuffer(demo->device, demo->uniform_data.buf, NULL);
  vkFreeMemory(demo->device, demo->uniform_data.mem, NULL);

  for (i = 0; i < demo->swapchainImageCount; i++) {
    vkDestroyImageView(demo->device, demo->buffers[i].view, NULL);
    vkFreeCommandBuffers(demo->device, demo->cmd_pool, 1,
                         &demo->buffers[i].cmd);
  }
  vkDestroyCommandPool(demo->device, demo->cmd_pool, NULL);
  if (demo->separate_present_queue) {
    vkDestroyCommandPool(demo->device, demo->present_cmd_pool, NULL);
  }
  free(demo->buffers);

  // Second, re-perform the demo_prepare() function, which will re-create the
  // swapchain:
  demo_prepare(demo);
}

// On MS-Windows, make this a global, so it's available to WndProc()
struct demo demo;

#if defined(VK_USE_PLATFORM_XLIB_KHR)
static void demo_create_xlib_window(struct demo* demo) {
  demo->display = XOpenDisplay(NULL);
  long visualMask = VisualScreenMask;
  int numberOfVisuals;
  XVisualInfo vInfoTemplate = {};
  vInfoTemplate.screen = DefaultScreen(demo->display);
  XVisualInfo* visualInfo = XGetVisualInfo(demo->display, visualMask,
                                           &vInfoTemplate, &numberOfVisuals);

  Colormap colormap = XCreateColormap(
      demo->display, RootWindow(demo->display, vInfoTemplate.screen),
      visualInfo->visual, AllocNone);

  XSetWindowAttributes windowAttributes = {};
  windowAttributes.colormap = colormap;
  windowAttributes.background_pixel = 0xFFFFFFFF;
  windowAttributes.border_pixel = 0;
  windowAttributes.event_mask =
      KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;

  demo->xlib_window = XCreateWindow(
      demo->display, RootWindow(demo->display, vInfoTemplate.screen), 0, 0,
      demo->width, demo->height, 0, visualInfo->depth, InputOutput,
      visualInfo->visual,
      CWBackPixel | CWBorderPixel | CWEventMask | CWColormap,
      &windowAttributes);

  XSelectInput(demo->display, demo->xlib_window, ExposureMask | KeyPressMask);
  XMapWindow(demo->display, demo->xlib_window);
  XFlush(demo->display);
  demo->xlib_wm_delete_window =
      XInternAtom(demo->display, "WM_DELETE_WINDOW", False);
}
static void demo_handle_xlib_event(struct demo* demo, const XEvent* event) {
  switch (event->type) {
    case ClientMessage:
      if ((Atom)event->xclient.data.l[0] == demo->xlib_wm_delete_window)
        demo->quit = true;
      break;
    case KeyPress:
      switch (event->xkey.keycode) {
        case 0x9:  // Escape
          demo->quit = true;
          break;
        case 0x71:  // left arrow key
          demo->spin_angle += demo->spin_increment;
          break;
        case 0x72:  // right arrow key
          demo->spin_angle -= demo->spin_increment;
          break;
        case 0x41:
          demo->pause = !demo->pause;
          break;
      }
      break;
    case ConfigureNotify:
      if ((demo->width != event->xconfigure.width) ||
          (demo->height != event->xconfigure.height)) {
        demo->width = event->xconfigure.width;
        demo->height = event->xconfigure.height;
        demo_resize(demo);
      }
      break;
    default:
      break;
  }
}

static void demo_run_xlib(struct demo* demo) {
  while (!demo->quit) {
    XEvent event;

    if (demo->pause) {
      XNextEvent(demo->display, &event);
      demo_handle_xlib_event(demo, &event);
    } else {
      while (XPending(demo->display) > 0) {
        XNextEvent(demo->display, &event);
        demo_handle_xlib_event(demo, &event);
      }
    }

    demo_draw(demo);
    demo->curFrame++;
    if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount)
      demo->quit = true;
  }
}
#endif  // VK_USE_PLATFORM_XLIB_KHR

#ifdef VK_USE_PLATFORM_XCB_KHR
static void demo_handle_xcb_event(struct demo* demo,
                                  const xcb_generic_event_t* event) {
  uint8_t event_code = event->response_type & 0x7f;
  switch (event_code) {
    case XCB_EXPOSE:
      // TODO: Resize window
      break;
    case XCB_CLIENT_MESSAGE:
      if ((*(xcb_client_message_event_t*)event).data.data32[0] ==
          (*demo->atom_wm_delete_window).atom) {
        demo->quit = true;
      }
      break;
    case XCB_KEY_RELEASE: {
      const xcb_key_release_event_t* key =
          (const xcb_key_release_event_t*)event;

      switch (key->detail) {
        case 0x9:  // Escape
          demo->quit = true;
          break;
        case 0x71:  // left arrow key
          demo->spin_angle += demo->spin_increment;
          break;
        case 0x72:  // right arrow key
          demo->spin_angle -= demo->spin_increment;
          break;
        case 0x41:
          demo->pause = !demo->pause;
          break;
      }
    } break;
    case XCB_CONFIGURE_NOTIFY: {
      const xcb_configure_notify_event_t* cfg =
          (const xcb_configure_notify_event_t*)event;
      if ((demo->width != cfg->width) || (demo->height != cfg->height)) {
        demo->width = cfg->width;
        demo->height = cfg->height;
        demo_resize(demo);
      }
    } break;
    default:
      break;
  }
}

static void demo_run_xcb(struct demo* demo) {
  xcb_flush(demo->connection);

  while (!demo->quit) {
    xcb_generic_event_t* event;

    if (demo->pause) {
      event = xcb_wait_for_event(demo->connection);
    } else {
      event = xcb_poll_for_event(demo->connection);
      while (event) {
        demo_handle_xcb_event(demo, event);
        free(event);
        event = xcb_poll_for_event(demo->connection);
      }
    }

    demo_draw(demo);
    demo->curFrame++;
    if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount)
      demo->quit = true;
  }
}

static void demo_create_xcb_window(struct demo* demo) {
  uint32_t value_mask, value_list[32];

  demo->xcb_window = xcb_generate_id(demo->connection);

  value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  value_list[0] = demo->screen->black_pixel;
  value_list[1] = XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE |
                  XCB_EVENT_MASK_STRUCTURE_NOTIFY;

  xcb_create_window(demo->connection, XCB_COPY_FROM_PARENT, demo->xcb_window,
                    demo->screen->root, 0, 0, demo->width, demo->height, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, demo->screen->root_visual,
                    value_mask, value_list);

  /* Magic code that will send notification when window is destroyed */
  xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(demo->connection, 1, 12, "WM_PROTOCOLS");
  xcb_intern_atom_reply_t* reply =
      xcb_intern_atom_reply(demo->connection, cookie, 0);

  xcb_intern_atom_cookie_t cookie2 =
      xcb_intern_atom(demo->connection, 0, 16, "WM_DELETE_WINDOW");
  demo->atom_wm_delete_window =
      xcb_intern_atom_reply(demo->connection, cookie2, 0);

  xcb_change_property(demo->connection, XCB_PROP_MODE_REPLACE, demo->xcb_window,
                      (*reply).atom, 4, 32, 1,
                      &(*demo->atom_wm_delete_window).atom);
  free(reply);

  xcb_map_window(demo->connection, demo->xcb_window);

  // Force the x/y coordinates to 100,100 results are identical in consecutive
  // runs
  const uint32_t coords[] = {100, 100};
  xcb_configure_window(demo->connection, demo->xcb_window,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, coords);
}
// VK_USE_PLATFORM_XCB_KHR
#endif

#if defined(CUBE_USE_IMAGE_PIPE)
static void demo_update_magma_one_frame(struct demo* demo) {
  static constexpr float kMsPerSec =
      std::chrono::milliseconds(std::chrono::seconds(1)).count();

  auto& num_frames = demo->fuchsia_state->num_frames;
  auto& elapsed_frames = demo->fuchsia_state->elapsed_frames;
  auto& t0 = demo->fuchsia_state->t0;

  if (elapsed_frames && (elapsed_frames % num_frames) == 0) {
    auto t1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = t1 - t0;

    float fps = num_frames / (elapsed.count() / kMsPerSec);
    printf("Framerate average for last %u frames: %f frames per second\n",
           num_frames, fps);
    // attempt to log once per second
    num_frames = fps;
    elapsed_frames = 0;
    t0 = t1;
  }

  demo_draw(demo);
  demo->curFrame++;
  elapsed_frames++;
  if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount)
    demo->quit = true;
}
#endif /**/

/*
 * Return 1 (true) if all layer names specified in check_names
 * can be found in given layer properties.
 */
static VkBool32 demo_check_layers(uint32_t check_count,
                                  const char** check_names,
                                  uint32_t layer_count,
                                  VkLayerProperties* layers) {
  for (uint32_t i = 0; i < check_count; i++) {
    VkBool32 found = 0;
    for (uint32_t j = 0; j < layer_count; j++) {
      if (!strcmp(check_names[i], layers[j].layerName)) {
        found = 1;
        break;
      }
    }
    if (!found) {
      fprintf(stderr, "Cannot find layer: %s\n", check_names[i]);
      return 0;
    }
  }
  return 1;
}

static void demo_init_vk(struct demo* demo) {
  VkResult err;
  uint32_t instance_extension_count = 0;
  uint32_t instance_layer_count = 0;
  const char** instance_validation_layers = NULL;
  demo->enabled_extension_count = 0;
  demo->enabled_layer_count = 0;

  const char* instance_validation_layers_alt1[] = {
#ifndef NDEBUG
      "VK_LAYER_LUNARG_standard_validation",
#endif
  };

  const char* instance_validation_layers_alt2[] = {
#ifndef NDEBUG
      "VK_LAYER_GOOGLE_threading",      "VK_LAYER_LUNARG_parameter_validation",
      "VK_LAYER_LUNARG_object_tracker", "VK_LAYER_LUNARG_core_validation",
      "VK_LAYER_LUNARG_swapchain",      "VK_LAYER_GOOGLE_unique_objects",
#endif
  };

#if VK_USE_PLATFORM_FUCHSIA
#if CUBE_USE_IMAGE_PIPE
  const char* kMagmaLayer = "VK_LAYER_FUCHSIA_imagepipe_swapchain";
#else
  const char* kMagmaLayer = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";
#endif
#endif

  /* Look for validation layers */
  VkBool32 validation_found = 0;
  if (true) {
    err = vkEnumerateInstanceLayerProperties(&instance_layer_count, NULL);
    assert(!err);

    instance_validation_layers = instance_validation_layers_alt1;
    if (instance_layer_count > 0) {
      VkLayerProperties* instance_layers = reinterpret_cast<VkLayerProperties*>(
          malloc(sizeof(VkLayerProperties) * instance_layer_count));
      err = vkEnumerateInstanceLayerProperties(&instance_layer_count,
                                               instance_layers);
      assert(!err);

      validation_found = demo_check_layers(
          ARRAY_SIZE(instance_validation_layers_alt1),
          instance_validation_layers, instance_layer_count, instance_layers);
      if (validation_found) {
        demo->enabled_layer_count = ARRAY_SIZE(instance_validation_layers_alt1);
        demo->enabled_layers[0] = "VK_LAYER_LUNARG_standard_validation";
        for (uint32_t i = 0; i < demo->enabled_layer_count; i++) {
          demo->enabled_layers[i] = instance_validation_layers[i];
        }
      } else {
        // use alternative set of validation layers
        instance_validation_layers = instance_validation_layers_alt2;
        demo->enabled_layer_count = ARRAY_SIZE(instance_validation_layers_alt2);
        validation_found = demo_check_layers(
            ARRAY_SIZE(instance_validation_layers_alt2),
            instance_validation_layers, instance_layer_count, instance_layers);
        for (uint32_t i = 0; i < demo->enabled_layer_count; i++) {
          demo->enabled_layers[i] = instance_validation_layers[i];
        }
      }
      free(instance_layers);
    }

    if (!validation_found) {
      ERR_EXIT(
          "vkEnumerateInstanceLayerProperties failed to find "
          "required validation layer.\n\n"
          "Please look at the Getting Started guide for additional "
          "information.\n",
          "vkCreateInstance Failure");
    }
  }

  /* Look for instance extensions */
  VkBool32 surfaceExtFound = 0;
  VkBool32 platformSurfaceExtFound = 0;
#if defined(VK_USE_PLATFORM_XLIB_KHR)
  VkBool32 xlibSurfaceExtFound = 0;
#endif
  memset(demo->extension_names, 0, sizeof(demo->extension_names));

  err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count,
                                               NULL);
  assert(!err);

  if (instance_extension_count > 0) {
    VkExtensionProperties* instance_extensions =
        reinterpret_cast<VkExtensionProperties*>(
            malloc(sizeof(VkExtensionProperties) * instance_extension_count));
    err = vkEnumerateInstanceExtensionProperties(
        NULL, &instance_extension_count, instance_extensions);
    assert(!err);
    for (uint32_t i = 0; i < instance_extension_count; i++) {
      if (!strcmp(VK_KHR_SURFACE_EXTENSION_NAME,
                  instance_extensions[i].extensionName)) {
        surfaceExtFound = 1;
        demo->extension_names[demo->enabled_extension_count++] =
            VK_KHR_SURFACE_EXTENSION_NAME;
      } else if (!strcmp(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
                         instance_extensions[i].extensionName)) {
        demo->extension_names[demo->enabled_extension_count++] =
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
      }
#if defined(VK_USE_PLATFORM_XLIB_KHR)
      if (!strcmp(VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
                  instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        xlibSurfaceExtFound = 1;
        demo->extension_names[demo->enabled_extension_count++] =
            VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
      }
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
      if (!strcmp(VK_KHR_XCB_SURFACE_EXTENSION_NAME,
                  instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        demo->extension_names[demo->enabled_extension_count++] =
            VK_KHR_XCB_SURFACE_EXTENSION_NAME;
      }
#endif
#if defined(VK_USE_PLATFORM_FUCHSIA)
      if (!strcmp(VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME,
                  instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        demo->extension_names[demo->enabled_extension_count++] =
            VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME;
      }
#endif
      if (!strcmp(VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
                  instance_extensions[i].extensionName)) {
        if (demo->validate) {
          demo->extension_names[demo->enabled_extension_count++] =
              VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
        }
      }
      assert(demo->enabled_extension_count < 64);
    }

    free(instance_extensions);
  }

  if (!surfaceExtFound || !platformSurfaceExtFound) {
    uint32_t count;
    VkResult result =
        vkEnumerateInstanceExtensionProperties(kMagmaLayer, &count, NULL);
    if (result == VK_SUCCESS && count > 0) {
      VkExtensionProperties* extensions =
          reinterpret_cast<VkExtensionProperties*>(
              malloc(sizeof(VkExtensionProperties) * count));
      result = vkEnumerateInstanceExtensionProperties(kMagmaLayer, &count,
                                                      extensions);
      for (uint32_t i = 0; i < count; i++) {
        if (!strcmp(VK_KHR_SURFACE_EXTENSION_NAME,
                    extensions[i].extensionName)) {
          surfaceExtFound = 1;
          demo->extension_names[demo->enabled_extension_count++] =
              VK_KHR_SURFACE_EXTENSION_NAME;
          demo->enabled_layers[demo->enabled_layer_count++] = kMagmaLayer;
        }
        if (!strcmp(VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME,
                    extensions[i].extensionName)) {
          platformSurfaceExtFound = 1;
          demo->extension_names[demo->enabled_extension_count++] =
              VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME;
          demo->enabled_layers[demo->enabled_layer_count++] = kMagmaLayer;
        }
      }
      free(extensions);
    }
  }

  if (!surfaceExtFound) {
    ERR_EXIT(
        "vkEnumerateInstanceExtensionProperties failed to find "
        "the " VK_KHR_SURFACE_EXTENSION_NAME
        " extension.\n\nDo you have a compatible "
        "Vulkan installable client driver (ICD) installed?\nPlease "
        "look at the Getting Started guide for additional "
        "information.\n",
        "vkCreateInstance Failure");
  }
  if (!platformSurfaceExtFound) {
#if defined(VK_USE_PLATFORM_XCB_KHR)
    ERR_EXIT(
        "vkEnumerateInstanceExtensionProperties failed to find "
        "the " VK_KHR_XCB_SURFACE_EXTENSION_NAME
        " extension.\n\nDo you have a compatible "
        "Vulkan installable client driver (ICD) installed?\nPlease "
        "look at the Getting Started guide for additional "
        "information.\n",
        "vkCreateInstance Failure");
#endif
#if defined(VK_USE_PLATFORM_FUCHSIA)
    ERR_EXIT(
        "vkEnumerateInstanceExtensionProperties failed to find "
        "the " VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME
        " extension.\n\nDo you have a compatible "
        "Vulkan installable client driver (ICD) installed?\nPlease "
        "look at the Getting Started guide for additional "
        "information.\n",
        "vkCreateInstance Failure");
#endif
  }
#if defined(VK_USE_PLATFORM_XLIB_KHR)
  if (demo->use_xlib && !xlibSurfaceExtFound) {
    ERR_EXIT(
        "vkEnumerateInstanceExtensionProperties failed to find "
        "the " VK_KHR_XLIB_SURFACE_EXTENSION_NAME
        " extension.\n\nDo you have a compatible "
        "Vulkan installable client driver (ICD) installed?\nPlease "
        "look at the Getting Started guide for additional "
        "information.\n",
        "vkCreateInstance Failure");
  }
#endif
  const VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = NULL,
      .pApplicationName = APP_SHORT_NAME,
      .applicationVersion = 0,
      .pEngineName = APP_SHORT_NAME,
      .engineVersion = 0,
      .apiVersion = VK_API_VERSION_1_0,
  };
  VkInstanceCreateInfo inst_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = NULL,
      .pApplicationInfo = &app,
      .enabledLayerCount = demo->enabled_layer_count,
      .ppEnabledLayerNames = (const char* const*)demo->enabled_layers,
      .enabledExtensionCount = demo->enabled_extension_count,
      .ppEnabledExtensionNames = (const char* const*)demo->extension_names,
  };

  /*
   * This is info for a temp callback to use during CreateInstance.
   * After the instance is created, we use the instance-based
   * function to register the final callback.
   */
  VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
  if (demo->validate) {
    dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    dbgCreateInfo.pNext = NULL;
    dbgCreateInfo.pfnCallback = demo->use_break ? BreakCallback : dbgFunc;
    dbgCreateInfo.pUserData = demo;
    dbgCreateInfo.flags =
        VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    inst_info.pNext = &dbgCreateInfo;
  }

  uint32_t gpu_count;

  err = vkCreateInstance(&inst_info, NULL, &demo->inst);
  if (err == VK_ERROR_INCOMPATIBLE_DRIVER) {
    ERR_EXIT(
        "Cannot find a compatible Vulkan installable client driver "
        "(ICD).\n\nPlease look at the Getting Started guide for "
        "additional information.\n",
        "vkCreateInstance Failure");
  } else if (err == VK_ERROR_EXTENSION_NOT_PRESENT) {
    ERR_EXIT(
        "Cannot find a specified extension library"
        ".\nMake sure your layers path is set appropriately.\n",
        "vkCreateInstance Failure");
  } else if (err) {
    ERR_EXIT(
        "vkCreateInstance failed.\n\nDo you have a compatible Vulkan "
        "installable client driver (ICD) installed?\nPlease look at "
        "the Getting Started guide for additional information.\n",
        "vkCreateInstance Failure");
  }

  /* Make initial call to query gpu_count, then second call for gpu info*/
  err = vkEnumeratePhysicalDevices(demo->inst, &gpu_count, NULL);
  assert(!err && gpu_count > 0);

  if (gpu_count > 0) {
    VkPhysicalDevice* physical_devices = reinterpret_cast<VkPhysicalDevice*>(
        malloc(sizeof(VkPhysicalDevice) * gpu_count));
    err = vkEnumeratePhysicalDevices(demo->inst, &gpu_count, physical_devices);
    assert(!err);
    /* For cube demo we just grab the first physical device */
    demo->gpu = physical_devices[0];
    free(physical_devices);
  } else {
    ERR_EXIT(
        "vkEnumeratePhysicalDevices reported zero accessible devices.\n\n"
        "Do you have a compatible Vulkan installable client driver (ICD) "
        "installed?\nPlease look at the Getting Started guide for "
        "additional information.\n",
        "vkEnumeratePhysicalDevices Failure");
  }

  /* Look for device extensions */
  uint32_t device_extension_count = 0;
  VkBool32 swapchainExtFound = 0;
  demo->enabled_extension_count = 0;
  memset(demo->extension_names, 0, sizeof(demo->extension_names));
  bool ycbcrExtFound = false;

  err = vkEnumerateDeviceExtensionProperties(demo->gpu, NULL,
                                             &device_extension_count, NULL);
  assert(!err);

  if (device_extension_count > 0) {
    VkExtensionProperties* device_extensions =
        reinterpret_cast<VkExtensionProperties*>(
            malloc(sizeof(VkExtensionProperties) * device_extension_count));
    err = vkEnumerateDeviceExtensionProperties(
        demo->gpu, NULL, &device_extension_count, device_extensions);
    assert(!err);

    for (uint32_t i = 0; i < device_extension_count; i++) {
      if (!strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                  device_extensions[i].extensionName)) {
        swapchainExtFound = 1;
        demo->extension_names[demo->enabled_extension_count++] =
            VK_KHR_SWAPCHAIN_EXTENSION_NAME;
      } else if (!strcmp(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
                         device_extensions[i].extensionName)) {
        ycbcrExtFound = true;
        demo->extension_names[demo->enabled_extension_count++] =
            VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME;
      } else if (!strcmp(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
                         device_extensions[i].extensionName)) {
        demo->extension_names[demo->enabled_extension_count++] =
            VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
      } else if (!strcmp(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
                         device_extensions[i].extensionName)) {
        demo->extension_names[demo->enabled_extension_count++] =
            VK_KHR_BIND_MEMORY_2_EXTENSION_NAME;
      } else if (!strcmp(VK_KHR_MAINTENANCE1_EXTENSION_NAME,
                         device_extensions[i].extensionName)) {
        demo->extension_names[demo->enabled_extension_count++] =
            VK_KHR_MAINTENANCE1_EXTENSION_NAME;
      }
      assert(demo->enabled_extension_count < 64);
    }

    free(device_extensions);
  }

  if (!swapchainExtFound) {
    uint32_t count;
    VkResult result = vkEnumerateDeviceExtensionProperties(
        demo->gpu, kMagmaLayer, &count, NULL);
    if (result == VK_SUCCESS && count > 0) {
      VkExtensionProperties* extensions =
          reinterpret_cast<VkExtensionProperties*>(
              malloc(sizeof(VkExtensionProperties) * count));
      result = vkEnumerateDeviceExtensionProperties(demo->gpu, kMagmaLayer,
                                                    &count, extensions);
      for (uint32_t i = 0; i < count; i++) {
        if (!strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                    extensions[i].extensionName)) {
          swapchainExtFound = 1;
          demo->extension_names[demo->enabled_extension_count++] =
              VK_KHR_SWAPCHAIN_EXTENSION_NAME;
          demo->enabled_layers[demo->enabled_layer_count++] = kMagmaLayer;
        }
      }
      free(extensions);
    }
  }

  if (!swapchainExtFound) {
    ERR_EXIT(
        "vkEnumerateDeviceExtensionProperties failed to find "
        "the " VK_KHR_SWAPCHAIN_EXTENSION_NAME
        " extension.\n\nDo you have a compatible "
        "Vulkan installable client driver (ICD) installed?\nPlease "
        "look at the Getting Started guide for additional "
        "information.\n",
        "vkCreateInstance Failure");
  }

#if USE_YUV_TEXTURE
  if (!ycbcrExtFound) {
    ERR_EXIT("VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME not found",
             "Fatal");
  }
#endif

  if (demo->validate) {
    demo->CreateDebugReportCallback =
        (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
            demo->inst, "vkCreateDebugReportCallbackEXT");
    demo->DestroyDebugReportCallback =
        (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
            demo->inst, "vkDestroyDebugReportCallbackEXT");
    if (!demo->CreateDebugReportCallback) {
      ERR_EXIT("GetProcAddr: Unable to find vkCreateDebugReportCallbackEXT\n",
               "vkGetProcAddr Failure");
    }
    if (!demo->DestroyDebugReportCallback) {
      ERR_EXIT("GetProcAddr: Unable to find vkDestroyDebugReportCallbackEXT\n",
               "vkGetProcAddr Failure");
    }
    demo->DebugReportMessage =
        (PFN_vkDebugReportMessageEXT)vkGetInstanceProcAddr(
            demo->inst, "vkDebugReportMessageEXT");
    if (!demo->DebugReportMessage) {
      ERR_EXIT("GetProcAddr: Unable to find vkDebugReportMessageEXT\n",
               "vkGetProcAddr Failure");
    }

    VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
    PFN_vkDebugReportCallbackEXT callback;
    callback = demo->use_break ? BreakCallback : dbgFunc;
    dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    dbgCreateInfo.pNext = NULL;
    dbgCreateInfo.pfnCallback = callback;
    dbgCreateInfo.pUserData = demo;
    dbgCreateInfo.flags =
        VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    err = demo->CreateDebugReportCallback(demo->inst, &dbgCreateInfo, NULL,
                                          &demo->msg_callback);
    switch (err) {
      case VK_SUCCESS:
        break;
      case VK_ERROR_OUT_OF_HOST_MEMORY:
        ERR_EXIT("CreateDebugReportCallback: out of host memory\n",
                 "CreateDebugReportCallback Failure");
        break;
      default:
        ERR_EXIT("CreateDebugReportCallback: unknown failure\n",
                 "CreateDebugReportCallback Failure");
        break;
    }
  }
  vkGetPhysicalDeviceProperties(demo->gpu, &demo->gpu_props);

  /* Call with NULL data to get count */
  vkGetPhysicalDeviceQueueFamilyProperties(demo->gpu, &demo->queue_family_count,
                                           NULL);
  assert(demo->queue_family_count >= 1);

  demo->queue_props = (VkQueueFamilyProperties*)malloc(
      demo->queue_family_count * sizeof(VkQueueFamilyProperties));
  vkGetPhysicalDeviceQueueFamilyProperties(demo->gpu, &demo->queue_family_count,
                                           demo->queue_props);

  GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfaceSupportKHR);
  GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfaceCapabilitiesKHR);
  GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfaceFormatsKHR);
  GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfacePresentModesKHR);
  GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceFeatures2KHR);
  GET_INSTANCE_PROC_ADDR(demo->inst, GetSwapchainImagesKHR);

  // Query fine-grained feature support for this device.
  //  If app has specific feature requirements it should check supported
  //  features based on this query
  VkPhysicalDeviceSamplerYcbcrConversionFeaturesKHR ycbcr_features = {
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES_KHR,
      .pNext = NULL};
  VkPhysicalDeviceFeatures2KHR physDevFeatures = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,
      .pNext = &ycbcr_features};
  demo->fpGetPhysicalDeviceFeatures2KHR(demo->gpu, &physDevFeatures);

#if USE_YUV_TEXTURE
  if (!ycbcr_features.samplerYcbcrConversion) {
    ERR_EXIT("samplerYcbcrConversion not supported", "Fatal");
  }
#endif
}

static void demo_create_device(struct demo* demo) {
  VkResult U_ASSERT_ONLY err;
  float queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queues[2];
  queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queues[0].pNext = NULL;
  queues[0].queueFamilyIndex = demo->graphics_queue_family_index;
  queues[0].queueCount = 1;
  queues[0].pQueuePriorities = queue_priorities;
  queues[0].flags = 0;

  VkDeviceCreateInfo device = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = NULL,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = queues,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = NULL,
      .enabledExtensionCount = demo->enabled_extension_count,
      .ppEnabledExtensionNames = (const char* const*)demo->extension_names,
      .pEnabledFeatures =
          NULL,  // If specific features are required, pass them in here
  };
  if (demo->separate_present_queue) {
    queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[1].pNext = NULL;
    queues[1].queueFamilyIndex = demo->present_queue_family_index;
    queues[1].queueCount = 1;
    queues[1].pQueuePriorities = queue_priorities;
    queues[1].flags = 0;
    device.queueCreateInfoCount = 2;
  }
  err = vkCreateDevice(demo->gpu, &device, NULL, &demo->device);
  assert(!err);
}

void demo_init_vk_swapchain(struct demo* demo) {
  VkResult U_ASSERT_ONLY err = VK_SUCCESS;
  uint32_t i;

  // Create a WSI surface for the window:
  if (demo->use_xlib) {
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    VkXlibSurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = NULL;
    createInfo.flags = 0;
    createInfo.dpy = demo->display;
    createInfo.window = demo->xlib_window;

    err = vkCreateXlibSurfaceKHR(demo->inst, &createInfo, NULL, &demo->surface);
#endif
  } else {
#if defined(VK_USE_PLATFORM_XCB_KHR)
    VkXcbSurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = NULL;
    createInfo.flags = 0;
    createInfo.connection = demo->connection;
    createInfo.window = demo->xcb_window;

    err = vkCreateXcbSurfaceKHR(demo->inst, &createInfo, NULL, &demo->surface);
#elif defined(VK_USE_PLATFORM_FUCHSIA)
    VkImagePipeSurfaceCreateInfoFUCHSIA createInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
#if defined(CUBE_USE_IMAGE_PIPE)
      .imagePipeHandle = demo->fuchsia_state->image_pipe_handle,
#endif
      .pNext = nullptr,
    };
    err = vkCreateImagePipeSurfaceFUCHSIA(demo->inst, &createInfo, nullptr,
                                          &demo->surface);
#endif  // VK_USE_PLATFORM_MAGMA_KHR
  }
  assert(!err);

  // Iterate over each queue to learn whether it supports presenting:
  VkBool32* supportsPresent =
      (VkBool32*)malloc(demo->queue_family_count * sizeof(VkBool32));
  for (i = 0; i < demo->queue_family_count; i++) {
    demo->fpGetPhysicalDeviceSurfaceSupportKHR(demo->gpu, i, demo->surface,
                                               &supportsPresent[i]);
  }

  // Search for a graphics and a present queue in the array of queue
  // families, try to find one that supports both
  uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
  uint32_t presentQueueFamilyIndex = UINT32_MAX;
  for (i = 0; i < demo->queue_family_count; i++) {
    if ((demo->queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      if (graphicsQueueFamilyIndex == UINT32_MAX) {
        graphicsQueueFamilyIndex = i;
      }

      if (supportsPresent[i] == VK_TRUE) {
        graphicsQueueFamilyIndex = i;
        presentQueueFamilyIndex = i;
        break;
      }
    }
  }

  if (presentQueueFamilyIndex == UINT32_MAX) {
    // If didn't find a queue that supports both graphics and present, then
    // find a separate present queue.
    for (i = 0; i < demo->queue_family_count; ++i) {
      if (supportsPresent[i] == VK_TRUE) {
        presentQueueFamilyIndex = i;
        break;
      }
    }
  }

  // Generate error if could not find both a graphics and a present queue
  if (graphicsQueueFamilyIndex == UINT32_MAX ||
      presentQueueFamilyIndex == UINT32_MAX) {
    ERR_EXIT("Could not find both graphics and present queues\n",
             "Swapchain Initialization Failure");
  }

  demo->graphics_queue_family_index = graphicsQueueFamilyIndex;
  demo->present_queue_family_index = presentQueueFamilyIndex;
  demo->separate_present_queue =
      (demo->graphics_queue_family_index != demo->present_queue_family_index);
  free(supportsPresent);

  demo_create_device(demo);

  GET_DEVICE_PROC_ADDR(demo->device, CreateSwapchainKHR);
  GET_DEVICE_PROC_ADDR(demo->device, DestroySwapchainKHR);
  GET_DEVICE_PROC_ADDR(demo->device, GetSwapchainImagesKHR);
  GET_DEVICE_PROC_ADDR(demo->device, AcquireNextImageKHR);
  GET_DEVICE_PROC_ADDR(demo->device, QueuePresentKHR);
  GET_DEVICE_PROC_ADDR(demo->device, CreateSamplerYcbcrConversionKHR);

  vkGetDeviceQueue(demo->device, demo->graphics_queue_family_index, 0,
                   &demo->graphics_queue);

  if (!demo->separate_present_queue) {
    demo->present_queue = demo->graphics_queue;
  } else {
    vkGetDeviceQueue(demo->device, demo->present_queue_family_index, 0,
                     &demo->present_queue);
  }

  // Get the list of VkFormat's that are supported:
  uint32_t formatCount;
  err = demo->fpGetPhysicalDeviceSurfaceFormatsKHR(demo->gpu, demo->surface,
                                                   &formatCount, NULL);
  assert(!err);
  VkSurfaceFormatKHR* surfFormats =
      (VkSurfaceFormatKHR*)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
  err = demo->fpGetPhysicalDeviceSurfaceFormatsKHR(demo->gpu, demo->surface,
                                                   &formatCount, surfFormats);
  assert(!err);
  // If the format list includes just one entry of VK_FORMAT_UNDEFINED,
  // the surface has no preferred format.  Otherwise, at least one
  // supported format will be returned.
  if (formatCount == 1 && surfFormats[0].format == VK_FORMAT_UNDEFINED) {
    demo->format = VK_FORMAT_B8G8R8A8_UNORM;
  } else {
    assert(formatCount >= 1);
    demo->format = surfFormats[0].format;
  }
  demo->color_space = surfFormats[0].colorSpace;

  demo->quit = false;
  demo->curFrame = 0;

  // Create semaphores to synchronize acquiring presentable buffers before
  // rendering and waiting for drawing to be complete before presenting
  VkSemaphoreCreateInfo semaphoreCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
  };

  // Create fences that we can use to throttle if we get too far
  // ahead of the image presents
  VkFenceCreateInfo fence_ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                .pNext = NULL,
                                .flags = VK_FENCE_CREATE_SIGNALED_BIT};
  for (uint32_t i = 0; i < FRAME_LAG; i++) {
    vkCreateFence(demo->device, &fence_ci, NULL, &demo->fences[i]);
    err = vkCreateSemaphore(demo->device, &semaphoreCreateInfo, NULL,
                            &demo->image_acquired_semaphores[i]);
    assert(!err);

    err = vkCreateSemaphore(demo->device, &semaphoreCreateInfo, NULL,
                            &demo->draw_complete_semaphores[i]);
    assert(!err);

    if (demo->separate_present_queue) {
      err = vkCreateSemaphore(demo->device, &semaphoreCreateInfo, NULL,
                              &demo->image_ownership_semaphores[i]);
      assert(!err);
    }
  }
  demo->frame_index = 0;

  // Get Memory information and properties
  vkGetPhysicalDeviceMemoryProperties(demo->gpu, &demo->memory_properties);
}

static void demo_init_connection(struct demo* demo) {
#if defined(VK_USE_PLATFORM_XCB_KHR)
  const xcb_setup_t* setup;
  xcb_screen_iterator_t iter;
  int scr;

  demo->connection = xcb_connect(NULL, &scr);
  if (xcb_connection_has_error(demo->connection) > 0) {
    printf(
        "Cannot find a compatible Vulkan installable client driver "
        "(ICD).\nExiting ...\n");
    fflush(stdout);
    exit(1);
  }

  setup = xcb_get_setup(demo->connection);
  iter = xcb_setup_roots_iterator(setup);
  while (scr-- > 0)
    xcb_screen_next(&iter);

  demo->screen = iter.data;
#endif
}

void demo_init(struct demo* demo, int argc, char** argv) {
  vec3 eye = {0.0f, 3.0f, 5.0f};
  vec3 origin = {0, 0, 0};
  vec3 up = {0.0f, 1.0f, 0.0};

  memset(demo, 0, sizeof(*demo));
  demo->presentMode = VK_PRESENT_MODE_FIFO_KHR;
  demo->frameCount = INT32_MAX;

  demo->width = 2160;
  demo->height = 1440;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--use_staging") == 0) {
      demo->use_staging_buffer = true;
      continue;
    }
    if ((strcmp(argv[i], "--present_mode") == 0) && (i < argc - 1)) {
      demo->presentMode = static_cast<VkPresentModeKHR>(atoi(argv[i + 1]));
      i++;
      continue;
    }
    if (strcmp(argv[i], "--break") == 0) {
      demo->use_break = true;
      continue;
    }
    if (strcmp(argv[i], "--validate") == 0) {
      demo->validate = true;
      continue;
    }
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (strcmp(argv[i], "--xlib") == 0) {
      demo->use_xlib = true;
      continue;
    }
#endif
    if (strcmp(argv[i], "--c") == 0 && demo->frameCount == INT32_MAX &&
        i < argc - 1 && sscanf(argv[i + 1], "%d", &demo->frameCount) == 1 &&
        demo->frameCount >= 0) {
      i++;
      continue;
    }
    if (strcmp(argv[i], "--suppress_popups") == 0) {
      demo->suppress_popups = true;
      continue;
    }
    if ((strcmp(argv[i], "--size") == 0 || strcmp(argv[i], "--res") == 0) &&
        (i < argc - 2) && sscanf(argv[i + 1], "%u", &demo->width) == 1 &&
        sscanf(argv[i + 2], "%u", &demo->height) == 1) {
      i += 2;
      continue;
    }

    fprintf(
        stderr,
        "Usage:\n  %s [--use_staging] [--validate] [--break] "
#if defined(VK_USE_PLATFORM_XLIB_KHR)
        "[--xlib] "
#endif
        "[--c <framecount>] [--suppress_popups] [--present_mode <present mode "
        "enum>] [--res width height]\n"
        "VK_PRESENT_MODE_IMMEDIATE_KHR = %d\n"
        "VK_PRESENT_MODE_MAILBOX_KHR = %d\n"
        "VK_PRESENT_MODE_FIFO_KHR = %d\n"
        "VK_PRESENT_MODE_FIFO_RELAXED_KHR = %d\n",
        APP_SHORT_NAME, VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_FIFO_RELAXED_KHR);
    fflush(stderr);
    exit(1);
  }

  if (!demo->use_xlib)
    demo_init_connection(demo);

  demo_init_vk(demo);

  demo->spin_angle = 1.0f;
  demo->spin_increment = 0.2f;
  demo->pause = false;

  // moved to after we know the window size
  mat4x4_perspective(demo->projection_matrix, (float)degreesToRadians(45.0f),
                     1.0f, 0.1f, 100.0f);
  mat4x4_look_at(demo->view_matrix, eye, origin, up);
  mat4x4_identity(demo->model_matrix);

  // Unclear why removing this fixes the winding order affecting face culling
  // order. demo->projection_matrix[1][1]*=-1;  //Flip projection matrix from GL
  // to Vulkan orientation.
}

#if defined(VK_USE_PLATFORM_FUCHSIA)

#if defined(CUBE_USE_IMAGE_PIPE)

void demo_run_image_pipe(struct demo* demo, int argc, char** argv) {
  demo->fuchsia_state = std::make_unique<FuchsiaState>();

  demo->fuchsia_state->t0 = std::chrono::high_resolution_clock::now();

#ifdef MAGMA_ENABLE_TRACING
  trace::TraceProvider trace_provider(demo->fuchsia_state->loop.dispatcher());
#endif

  demo->fuchsia_state->component =
      std::make_unique<scenic::ViewProviderComponent>(
          [demo](scenic::ViewContext view_context) {
            auto resize_callback = [demo](float width, float height) {
              demo->width = width;
              demo->height = height;
              if (demo->prepared) {
                demo_resize(demo);
              } else {
                demo_prepare(demo);
              }
            };
            auto view = std::make_unique<VkCubeView>(std::move(view_context),
                                                     resize_callback);
            demo->fuchsia_state->image_pipe_handle =
                view->TakeImagePipeChannel().release();
            demo_init_vk_swapchain(demo);
            return view;
          },
          &demo->fuchsia_state->loop);

  zx_status_t loop_status = ZX_OK;
  while (!demo->quit && loop_status == ZX_OK) {
    if (demo->prepared) {
      demo_update_magma_one_frame(demo);
    }
    loop_status = demo->fuchsia_state->loop.RunUntilIdle();
  }
}

#else

static void demo_run_magma(struct demo* demo) {
  uint32_t num_frames = 60;
  uint32_t elapsed_frames = 0;
  static const float kMsPerSec =
      std::chrono::milliseconds(std::chrono::seconds(1)).count();

  float total_ms = 0;
  auto t0 = std::chrono::high_resolution_clock::now();

  while (!demo->quit) {
    demo_update_data_buffer(demo);

    auto t1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = t1 - t0;
    total_ms += elapsed.count();
    t0 = t1;

    if (elapsed_frames && (elapsed_frames % num_frames) == 0) {
      float fps = num_frames / (total_ms / kMsPerSec);
      printf("Framerate average for last %u frames: %f frames per second\n",
             num_frames, fps);
      total_ms = 0;
      // attempt to log once per second
      num_frames = fps;
      elapsed_frames = 0;
    }

    demo_draw(demo);
    demo->curFrame++;
    elapsed_frames++;
    if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount)
      demo->quit = true;
  }
}
#endif  // defined(CUBE_USE_IMAGE_PIPE)
#endif  // VK_USE_PLATFORM_FUCHSIA

int cube_main(int argc, char** argv) {
  struct demo demo;

  demo_init(&demo, argc, argv);

#if defined(VK_USE_PLATFORM_XLIB_KHR) && defined(VK_USE_PLATFORM_XCB_KHR)
  if (demo.use_xlib)
    demo_create_xlib_window(&demo);
  else
    demo_create_xcb_window(&demo);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
  demo_create_xcb_window(&demo);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
  demo_create_xlib_window(&demo);
#endif

#if defined(CUBE_USE_IMAGE_PIPE)
  demo_run_image_pipe(&demo, argc, argv);
#else
  demo_init_vk_swapchain(&demo);

  demo_prepare(&demo);

#if defined(VK_USE_PLATFORM_XLIB_KHR) && defined(VK_USE_PLATFORM_XCB_KHR)
  if (demo.use_xlib)
    demo_run_xlib(&demo);
  else
    demo_run_xcb(&demo);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
  demo_run_xcb(&demo);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
  demo_run_xlib(&demo);
#elif defined(VK_USE_PLATFORM_FUCHSIA)
  demo_run_magma(&demo);
#endif

#endif  // defined(CUBE_USE_IMAGE_PIPE)

  demo_cleanup(&demo);

  return validation_error;
}

int test_vk_cube(int argc, char** argv) {
#if defined(MAGMA_USE_SHIM)
  VulkanShimInit();
#endif
#if defined(MAGMA_ENABLE_TRACING)
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  loop.StartThread();
  trace::TraceProvider trace_provider(loop.dispatcher());
#endif
  return cube_main(argc, argv);
}
