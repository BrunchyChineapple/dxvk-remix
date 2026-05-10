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
#pragma once

#include "rtx_resources.h"
#include "../../util/util_vector.h"
#include "rtx/pass/atmosphere/atmosphere_args.h"  // AtmosphereArgs (global struct, shared with shader)

namespace dxvk {

  class DxvkContext;

  // Mirrors CloudShadowMapPushArgs from cloud_shadow_map_bindings.slangh.
  // Field order MUST match exactly; std430 / Vulkan push-constant alignment
  // requires the same hand-padded 16-byte rows as the slang struct. Total
  // 96 bytes, well under the 128-byte minimum guaranteed push-constant
  // range. Keep this in lock-step with the .slangh struct -- if either is
  // edited, both must be.
  struct CloudShadowMapPushArgs {
    // Row 0: vec3 + float
    vec3  sunDirection;
    float planetRadius;

    // Row 1: 4x float
    float cloudAltitude;
    float cloudThickness;
    float cloudCurvature;
    float cloudDensity;

    // Row 2: 4x float
    float cloudCoverageMean;
    float cloudCoverageSpread;
    float cloudCoverageNoiseScale;
    float cloudTypeMean;

    // Row 3: 4x float
    float cloudTypeSpread;
    float cloudTypeNoiseScale;
    float cloudNoiseTileKm;
    float cloudAnvilBias;

    // Row 4: 2x vec2
    vec2  cloudWindOffset;
    vec2  mapOriginXZ;

    // Row 5: float + uint + 2x pad float
    float    texelSize;
    uint32_t resolution;
    float    pad0;
    float    pad1;
  };
  static_assert(sizeof(CloudShadowMapPushArgs) == 96,
                "CloudShadowMapPushArgs must be 96 bytes -- mirror of "
                "cloud_shadow_map_bindings.slangh::CloudShadowMapPushArgs");

  // RtxCloudShadowMap -- fork-side resource manager for the per-frame 2D
  // cloud shadow map. Renders a 512x512 R8 ground-plane projection of cloud
  // transmittance along sun direction; consumers sample by world (X, Z) via
  // evalCloudShadowAtWorld in atmosphere_common.slangh.
  //
  // Structure mirrors RtxFastNoise (plain class, default ctor/dtor) but adds
  // a per-frame dispatch surface and a sampler -- the noise texture is
  // immutable after init, the shadow map is rewritten every frame.
  //
  // Spec: docs/superpowers/specs/2026-05-09-cloud-shadow-map-design.md
  class RtxCloudShadowMap {
  public:
    RtxCloudShadowMap() = default;
    ~RtxCloudShadowMap() = default;

    static constexpr uint32_t kResolution   = 512u;
    static constexpr float    kExtentMeters = 8000.f;
    static constexpr float    kTexelSize    = kExtentMeters / float(kResolution);

    // Allocates the VkImage, view, and sampler. Idempotent -- safe to call
    // multiple times. Must be called before any dispatch().
    void initialize(Rc<DxvkContext> ctx);

    // Runs once per frame. Captures camera world XZ, snaps to texel boundary,
    // dispatches the compute pass that fills the shadow map, and updates the
    // anchor returned by getAnchor(). Skips the GPU dispatch (leaves previous
    // frame's content) when cloudEnabled < 0.5f or the resources haven't been
    // initialized.
    void dispatch(Rc<DxvkContext> ctx,
                  const AtmosphereArgs& atmosphereArgs,
                  const Vector3& cameraWorldPos);

    // Resource accessors for binding into consumer dispatches.
    bool              isValid()    const { return m_image.isValid(); }
    Rc<DxvkImageView> getView()    const { return m_image.view; }
    Rc<DxvkSampler>   getSampler() const { return m_sampler; }

    // Per-frame anchor (origin XZ, texel size, resolution) -- written by
    // dispatch() and read by the per-frame hook to populate AtmosphereArgs.
    struct Anchor {
      Vector2 mapOriginXZ;
      float   texelSize;
      float   resolution;
    };
    const Anchor& getAnchor() const { return m_anchor; }

  private:
    Resources::Resource m_image;
    Rc<DxvkSampler>     m_sampler;
    Anchor              m_anchor      = { Vector2(0.f, 0.f), 1.f, 1.f };
    bool                m_initialized = false;
  };

}
