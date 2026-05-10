/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include "rtx_fork_cloud_shadows.h"

#include "../dxvk_context.h"
#include "../dxvk_device.h"
#include "rtx_shader_manager.h"
#include "rtx/pass/atmosphere/atmosphere_args.h"
#include "rtx/pass/common_binding_indices.h"

#include <cmath>

namespace dxvk {

  // CloudShadowMapPushArgs lives in rtx_fork_cloud_shadows.h so that
  // rtx_atmosphere.cpp's dispatch path can construct one without duplicating
  // the layout. See the header for the static_assert and field-by-field
  // mirror of cloud_shadow_map_bindings.slangh.

  void RtxCloudShadowMap::initialize(Rc<DxvkContext> ctx) {
    if (m_initialized) {
      return;
    }

    // Allocate the VkImage as a 2D R8_UNORM with both STORAGE (compute write)
    // and SAMPLED (consumer read) usage. Resources::createImageResource adds
    // STORAGE+SAMPLED+TRANSFER_DST internally; the extra-usage parameter is
    // just there to inject any additional bits. We don't need any beyond the
    // defaults, so pass 0.
    const VkExtent3D extent = { kResolution, kResolution, 1 };
    m_image = Resources::createImageResource(
      ctx,
      "Atmosphere Cloud Shadow Map",
      extent,
      VK_FORMAT_R8_UNORM,
      1,                                 // numLayers
      VK_IMAGE_TYPE_2D,
      VK_IMAGE_VIEW_TYPE_2D,
      0,                                 // imageCreateFlags
      0,                                 // extraUsageFlags (defaults already cover STORAGE+SAMPLED+TRANSFER_DST)
      VkClearColorValue{},               // clearValue (unused)
      1                                  // mipLevels
    );

    // Linear sampler with clamp-to-border using white (R=1) border so any
    // off-grid sample reads as "no cloud shadow", matching the
    // evalCloudShadowAtWorld ATMOSPHERE_AVAILABLE early-return contract.
    // dxvk's DxvkSamplerCreateInfo uses field names mipmapLodMin/Max (not
    // minLod/maxLod) and borderColor is VkClearColorValue (a union), not
    // VkBorderColor (the Vulkan enum). White border = 1.0f on all four
    // channels.
    DxvkSamplerCreateInfo sampInfo = {};
    sampInfo.magFilter      = VK_FILTER_LINEAR;
    sampInfo.minFilter      = VK_FILTER_LINEAR;
    sampInfo.mipmapMode     = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampInfo.mipmapLodBias  = 0.f;
    sampInfo.mipmapLodMin   = 0.f;
    sampInfo.mipmapLodMax   = 0.f;
    sampInfo.useAnisotropy  = VK_FALSE;
    sampInfo.maxAnisotropy  = 1.f;
    sampInfo.addressModeU   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampInfo.addressModeV   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampInfo.addressModeW   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampInfo.compareToDepth = VK_FALSE;
    sampInfo.compareOp      = VK_COMPARE_OP_NEVER;
    sampInfo.borderColor.float32[0] = 1.f;
    sampInfo.borderColor.float32[1] = 1.f;
    sampInfo.borderColor.float32[2] = 1.f;
    sampInfo.borderColor.float32[3] = 1.f;
    sampInfo.usePixelCoord  = VK_FALSE;

    m_sampler = ctx->getDevice()->createSampler(sampInfo);
    m_initialized = true;
  }

  void RtxCloudShadowMap::dispatch(Rc<DxvkContext> ctx,
                                   const AtmosphereArgs& atmosphereArgs,
                                   const Vector3& cameraWorldPos) {
    (void)ctx;
    (void)atmosphereArgs;

    if (!m_initialized) {
      return;
    }

    // Snap camera XZ to texel boundary so a given world point lands on the
    // same texel across consecutive frames (until the camera crosses a texel
    // boundary, ~15.6 m at default config). The snap eliminates per-frame
    // sample shimmer that would otherwise show up as cloud-shadow flicker.
    const Vector2 cameraXZ(cameraWorldPos.x, cameraWorldPos.z);
    const Vector2 snappedXZ(
      std::floor(cameraXZ.x / kTexelSize) * kTexelSize,
      std::floor(cameraXZ.y / kTexelSize) * kTexelSize);
    const Vector2 mapOriginXZ(
      snappedXZ.x - 0.5f * kExtentMeters,
      snappedXZ.y - 0.5f * kExtentMeters);

    m_anchor.mapOriginXZ = mapOriginXZ;
    m_anchor.texelSize   = kTexelSize;
    m_anchor.resolution  = float(kResolution);

    // The actual GPU dispatch (bindShader / bindResourceView for the cloud-
    // noise SRV+sampler / pushConstants / ctx->dispatch) lives in
    // RtxAtmosphere::dispatchCloudShadowMap, which has the cloud-noise view
    // and sampler reachable as members. This method's contract is now just
    // "update anchor"; the caller drives the GPU work with the resulting
    // anchor + the push-arg layout exposed via rtx_fork_cloud_shadows.h.
  }

}
