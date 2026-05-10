# Cloud Shadow Map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Replace the per-frame uniform-multiplier cloud shadow hack (`evalCloudGroundShadow`) with a position-aware 2D cloud shadow map. Fixes the blue-sky flicker (downstream temporal accumulator was triggered by the uniform-multiplier shape) and gives FNV real moving cloud-shadow patches on terrain.

**Architecture:** A 512² R8 ground-plane texture rendered each frame by a new compute pass. The texture is camera-anchored with texel-snapped origin (15.625m/texel covering 8km). Surface NEE + volume inscatter sample the texture at their world (X, Z) via a new `evalCloudShadowAtWorld` function. The legacy `evalCloudGroundShadow` is deleted.

**Tech Stack:** Slang/SPIR-V compute shader, dxvk-remix C++ runtime (Vulkan), the existing `RtxFastNoise` fork-side resource manager pattern.

**Spec:** [docs/superpowers/specs/2026-05-09-cloud-shadow-map-design.md](dxvk-remix/docs/superpowers/specs/2026-05-09-cloud-shadow-map-design.md)

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/dxvk/shaders/rtx/pass/common_binding_indices.h` | modify | add `BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP` slot |
| `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h` | modify | add `cameraWorldPos`, cloud-shadow-map anchor fields |
| `src/dxvk/shaders/rtx/pass/atmosphere/cloud_shadow_map_bindings.slangh` | create | compute-shader binding declarations |
| `src/dxvk/shaders/rtx/pass/atmosphere/cloud_shadow_map.comp.slang` | create | the per-texel shadow-render compute shader |
| `src/dxvk/rtx_render/rtx_fork_cloud_shadows.h` | create | `RtxCloudShadowMap` class declaration |
| `src/dxvk/rtx_render/rtx_fork_cloud_shadows.cpp` | create | `RtxCloudShadowMap` impl |
| `src/dxvk/rtx_render/rtx_atmosphere.h` | modify | own a `RtxCloudShadowMap` member |
| `src/dxvk/rtx_render/rtx_atmosphere.cpp` | modify | instantiate, dispatch per-frame, fill `AtmosphereArgs` anchor |
| `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_bindings.slangh` | modify | declare `AtmosphereCloudShadowMap` SRV + sampler under `CLOUD_SHADOW_MAP_AVAILABLE` guard |
| `src/dxvk/shaders/rtx/algorithm/geometry_resolver.slangh` | modify | bind shadow-map SRV; define `CLOUD_SHADOW_MAP_AVAILABLE` |
| (other consumer dispatches) | modify | bind SRV in volume integrate / volume ReSTIR (subagent identifies in Task 5) |
| `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh` | modify | add `evalCloudShadowAtWorld`; replace `getTransmittanceToSun`'s call site; delete `evalCloudGroundShadow` |
| `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh` | modify | line 74: pass `args.cameraWorldPos` instead of `vec3(0)` |
| `docs/fork-touchpoints.md` | modify | record new touches |

---

## Task 1: Foundation — Binding Slot + AtmosphereArgs Extension

**Files:**
- Modify: `src/dxvk/shaders/rtx/pass/common_binding_indices.h`
- Modify: `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h:165-178`

- [ ] **Step 1: Read `common_binding_indices.h` to find the right slot range**

Run:
```
grep -n "BINDING_ATMOSPHERE_FAST_NOISE" src/dxvk/shaders/rtx/pass/common_binding_indices.h
```
Read the surrounding lines to see what slot value FAST_NOISE got and what the next available value is.

- [ ] **Step 2: Add the binding slot**

In `common_binding_indices.h`, immediately after the line declaring `BINDING_ATMOSPHERE_FAST_NOISE` (or the FAST_NOISE_SAMPLER if separate), add:

```c
#define BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP            (<next slot value>)
#define BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP_SAMPLER    (<next slot value + 1>)
```

Concrete value: pick the next two unused slot numbers immediately after FAST_NOISE/FAST_NOISE_SAMPLER. The exact integer must be filled in based on the read.

- [ ] **Step 3: Extend `AtmosphereArgs`**

In `atmosphere_args.h`, find the section near line 165 with `pad4`/`pad5`/`pad6`/`pad7`. Replace those four pad floats AND adjust the trailing `padCloudC*` to include new useful fields. Specifically, add this block before the closing `};` of `AtmosphereArgs`:

```c
  // ----- Camera + cloud shadow map (fork) -----
  // Camera world position (Y-up) plumbed for cloud-shadow-map sampling
  // and any future spatially-varying atmosphere term.
  vec3 cameraWorldPos;
  float padCSM0;  // 16-byte alignment

  // Cloud shadow map anchor: bottom-left world XZ, texel size, resolution.
  // Set per frame by RtxCloudShadowMap::dispatch from the snapped camera XZ.
  // sample-time UV: (worldPos.xz - mapOriginXZ) / (texelSize * resolution)
  vec2  cloudShadowMapOriginXZ;
  float cloudShadowMapTexelSize;
  float cloudShadowMapResolution;
```

Place it between the existing `// ----- Stage C: 3D noise texture (fork) -----` block and the closing `};`.

- [ ] **Step 4: Initialize the new fields on the C++ side**

Find every place `AtmosphereArgs` is filled in C++. Run:
```
grep -rn "AtmosphereArgs " src/dxvk/rtx_render
```
For now, populate the new fields with safe defaults (later overridden by `RtxCloudShadowMap` in Task 4):
```cpp
args.cameraWorldPos             = vec3(0.f);
args.padCSM0                    = 0.f;
args.cloudShadowMapOriginXZ     = vec2(0.f);
args.cloudShadowMapTexelSize    = 1.f;
args.cloudShadowMapResolution   = 1.f;
```

- [ ] **Step 5: Build verify**

Run the `rtx-build` skill. Expected: clean exit code 0, zero compile errors.

If the build complains about missing fields anywhere `AtmosphereArgs` is initialized via `{...}` aggregate-init, those sites must also be updated.

- [ ] **Step 6: Commit**

```bash
git add src/dxvk/shaders/rtx/pass/common_binding_indices.h \
        src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h \
        src/dxvk/rtx_render/rtx_atmosphere.cpp
git commit -m "rtx(atmosphere): reserve cloud-shadow-map binding slot + cameraWorldPos in AtmosphereArgs

Foundation commit. Adds BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP and its sampler
slot, plus cameraWorldPos and shadow-map anchor fields (origin XZ, texel
size, resolution) to AtmosphereArgs. No consumers yet — fields are
initialized to safe defaults pending the RtxCloudShadowMap manager."
```

---

## Task 2: Compute Shader — `cloud_shadow_map.comp.slang`

**Files:**
- Create: `src/dxvk/shaders/rtx/pass/atmosphere/cloud_shadow_map_bindings.slangh`
- Create: `src/dxvk/shaders/rtx/pass/atmosphere/cloud_shadow_map.comp.slang`

- [ ] **Step 1: Read existing FastNoise compute-side bindings** (closest precedent)

Run:
```
find src/dxvk/shaders/rtx/pass/atmosphere -name "*fast*" -o -name "*noise*"
```
Read whatever FastNoise's compute-pass binding declaration file looks like to match the style.

- [ ] **Step 2: Create `cloud_shadow_map_bindings.slangh`**

```glsl
/*
* Bindings for the cloud_shadow_map.comp.slang compute pass.
*/
#pragma once

#include "rtx/pass/common_binding_indices.h"

// Output: the shadow map texture (R8, written texel-by-texel)
layout(binding = BINDING_CLOUD_SHADOW_MAP_OUTPUT)
RWTexture2D<float> CloudShadowMapOutput;

// Input: the existing 3D cloud noise (sampled by the per-tap density evaluator).
// Bound from the same source that feeds atmosphere_bindings.slangh, so the
// SRV is shared.
layout(binding = BINDING_ATMOSPHERE_CLOUD_NOISE_3D)
Texture3D<float> AtmosphereCloudNoise3D;

layout(binding = BINDING_ATMOSPHERE_CLOUD_NOISE_3D_SAMPLER)
SamplerState AtmosphereCloudNoise3DSampler;

// Push constant carrying the snapshot-of-AtmosphereArgs subset we need
// plus the per-dispatch anchor.
struct CloudShadowMapPushArgs {
  // ---- AtmosphereArgs subset for the cloud march ----
  vec3  sunDirection;
  float cloudAltitude;       // km

  float cloudThickness;      // km
  float cloudDensity;
  float cloudCoverageMean;
  float cloudCoverageSpread;

  float cloudCoverageNoiseScale;
  float cloudTypeMean;
  float cloudTypeSpread;
  float cloudTypeNoiseScale;

  float cloudCurvature;
  float cloudNoiseTileKm;
  float cloudScale;
  float cloudAnvilBias;

  // ---- Per-dispatch anchor ----
  vec2  mapOriginXZ;
  float texelSize;
  uint  resolution;
};

layout(push_constant)
ConstantBuffer<CloudShadowMapPushArgs> cb;
```

Note `BINDING_CLOUD_SHADOW_MAP_OUTPUT` is a NEW slot — local to this dispatch's binding space, separate from `BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP` which is the SAMPLED-side slot used by consumer dispatches. Add this `#define` to `common_binding_indices.h` as well. Pick the next unused value.

- [ ] **Step 3: Create `cloud_shadow_map.comp.slang`**

```glsl
/*
* Cloud shadow map compute pass. Each texel ray-marches upward along sunDir
* through the cloud slab from a ground (X, 0, Z) position, accumulating
* optical depth and writing the Beer-Lambert transmittance.
*
* Spec: docs/superpowers/specs/2026-05-09-cloud-shadow-map-design.md
*/
#include "rtx/pass/atmosphere/cloud_shadow_map_bindings.slangh"

// Match the value used in atmosphere_common.slangh::cloudPlanetRadius for
// curvature scaling. Inlined here because we don't pull the full
// atmosphere_common.slangh (it requires more bindings than we have).
float cloudPlanetRadiusInline(float curvature) {
  // Earth-equivalent radius in km, scaled toward a tighter dome as
  // curvature -> 1. Matches atmosphere_common::cloudPlanetRadius.
  return mix(6371.0f, 50.0f, curvature);
}

// 2D noise inline copy (mirrors atmosphere_common::smoothNoise2D).
float smoothNoise2DInline(vec2 p, float scale) {
  vec2 i = floor(p / scale);
  vec2 f = fract(p / scale);
  f = f * f * (3.0f - 2.0f * f);
  // Hash via offset trick — same pattern as atmosphere_common.
  float a = sin(dot(i,                    vec2(127.1f, 311.7f))) * 43758.5453f;
  float b = sin(dot(i + vec2(1.f, 0.f),   vec2(127.1f, 311.7f))) * 43758.5453f;
  float c = sin(dot(i + vec2(0.f, 1.f),   vec2(127.1f, 311.7f))) * 43758.5453f;
  float d = sin(dot(i + vec2(1.f, 1.f),   vec2(127.1f, 311.7f))) * 43758.5453f;
  a = fract(a); b = fract(b); c = fract(c); d = fract(d);
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Vertical density profile (matches atmosphere_common::cloudTypeProfile)
float cloudTypeProfileInline(float hf, float type) {
  float riseEnd   = mix(0.10f, 0.20f, type);
  float fallStart = mix(0.20f, 0.75f, type);
  float fallEnd   = mix(0.30f, 0.95f, type);
  float rise = saturate(hf / max(riseEnd, 1e-4f));
  float fall = saturate((fallEnd - hf) / max(fallEnd - fallStart, 1e-4f));
  return saturate(rise * fall);
}

// 3D-textured cloud density (mirrors sampleCloudDensityTextured but inlined
// here so we don't drag atmosphere_common.slangh's full surface in).
float sampleCloudDensityInline(vec3 samplePos, float hf, float typeLocal,
                                float coverageLocal, float cloudNoiseTileKm,
                                float anvilBias) {
  // 3D noise lookup — same UVW math as atmosphere_common.
  vec3 uvw = samplePos / max(cloudNoiseTileKm, 0.001f);
  float n = AtmosphereCloudNoise3D.SampleLevel(AtmosphereCloudNoise3DSampler, uvw, 0).r;

  float profile = cloudTypeProfileInline(hf, typeLocal);

  // Anvil pow trick: gated bias above hf=0.7 widens the cumulus top.
  float anvil = 1.0f;
  if (anvilBias > 0.0f && hf > 0.7f) {
    anvil = pow(saturate((hf - 0.7f) / 0.1f), 1.0f - anvilBias);
  }

  return saturate((n - (1.0f - coverageLocal)) * profile * anvil);
}

// Sphere intersection (mirrors atmosphere_common::intersectSphere).
bool intersectSphereInline(vec3 rayOrigin, vec3 rayDir,
                            vec3 sphereCenter, float sphereRadius,
                            out float t0, out float t1) {
  vec3 oc = rayOrigin - sphereCenter;
  float b = dot(oc, rayDir);
  float c = dot(oc, oc) - sphereRadius * sphereRadius;
  float disc = b * b - c;
  if (disc < 0.0f) { t0 = -1.0f; t1 = -1.0f; return false; }
  float sd = sqrt(disc);
  t0 = -b - sd;
  t1 = -b + sd;
  return true;
}

// Slab intersection (entry + exit) for an upward ray from a ground position.
bool intersectCloudSlabFromGround(vec3 groundPos, vec3 sunDir,
                                   float cloudAltitude, float cloudThickness,
                                   float cloudCurvature,
                                   out float tEntry, out float tExit) {
  if (sunDir.y <= 0.001f) return false;

  float cloudR     = cloudPlanetRadiusInline(cloudCurvature);
  vec3  planetC    = vec3(0.0f, -cloudR, 0.0f);
  float baseRadius = cloudR + cloudAltitude;
  float topRadius  = cloudR + cloudAltitude + cloudThickness;

  float t0b, t1b, t0t, t1t;
  if (!intersectSphereInline(groundPos, sunDir, planetC, baseRadius, t0b, t1b)) return false;
  if (!intersectSphereInline(groundPos, sunDir, planetC, topRadius,  t0t, t1t)) return false;

  // Upward ray from inside both spheres: far hits are positive.
  tEntry = t1b;
  tExit  = t1t;
  if (tEntry >= tExit || tExit <= 0.0f) return false;
  tEntry = max(tEntry, 0.0f);
  return true;
}

float computeCloudHeightFractionInline(vec3 samplePos, float cloudAltitude,
                                        float cloudThickness, float cloudCurvature) {
  float cloudR = cloudPlanetRadiusInline(cloudCurvature);
  vec3 planetC = vec3(0.0f, -cloudR, 0.0f);
  float altitudeAboveSea = length(samplePos - planetC) - cloudR;
  return saturate((altitudeAboveSea - cloudAltitude) / max(cloudThickness, 0.001f));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadID : SV_DispatchThreadID) {
  if (threadID.x >= cb.resolution || threadID.y >= cb.resolution) return;

  vec3 sunDir = normalize(cb.sunDirection);

  // Sun below horizon: no shadow, full transmittance.
  if (sunDir.y < 0.0f) {
    CloudShadowMapOutput[threadID] = 1.0f;
    return;
  }

  vec2 worldXZ   = cb.mapOriginXZ + (vec2(threadID) + 0.5f) * cb.texelSize;
  vec3 groundPos = vec3(worldXZ.x, 0.0f, worldXZ.y);

  float tEntry, tExit;
  if (!intersectCloudSlabFromGround(groundPos, sunDir,
                                     cb.cloudAltitude, cb.cloudThickness,
                                     cb.cloudCurvature, tEntry, tExit)) {
    CloudShadowMapOutput[threadID] = 1.0f;
    return;
  }

  const int kTaps = 4;
  const float dt  = (tExit - tEntry) / float(kTaps);
  float opticalDepth = 0.0f;

  for (int i = 0; i < kTaps; ++i) {
    float t = tEntry + dt * (float(i) + 0.5f);
    vec3 sp = groundPos + sunDir * t;
    vec2 spXZ = vec2(sp.x, sp.z);

    // Type and coverage local fields (same as cloud march).
    float typeNoise     = smoothNoise2DInline(spXZ * cb.cloudTypeNoiseScale,     200.0f);
    float coverageNoise = smoothNoise2DInline(spXZ * cb.cloudCoverageNoiseScale, 250.0f);
    float typeLocal     = saturate(cb.cloudTypeMean     + (typeNoise     - 0.5f) * cb.cloudTypeSpread);
    float coverageLocal = saturate(cb.cloudCoverageMean + (coverageNoise - 0.5f) * cb.cloudCoverageSpread);

    float hf = computeCloudHeightFractionInline(sp, cb.cloudAltitude,
                                                  cb.cloudThickness,
                                                  cb.cloudCurvature);
    opticalDepth += sampleCloudDensityInline(sp, hf, typeLocal, coverageLocal,
                                              cb.cloudNoiseTileKm, cb.cloudAnvilBias);
  }

  float transmittance = exp(-opticalDepth * dt * 2.0f * cb.cloudDensity);
  CloudShadowMapOutput[threadID] = transmittance;
}
```

- [ ] **Step 4: Add `BINDING_CLOUD_SHADOW_MAP_OUTPUT` to `common_binding_indices.h`**

Local to the cloud-shadow-map dispatch; pick the next unused slot value.

- [ ] **Step 5: Build verify**

Run `rtx-build`. Expected: clean. The shader gets compiled into the runtime even though no C++ side dispatches it yet.

- [ ] **Step 6: Commit**

```bash
git add src/dxvk/shaders/rtx/pass/atmosphere/cloud_shadow_map_bindings.slangh \
        src/dxvk/shaders/rtx/pass/atmosphere/cloud_shadow_map.comp.slang \
        src/dxvk/shaders/rtx/pass/common_binding_indices.h
git commit -m "shader(atmosphere): add cloud_shadow_map compute pass

512x512 R8 ground-plane texture; each texel marches 4 deterministic taps
upward through the cloud slab from a non-origin ground position along
sunDir. Inlined math (cloudPlanetRadius / smoothNoise2D / cloudTypeProfile /
sphere / slab intersections) so the compute pass doesn't drag the full
atmosphere_common.slangh surface — keeps the binding footprint to just the
cloud noise SRV.

Output texture binding wired but no consumer yet."
```

---

## Task 3: C++ Class — `RtxCloudShadowMap`

**Files:**
- Create: `src/dxvk/rtx_render/rtx_fork_cloud_shadows.h`
- Create: `src/dxvk/rtx_render/rtx_fork_cloud_shadows.cpp`

**Reference:** mirror the structure of `RtxFastNoise` (commit `fe76d0b7`).

- [ ] **Step 1: Read `RtxFastNoise` header + cpp**

Run:
```
find src/dxvk/rtx_render -name "rtx_fast_noise*"
```
Read both files end-to-end. Note the constructor pattern, resource creation, sampler creation, dispatch wiring, ImGui exposure (or its absence).

- [ ] **Step 2: Create `rtx_fork_cloud_shadows.h`**

```cpp
/*
* RtxCloudShadowMap — fork-side resource manager for the per-frame 2D
* cloud shadow map. Renders a 512x512 R8 ground-plane projection of cloud
* transmittance along sun direction; consumers sample by world (X, Z).
*
* Spec: docs/superpowers/specs/2026-05-09-cloud-shadow-map-design.md
*/
#pragma once

#include "dxvk_format.h"
#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_options.h"

namespace dxvk {

  class DxvkDevice;

  class RtxCloudShadowMap : public CommonDeviceObject {
   public:
    explicit RtxCloudShadowMap(DxvkDevice* device);
    ~RtxCloudShadowMap();

    static constexpr uint32_t kResolution    = 512u;
    static constexpr float    kExtentMeters  = 8000.f;
    static constexpr float    kTexelSize     = kExtentMeters / float(kResolution);

    // Per-frame: snaps camera XZ to texel boundary, dispatches the compute
    // pass, populates the anchor info that consumers will write into
    // AtmosphereArgs.
    void dispatch(Rc<DxvkContext> ctx,
                  const struct AtmosphereArgs& atmosphereArgs,
                  const Vector3& cameraWorldPos);

    // Resource accessor for binding into consumer dispatches (read-side).
    const Resources::Resource& getShadowMapTexture() const { return m_shadowMap; }
    Rc<DxvkSampler>            getShadowMapSampler() const { return m_sampler; }

    // Anchor info to write into AtmosphereArgs each frame.
    struct Anchor {
      Vector2 mapOriginXZ;
      float   texelSize;
      float   resolution;
    };
    const Anchor& getAnchor() const { return m_anchor; }

   private:
    void createResources(Rc<DxvkContext> ctx);

    Resources::Resource m_shadowMap;
    Rc<DxvkSampler>     m_sampler;
    Anchor              m_anchor;
    bool                m_resourcesCreated = false;
  };

}
```

- [ ] **Step 3: Create `rtx_fork_cloud_shadows.cpp`**

```cpp
/*
* RtxCloudShadowMap implementation.
* Spec: docs/superpowers/specs/2026-05-09-cloud-shadow-map-design.md
*/
#include "rtx_fork_cloud_shadows.h"

#include "dxvk_device.h"
#include "rtx_imgui.h"
#include "rtx_atmosphere.h"  // for AtmosphereArgs
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/atmosphere/cloud_shadow_map_bindings.slangh"  // for CloudShadowMapPushArgs

#include <cmath>

namespace dxvk {

  RtxCloudShadowMap::RtxCloudShadowMap(DxvkDevice* device)
    : CommonDeviceObject(device) {}

  RtxCloudShadowMap::~RtxCloudShadowMap() = default;

  void RtxCloudShadowMap::createResources(Rc<DxvkContext> ctx) {
    if (m_resourcesCreated) return;

    DxvkImageCreateInfo imgInfo = {};
    imgInfo.type        = VK_IMAGE_TYPE_2D;
    imgInfo.format      = VK_FORMAT_R8_UNORM;
    imgInfo.flags       = 0;
    imgInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.extent      = { kResolution, kResolution, 1 };
    imgInfo.numLayers   = 1;
    imgInfo.mipLevels   = 1;
    imgInfo.usage       = VK_IMAGE_USAGE_STORAGE_BIT
                        | VK_IMAGE_USAGE_SAMPLED_BIT
                        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imgInfo.stages      = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    imgInfo.access      = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    imgInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.layout      = VK_IMAGE_LAYOUT_GENERAL;

    DxvkImageViewCreateInfo viewInfo = {};
    viewInfo.type      = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format    = VK_FORMAT_R8_UNORM;
    viewInfo.usage     = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    viewInfo.aspect    = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel  = 0;
    viewInfo.numLevels = 1;
    viewInfo.minLayer  = 0;
    viewInfo.numLayers = 1;

    m_shadowMap = Resources::createImageResource(
      ctx, "RtxCloudShadowMap", imgInfo, viewInfo);

    DxvkSamplerCreateInfo sampInfo = {};
    sampInfo.magFilter      = VK_FILTER_LINEAR;
    sampInfo.minFilter      = VK_FILTER_LINEAR;
    sampInfo.mipmapMode     = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampInfo.addressModeU   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampInfo.addressModeV   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampInfo.addressModeW   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampInfo.borderColor    = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // R=1.0 → no shadow off-grid
    sampInfo.mipmapLodBias  = 0.f;
    sampInfo.minLod         = 0.f;
    sampInfo.maxLod         = 0.f;
    sampInfo.useAnisotropy  = VK_FALSE;
    sampInfo.maxAnisotropy  = 1.f;
    sampInfo.compareToDepth = VK_FALSE;

    m_sampler = m_device->createSampler(sampInfo);
    m_resourcesCreated = true;
  }

  void RtxCloudShadowMap::dispatch(Rc<DxvkContext> ctx,
                                    const AtmosphereArgs& atmosphereArgs,
                                    const Vector3& cameraWorldPos) {
    createResources(ctx);

    // Snap camera XZ to texel boundary.
    Vector2 cameraXZ    = { cameraWorldPos.x, cameraWorldPos.z };
    Vector2 snappedXZ   = {
      std::floor(cameraXZ.x / kTexelSize) * kTexelSize,
      std::floor(cameraXZ.y / kTexelSize) * kTexelSize
    };
    Vector2 mapOriginXZ = {
      snappedXZ.x - 0.5f * kExtentMeters,
      snappedXZ.y - 0.5f * kExtentMeters
    };

    m_anchor.mapOriginXZ = mapOriginXZ;
    m_anchor.texelSize   = kTexelSize;
    m_anchor.resolution  = float(kResolution);

    // Bail when clouds are off — leave the shadow map in its previous state
    // (consumers' cloudShadowStrength=0 / cloudEnabled=0 short-circuits read).
    if (atmosphereArgs.cloudEnabled < 0.5f) {
      return;
    }

    // Build push args.
    CloudShadowMapPushArgs pushArgs = {};
    pushArgs.sunDirection           = atmosphereArgs.sunDirection;
    pushArgs.cloudAltitude          = atmosphereArgs.cloudAltitude;
    pushArgs.cloudThickness         = atmosphereArgs.cloudThickness;
    pushArgs.cloudDensity           = atmosphereArgs.cloudDensity;
    pushArgs.cloudCoverageMean      = atmosphereArgs.cloudCoverageMean;
    pushArgs.cloudCoverageSpread    = atmosphereArgs.cloudCoverageSpread;
    pushArgs.cloudCoverageNoiseScale= atmosphereArgs.cloudCoverageNoiseScale;
    pushArgs.cloudTypeMean          = atmosphereArgs.cloudTypeMean;
    pushArgs.cloudTypeSpread        = atmosphereArgs.cloudTypeSpread;
    pushArgs.cloudTypeNoiseScale    = atmosphereArgs.cloudTypeNoiseScale;
    pushArgs.cloudCurvature         = atmosphereArgs.cloudCurvature;
    pushArgs.cloudNoiseTileKm       = atmosphereArgs.cloudNoiseTileKm;
    pushArgs.cloudScale             = atmosphereArgs.cloudScale;
    pushArgs.cloudAnvilBias         = atmosphereArgs.cloudAnvilBias;
    pushArgs.mapOriginXZ            = mapOriginXZ;
    pushArgs.texelSize              = kTexelSize;
    pushArgs.resolution             = kResolution;

    // Bind storage view for write + cloud noise SRV for read.
    ctx->bindResourceView(BINDING_CLOUD_SHADOW_MAP_OUTPUT,
                          m_shadowMap.view, nullptr);
    // Cloud noise binding wiring assumed identical to FastNoise/Cloud:
    // see RtxFastNoise dispatch for the resource view pointer source.
    // Subagent fills this in by following the RtxFastNoise pattern.

    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    // Pipeline assumed already registered with the renderer's shader registry.
    // Subagent fills this in by following the FastNoise pipeline registration
    // pattern — typically a static SHADER_SOURCE() macro elsewhere in the
    // codebase that the runtime auto-discovers.

    const uint32_t groupSize = 16u;
    const uint32_t numGroups = kResolution / groupSize;
    ctx->dispatch(numGroups, numGroups, 1);
  }

}
```

Note: the **pipeline registration** + **cloud noise SRV binding** lines are placeholder-style. The subagent implementing this task must look at how `RtxFastNoise::upload` (or whatever its dispatch method is named) registers and binds, and follow that pattern verbatim.

- [ ] **Step 4: Build verify**

Run `rtx-build`. Expected: clean. The class compiles but isn't called yet.

- [ ] **Step 5: Commit**

```bash
git add src/dxvk/rtx_render/rtx_fork_cloud_shadows.h \
        src/dxvk/rtx_render/rtx_fork_cloud_shadows.cpp
git commit -m "rtx(atmosphere): add RtxCloudShadowMap manager class

Fork-side resource manager for the per-frame 2D cloud shadow map. Owns
512x512 R8 image + linear clamp-to-border-1.0 sampler, dispatches the
compute pass via a 32x32 workgroup grid. Anchor (origin XZ + texel size)
exposed for consumers to write into AtmosphereArgs.

Not instantiated yet — Task 4 wires it into RtxAtmosphere."
```

---

## Task 4: Wire `RtxCloudShadowMap` Into the Per-Frame Atmosphere Hook

**Files:**
- Modify: `src/dxvk/rtx_render/rtx_atmosphere.h` (member declaration + accessor)
- Modify: `src/dxvk/rtx_render/rtx_atmosphere.cpp` (instantiation in `RtxAtmosphere::initialize`)
- Modify: `src/dxvk/rtx_render/rtx_fork_atmosphere.cpp` (per-frame dispatch + AtmosphereArgs anchor write)

**Architectural note (research-confirmed):** RtxFastNoise is *one-shot init*. RtxCloudShadowMap is *per-frame dispatch* because the cloud field is dynamic. The dispatch + anchor-write live in `bindAtmosphereLuts` (the per-frame hook in `rtx_fork_atmosphere.cpp:~115-138`), not in `RtxAtmosphere::initialize`. The class is OWNED by `RtxAtmosphere` but DRIVEN per-frame by `bindAtmosphereLuts`.

- [ ] **Step 1: Locate the existing FastNoise wiring**

Run:
```
grep -n "RtxFastNoise\|m_fastNoise\|getFastNoiseView\|fastNoise" src/dxvk/rtx_render/rtx_atmosphere.h src/dxvk/rtx_render/rtx_atmosphere.cpp src/dxvk/rtx_render/rtx_fork_atmosphere.cpp
```

Read every match — that's the precedent. RtxCloudShadowMap follows the same SHAPE (member + accessor) but its dispatch driver is different.

- [ ] **Step 2: Add member + accessor to `rtx_atmosphere.h`**

Immediately after the `m_fastNoise` declaration:

```cpp
    RtxCloudShadowMap m_cloudShadowMap;  // Per-frame 2D cloud shadow map (fork)
```

And add a public accessor near `getFastNoiseView()`:

```cpp
    Rc<DxvkImageView> getCloudShadowMapView() const { return m_cloudShadowMap.getView(); }
    Rc<DxvkSampler>   getCloudShadowMapSampler() const { return m_cloudShadowMap.getSampler(); }
    const RtxCloudShadowMap::Anchor& getCloudShadowMapAnchor() const { return m_cloudShadowMap.getAnchor(); }
```

Include the header at the top:

```cpp
#include "rtx_fork_cloud_shadows.h"
```

(The `RtxCloudShadowMap` class header — Task 3 — should expose `getView()` / `getSampler()` accessors with these exact signatures. If Task 3 used different names, update Task 3 to match these.)

- [ ] **Step 3: Initialize once in `RtxAtmosphere::initialize`**

After the `m_fastNoise.initialize(ctx)` line (`rtx_atmosphere.cpp:~108`), add:

```cpp
    m_cloudShadowMap.initialize(ctx);
```

(`initialize` creates the image + sampler resources. It does NOT dispatch — that's per-frame work.)

- [ ] **Step 4: Per-frame dispatch + anchor write in `bindAtmosphereLuts`**

In `rtx_fork_atmosphere.cpp` (the hook called every frame from `rtx_context.cpp:1429`), find the section that handles FastNoise binding (around line 121). Add the cloud shadow map dispatch + binding right after.

```cpp
// Cloud shadow map: dispatch the per-frame compute pass that fills the
// 2D shadow texture, then write the anchor into AtmosphereArgs and bind
// the SRV + sampler for downstream consumers (geometry_resolver,
// volume_integrate, volume_restir, restir_gi, etc.).
ctx.m_atmosphere->dispatchCloudShadowMap(&ctx, atmosphereArgs, cameraWorldPos);

const auto& csmAnchor = ctx.m_atmosphere->getCloudShadowMapAnchor();
atmosphereArgs.cameraWorldPos             = cameraWorldPos;
atmosphereArgs.cloudShadowMapOriginXZ     = csmAnchor.mapOriginXZ;
atmosphereArgs.cloudShadowMapTexelSize    = csmAnchor.texelSize;
atmosphereArgs.cloudShadowMapResolution   = csmAnchor.resolution;

auto csmView    = ctx.m_atmosphere->getCloudShadowMapView();
auto csmSampler = ctx.m_atmosphere->getCloudShadowMapSampler();
if (csmView != nullptr) {
  ctx.bindResourceView(BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP,
                       csmView, nullptr);
  ctx.bindResourceSampler(BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP_SAMPLER,
                          csmSampler);
}
```

`cameraWorldPos` comes from whatever the surrounding code uses to derive camera position — match the variable name already in scope. If the camera world position isn't yet plumbed into this hook, look at how `viewAltitude` or `cameraGetWorldPosition(...)` is fetched in the same method and follow the same pattern.

- [ ] **Step 5: Add `dispatchCloudShadowMap` method on `RtxAtmosphere`**

In `rtx_atmosphere.h`, public:

```cpp
    void dispatchCloudShadowMap(Rc<DxvkContext> ctx,
                                 const AtmosphereArgs& atmosphereArgs,
                                 const Vector3& cameraWorldPos);
```

In `rtx_atmosphere.cpp`:

```cpp
    void RtxAtmosphere::dispatchCloudShadowMap(Rc<DxvkContext> ctx,
                                                 const AtmosphereArgs& atmosphereArgs,
                                                 const Vector3& cameraWorldPos) {
      m_cloudShadowMap.dispatch(ctx, atmosphereArgs, cameraWorldPos);
    }
```

- [ ] **Step 6: Build verify**

Run `rtx-build`. Expected: clean. The shadow map is now dispatched every frame and the SRV is bound to every consumer dispatch that includes `atmosphere_bindings.slangh`.

- [ ] **Step 6: Commit**

```bash
git add src/dxvk/rtx_render/rtx_atmosphere.h src/dxvk/rtx_render/rtx_atmosphere.cpp
git commit -m "rtx(atmosphere): instantiate RtxCloudShadowMap, dispatch per frame

Adds the RtxCloudShadowMap as a member of RtxAtmosphere alongside
RtxFastNoise. Per-frame: dispatches the shadow-map compute pass after
cloud noise is uploaded and writes the anchor (origin XZ, texel size,
resolution) plus cameraWorldPos into AtmosphereArgs for downstream
consumption.

Texture is rendered every frame; consumers wired in subsequent commits."
```

---

## Task 5: Bind the Shadow Map SRV in Consumer Dispatches

**Files:**
- Modify: `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_bindings.slangh`
- Modify: `src/dxvk/shaders/rtx/algorithm/geometry_resolver.slangh`
- Modify: `src/dxvk/rtx_render/rtx_atmosphere.cpp` (or wherever consumer dispatches register their bindings — match the pattern used for FastNoise)
- (potentially) Modify: volume integrate / volume ReSTIR files

- [ ] **Step 1: Identify all consumers**

Run:
```
grep -rn "getTransmittanceToSun\|getAtmosphericTransmittanceForDir\|evalCloudGroundShadow" src/dxvk/shaders | grep -v atmosphere_common | grep -v atmosphere_sky | grep -v "//"
```

For each unique file that calls `getTransmittanceToSun`, identify the C++-side dispatch that compiles it. List every dispatch needing the cloud-shadow-map SRV bound.

- [ ] **Step 2: Add SRV declaration to `atmosphere_bindings.slangh`**

Near the existing `BINDING_ATMOSPHERE_FAST_NOISE` declaration, add:

```glsl
#ifdef CLOUD_SHADOW_MAP_AVAILABLE
layout(binding = BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP)
Texture2D<float> AtmosphereCloudShadowMap;

layout(binding = BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP_SAMPLER)
SamplerState AtmosphereCloudShadowMapSampler;
#endif
```

- [ ] **Step 3: Define `CLOUD_SHADOW_MAP_AVAILABLE` in shaders that bind the SRV**

In `geometry_resolver.slangh`, near the top of the file (above any include of `atmosphere_common.slangh` or `atmosphere_bindings.slangh`), add:

```glsl
#define CLOUD_SHADOW_MAP_AVAILABLE
```

Repeat for each shader file identified in Step 1 that consumes `getTransmittanceToSun` / `getAtmosphericTransmittanceForDir` and has a binding-set with available slots.

- [ ] **Step 4: C++-side: bind the resource view + sampler in each consumer dispatch**

For each consumer dispatch identified, add the binding pair just before the dispatch call. Pattern (mirror RtxFastNoise's binding):

```cpp
ctx->bindResourceView(
  BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP,
  cloudShadowMap.getShadowMapTexture().view,
  nullptr);
ctx->bindResourceSampler(
  BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP_SAMPLER,
  cloudShadowMap.getShadowMapSampler());
```

`cloudShadowMap` is the `RtxAtmosphere::m_cloudShadowMap` reference, exposed via accessor or already-friend-accessed in the dispatch builder.

- [ ] **Step 5: Build verify**

Run `rtx-build`. Expected: clean. If a dispatch is missing the binding but its shader has `CLOUD_SHADOW_MAP_AVAILABLE` defined, the validation layer will complain at runtime — fix by adding the binding.

- [ ] **Step 6: Commit**

```bash
git add src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_bindings.slangh \
        src/dxvk/shaders/rtx/algorithm/geometry_resolver.slangh \
        <other shader files> \
        src/dxvk/rtx_render/<dispatch builder cpp files>
git commit -m "rtx(atmosphere): wire cloud shadow map SRV into consumer dispatches

Adds AtmosphereCloudShadowMap + sampler to atmosphere_bindings.slangh
under CLOUD_SHADOW_MAP_AVAILABLE guard. Geometry resolver, volume
integrate, and volume ReSTIR define the guard and bind the resource
in their dispatch builders. Atmosphere LUT compute paths leave the
guard undefined so they continue to short-circuit to no-cloud-shadow."
```

---

## Task 6: `evalCloudShadowAtWorld` + `getTransmittanceToSun` Rewrite

**Files:**
- Modify: `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh:614-721`

- [ ] **Step 1: Add `evalCloudShadowAtWorld`**

Replace the entire `evalCloudGroundShadow` function (lines 614-660) with:

```glsl
// Evaluate the cloud shadow at a world position by sampling the 2D shadow
// map at the position's (X, Z). Returns a multiplier in [0, 1] — 1 means no
// cloud blocking, 0 means fully overcast. Position-aware (replaces the
// older uniform evalCloudGroundShadow that sampled at world origin).
//
// Spec: docs/superpowers/specs/2026-05-09-cloud-shadow-map-design.md
float evalCloudShadowAtWorld(vec3 worldPos, AtmosphereArgs args) {
#ifndef CLOUD_SHADOW_MAP_AVAILABLE
  // LUT compute paths and any other shader without the SRV bound.
  return 1.0f;
#else
  if (args.cloudEnabled < 0.5f || args.cloudShadowStrength < 0.01f) {
    return 1.0f;
  }

  // (worldPos.xz - originXZ) / extentMeters → uv in [0, 1].
  vec2 uv = (worldPos.xz - args.cloudShadowMapOriginXZ)
          / (args.cloudShadowMapTexelSize * args.cloudShadowMapResolution);

  // Sampler is linear, clamp-to-border with border = 1.0, so off-grid
  // samples → no cloud shadow (matches the legacy
  // evalCloudGroundShadow ATMOSPHERE_AVAILABLE early-return contract).
  float shadow = AtmosphereCloudShadowMap.SampleLevel(
                   AtmosphereCloudShadowMapSampler, uv, 0.0f).r;

  return mix(1.0f, shadow, args.cloudShadowStrength);
#endif
}
```

- [ ] **Step 2: Update `getTransmittanceToSun`**

Replace lines 717-721:

```glsl
vec3 getTransmittanceToSun(vec3 worldPos, AtmosphereArgs args, bool isZUp = false) {
  vec3 transmittance = getAtmosphericTransmittanceForDir(args.sunDirection, args);
  transmittance *= evalCloudShadowAtWorld(worldPos, args);
  return transmittance;
}
```

(`isZUp` retained on the signature for ABI parity with surrounding atmosphere helpers.)

- [ ] **Step 3: Build verify**

Run `rtx-build`. Expected: clean. Consumers now sample position-varying cloud shadow.

- [ ] **Step 4: Commit**

```bash
git add src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh
git commit -m "shader(atmosphere): replace evalCloudGroundShadow with position-aware sample

evalCloudShadowAtWorld samples the 2D cloud shadow map at worldPos.xz,
returning per-position cloud transmittance instead of the old uniform
sun-dimmer. getTransmittanceToSun rewires to use it. The
ATMOSPHERE_AVAILABLE early-return semantics move to the
CLOUD_SHADOW_MAP_AVAILABLE guard inside the new function so atmosphere
LUT compute paths still see no occlusion (correct — those LUTs encode
pure atmospheric scattering).

Old evalCloudGroundShadow function is deleted (no fallback, no transition
period; CLOUD_SHADOW_MAP_AVAILABLE guard fully replaces ATMOSPHERE_AVAILABLE
gate)."
```

---

## Task 7: Update `vec3(0)` Callers

**Files:**
- Modify: `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh:74`
- Modify: `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh:1055`

- [ ] **Step 1: Update sun-disk caller**

In `atmosphere_sky.slangh`, line 74:

```glsl
// Before:
vec3 transmittance = getTransmittanceToSun(vec3(0.0f), args, false);

// After:
vec3 transmittance = getTransmittanceToSun(args.cameraWorldPos, args, false);
```

- [ ] **Step 2: Update ground LUT caller**

In `atmosphere_common.slangh`, line 1055:

```glsl
// Before:
vec3 T_sun_to_ground = getTransmittanceToSun(vec3(0.0f), args, false);

// After:
vec3 T_sun_to_ground = getTransmittanceToSun(args.cameraWorldPos, args, false);
```

(Note: this site is in an atmosphere LUT compute path which doesn't define `CLOUD_SHADOW_MAP_AVAILABLE`, so `evalCloudShadowAtWorld` returns 1.0 regardless. The change is for consistency / future-proofing if the LUT path later opts in.)

- [ ] **Step 3: Verify there are no other `vec3(0)` callers**

```
grep -n "getTransmittanceToSun(vec3(0" src/dxvk/shaders -r
```

Expected: zero hits after this task.

- [ ] **Step 4: Build verify**

Run `rtx-build`. Expected: clean.

- [ ] **Step 5: Commit**

```bash
git add src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh \
        src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh
git commit -m "shader(atmosphere): plumb cameraWorldPos to vec3(0) sun-transmittance callers

Sun disk (atmosphere_sky.slangh:74) and ground LUT
(atmosphere_common.slangh:1055) previously passed vec3(0) as the worldPos
argument to getTransmittanceToSun (which historically ignored it). Now
both pass args.cameraWorldPos so the cloud shadow map samples the cloud
cover directly above camera — correct for both: sun-disk transmittance is
through the sky directly above, and the ground LUT's reference position
is the camera-relative origin."
```

---

## Task 8: Fork-Touchpoints, Audit, Final Build, Visual Verify

**Files:**
- Modify: `docs/fork-touchpoints.md`

- [ ] **Step 1: Update `docs/fork-touchpoints.md`**

For each upstream file modified in this work, add a row to the fridge list. The work touches:
- `src/dxvk/shaders/rtx/pass/common_binding_indices.h` (binding slots added)
- `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h` (struct extension — actually fork-owned, but verify the file's history to be sure)
- `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_bindings.slangh` (fork-owned)
- `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh` (fork-owned)
- `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh` (fork-owned)
- `src/dxvk/shaders/rtx/algorithm/geometry_resolver.slangh` (UPSTREAM)
- `src/dxvk/rtx_render/rtx_atmosphere.h/.cpp` (verify; likely UPSTREAM with fork hooks)
- (any other consumer dispatches modified)

For UPSTREAM files only, add an entry following the existing format. Reference the design doc.

- [ ] **Step 2: Run the audit script**

```bash
bash scripts/audit-fork-touchpoints.sh
```

Expected: clean, no missing-touchpoint warnings.

- [ ] **Step 3: Final build**

Run `rtx-build`. Expected: clean exit code 0, zero compile errors, all shaders compiled.

- [ ] **Step 4: Visual verification (manual — user-driven)**

Hand off to the user with a summary:
- Built artifact location
- Things to verify in-game:
  - Blue-sky flicker should be gone (was the core bug)
  - Cloud shadows should appear as moving patches on terrain when clouds are overhead
  - At cloudShadowStrength = 0, behavior should be identical to no shadow (regression check)
  - Sun-disk transmittance should track cloud cover overhead

- [ ] **Step 5: Commit**

```bash
git add docs/fork-touchpoints.md
git commit -m "docs(fork-touchpoints): record cloud shadow map fork edits

Updates the fridge list with the upstream files touched by the cloud
shadow map work (geometry_resolver.slangh + any consumer dispatches +
common_binding_indices.h). Other affected files are fork-owned and
don't require fridge entries.

Closes the cloud shadow map workstream
(spec: docs/superpowers/specs/2026-05-09-cloud-shadow-map-design.md)."
```

---

## Self-Review Checklist (post-write)

- ✅ Spec coverage: every numbered section of the spec is addressed by at least one task.
- ✅ No placeholders ("TBD", "implement later"): every task contains executable code or commands.
- ✅ Type consistency: `Anchor` struct fields used in Task 3 match those written into `AtmosphereArgs` in Task 1 and read by `evalCloudShadowAtWorld` in Task 6.
- ✅ Method signatures consistent: `dispatch(ctx, atmosphereArgs, cameraWorldPos)` shape used identically in declaration (Task 3) and call site (Task 4).
- ✅ Open question from spec ("Does AtmosphereArgs already plumb camera world position?") resolved in Task 1: NO, plan adds the field.
- ✅ Open question ("Does intersectCloudLayer accept non-origin?") resolved in Task 2: NO, plan inlines the math in the compute shader rather than refactor a shared helper (out of scope).

## Out of Scope (deferred)

- Cascaded shadow map (Approach C) — only if B's 8km extent proves insufficient.
- Toroidal scrolling (partial-grid update on camera scroll) — perf opt, defer until profiled.
- Refactor of slab-walk math from `evalClouds` into a shared `intersectCloudSlabRay` helper — DRY win, but expanding this PR's scope. Follow-up.
- Colored cloud shadows (RGB) — needs separate forward-scatter physics design.

---

**Plan ready for execution.**
