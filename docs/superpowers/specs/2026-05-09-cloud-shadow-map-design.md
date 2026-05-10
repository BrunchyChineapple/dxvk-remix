# Cloud Shadow Map — Design Spec

**Date:** 2026-05-09
**Status:** Approved (sections 1–5)
**Replaces:** `evalCloudGroundShadow()` in `atmosphere_common.slangh`
**Motivation:** Two coupled goals.

## Background — what's wrong today

`evalCloudGroundShadow` is a single-point sun-direction sample at world origin
that returns a scalar applied as a uniform sun-dimmer to every consumer of
`getTransmittanceToSun`. Every blade of grass and every volumetric froxel sees
the *same* cloud occlusion value per frame, regardless of position. This:

1. **Isn't a real cloud shadow.** No spatial variation on terrain — overcast
   doesn't make clouds-overhead-darker, just everything-darker-uniformly.
2. **Triggers a downstream temporal-accumulator divergence** in the volume
   integrator path (`getTransmittanceToSun` at
   `atmosphere_common.slangh:1022`), manifesting as observable flicker on
   blue sky between clouds, scaling with cloud density, growing in
   amplitude over 2–5 minutes of gameplay. Confirmed 2026-05-09 by
   bypassing the function (`cloudShadowStrength = 0` stops the flicker).
   The cloud-shadow value is per-frame deterministic, so the variance is
   downstream — but the uniform-multiplier shape is the *trigger*.

## Solution — 2D cloud shadow map

A standard technique (Decima, Frostbite, Nubis): per-frame compute pass
renders a 2D projection of cloud transmittance onto a ground-plane texture
parameterized by world (X, Z). Each texel ray-marches **upward** through the
cloud layer along sun direction, returning Beer-Lambert transmittance.
Surface NEE and volume inscatter sample the map at their world (X, Z),
getting position-varying cloud shadow.

### Why this design (not alternatives)

Three approaches were considered:

- **A:** 1024² high-res over 4km — overshoots physical resolution
  (penumbra ≈ 7m at 1.5km cloud altitude bounds meaningful detail to
  ~10m features). Hard horizon cutoff at 2km from camera bothers FNV's
  long-distance Mojave sightlines.
- **B (chosen):** 512² over 8km — 15.6m/texel matches the natural
  penumbra. Wider extent covers FNV's typical sightlines without a
  visible cutoff seam. ~16× cheaper than A.
- **C:** Cascaded near+far — overengineering until B proves
  insufficient. Defer.

## Configuration

| Constant | Value | Rationale |
|---|---|---|
| `kResolution` | 512 | 512² = 256KB R8, fits SLM/cache budget cleanly |
| `kExtentMeters` | 8000 | Covers FNV's typical view distance with margin |
| `kTexelSize` | 15.625m | = extent / resolution, sits at physical penumbra |
| `kTaps` | 4 | Avoids 2-tap Jensen-bias trap; cheap at low resolution |
| Texture format | R8_UNORM | Scalar transmittance, [0, 1], ~1KB header overhead |
| Sampler | Linear, clamp-to-border, border=1 | Off-grid → no shadow |

## Architecture

### Files added

```
src/dxvk/rtx_render/rtx_fork_cloud_shadows.h
src/dxvk/rtx_render/rtx_fork_cloud_shadows.cpp
src/dxvk/shaders/rtx/pass/atmosphere/cloud_shadow_map.comp.slang
src/dxvk/shaders/rtx/pass/atmosphere/cloud_shadow_map_bindings.slangh
```

### Files modified (fork-touchpoints)

```
src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh
  — getTransmittanceToSun rewires to evalCloudShadowAtWorld
  — evalCloudGroundShadow deleted
src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_bindings.slangh
  — adds BINDING_ATMOSPHERE_CLOUD_SHADOW_MAP slot declaration
src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh
  — line 74 vec3(0) → cameraWorldPosXYZ
src/dxvk/shaders/rtx/pass/common_binding_indices.h
  — adds slot enum value
src/dxvk/rtx_render/rtx_atmosphere.cpp/.h
  — instantiates RtxCloudShadowMap, dispatches per frame
src/dxvk/shaders/rtx/algorithm/geometry_resolver.slangh
  — bind the new SRV on the atmosphere-evaluating dispatch
docs/fork-touchpoints.md
  — record the new touch
```

### Class — `RtxCloudShadowMap`

Fork-side (`rtx_fork_cloud_shadows.h/cpp`), instantiated as a member of
`RtxAtmosphere` (analogous to `RtxFastNoise` as the closest precedent).

```cpp
class RtxCloudShadowMap : public CommonDeviceObject {
 public:
  explicit RtxCloudShadowMap(DxvkDevice* device);

  // Called per-frame from RtxAtmosphere, before any consumer dispatches.
  // Captures camera world XZ, snaps to texel boundary, dispatches the
  // 32×32 workgroup compute pass that fills m_shadowMap with R8 cloud
  // transmittance.
  void dispatch(Rc<DxvkContext> ctx,
                const RtxAtmosphereArgs& atmosphereArgs,
                const vec3& cameraWorldPos);

  // Resource accessor for binding into consumer dispatches.
  const Resources::Resource& getShadowMapTexture() const { return m_shadowMap; }

  // Anchor info pushed into AtmosphereArgs for sample-time UV computation.
  struct Anchor {
    vec2 mapOriginXZ;
    float texelSize;
    float resolution;
  };
  Anchor getAnchor() const { return m_anchor; }

 private:
  void createResources(Rc<DxvkContext> ctx);

  Resources::Resource m_shadowMap;
  Rc<DxvkSampler>     m_sampler;
  Anchor              m_anchor;
};
```

### Compute shader — `cloud_shadow_map.comp.slang`

Inputs (read-only):
- `AtmosphereCloudNoise3D` (the existing 3D noise)
- `AtmosphereCloudNoise3DSampler`
- `AtmosphereArgs` (cloudAltitude, cloudThickness, cloudCoverageMean/Spread,
  cloudTypeMean/Spread, cloudDensity, cloudTypeNoiseScale,
  cloudCoverageNoiseScale, sunDirection)
- Push constant: `mapOriginXZ`, `texelSize`, `resolution`

Output:
- `RWTexture2D<float> ShadowMap` at `BINDING_CLOUD_SHADOW_MAP_OUTPUT`

Per-thread:
```glsl
[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadID : SV_DispatchThreadID) {
  if (threadID.x >= cb.resolution || threadID.y >= cb.resolution) return;

  vec3 sunDir = normalize(cb.atmosphereArgs.sunDirection);
  if (sunDir.y < 0.0f) {
    ShadowMap[threadID] = 1.0f;
    return;
  }

  vec2 worldXZ = cb.mapOriginXZ + (vec2(threadID) + 0.5f) * cb.texelSize;
  vec3 groundPos = vec3(worldXZ.x, 0.0f, worldXZ.y);

  // Slab intersection (re-uses atmosphere_common helper)
  float t0, t1;
  if (!intersectCloudSlabFromGround(groundPos, sunDir, cb.atmosphereArgs, t0, t1)) {
    ShadowMap[threadID] = 1.0f;
    return;
  }

  const int kTaps = 4;
  const float dt = (t1 - t0) / float(kTaps);
  float opticalDepth = 0.0f;

  for (int i = 0; i < kTaps; ++i) {
    float t = t0 + dt * (float(i) + 0.5f);
    vec3 sp = groundPos + sunDir * t;
    vec2 spXZ = sp.xz;

    float typeNoise     = smoothNoise2D(spXZ * cb.atmosphereArgs.cloudTypeNoiseScale,     200.0f);
    float coverageNoise = smoothNoise2D(spXZ * cb.atmosphereArgs.cloudCoverageNoiseScale, 250.0f);
    float typeLocal     = saturate(cb.atmosphereArgs.cloudTypeMean
                                 + (typeNoise - 0.5f) * cb.atmosphereArgs.cloudTypeSpread);
    float coverageLocal = saturate(cb.atmosphereArgs.cloudCoverageMean
                                 + (coverageNoise - 0.5f) * cb.atmosphereArgs.cloudCoverageSpread);

    float hf = computeCloudHeightFraction(sp, cb.atmosphereArgs);
    opticalDepth += sampleCloudDensityTextured(sp, hf, typeLocal, coverageLocal,
                                                cb.atmosphereArgs,
                                                AtmosphereCloudNoise3D,
                                                AtmosphereCloudNoise3DSampler);
  }

  ShadowMap[threadID] = exp(-opticalDepth * dt * 2.0f * cb.atmosphereArgs.cloudDensity);
}
```

Note: `intersectCloudSlabFromGround` is a new helper in `atmosphere_common.slangh`
(or maybe inlined in the compute shader if it isn't useful elsewhere). It does
the same sphere-intersection math as `intersectCloudLayer` but starts the ray
from a non-origin ground position.

### Sample-time function — `evalCloudShadowAtWorld`

```glsl
// Replaces evalCloudGroundShadow.
// CLOUD_SHADOW_MAP_AVAILABLE is defined by shaders that bind the SRV.
// (Atmosphere LUT compute shaders that don't bind it get the 1.0 fallback —
// preserves the existing ATMOSPHERE_AVAILABLE early-return contract.)
float evalCloudShadowAtWorld(vec3 worldPos, AtmosphereArgs args) {
#ifndef CLOUD_SHADOW_MAP_AVAILABLE
  return 1.0f;
#else
  if (args.cloudEnabled < 0.5f || args.cloudShadowStrength < 0.01f) return 1.0f;

  vec2 uv = (worldPos.xz - args.cloudShadowMapOriginXZ)
          / (args.cloudShadowMapTexelSize * args.cloudShadowMapResolution);

  // Linear-clamp-to-border-1.0 sampler: off-grid samples = no cloud shadow.
  float shadow = AtmosphereCloudShadowMap.SampleLevel(
                   AtmosphereCloudShadowMapSampler, uv, 0.0f).r;

  return mix(1.0f, shadow, args.cloudShadowStrength);
#endif
}
```

### Updated `getTransmittanceToSun`

```glsl
vec3 getTransmittanceToSun(vec3 worldPos, AtmosphereArgs args, bool isZUp = false) {
  vec3 transmittance = getAtmosphericTransmittanceForDir(args.sunDirection, args);
  transmittance *= evalCloudShadowAtWorld(worldPos, args);
  return transmittance;
}
```

### Camera-anchoring math

Per-frame on CPU:
```cpp
const float kExtent     = 8000.f;
const uint  kResolution = 512u;
const float kTexelSize  = kExtent / float(kResolution);  // 15.625m

vec2 cameraXZ    = {cameraWorldPos.x, cameraWorldPos.z};
vec2 snappedXZ   = floor(cameraXZ / kTexelSize) * kTexelSize;
vec2 mapOriginXZ = snappedXZ - 0.5f * kExtent;

m_anchor.mapOriginXZ = mapOriginXZ;
m_anchor.texelSize   = kTexelSize;
m_anchor.resolution  = float(kResolution);
```

Camera scrolling 15.6m crosses a texel boundary; the snapping ensures
that until that happens, every world point maps to the same texel
across consecutive frames (so any per-frame variance is in the cloud
march itself, not in coordinate-space aliasing).

### `vec3(0)` caller updates

Two known sites pass `vec3(0)` to `getTransmittanceToSun`:
- `atmosphere_sky.slangh:74` — sun disk transmittance
- `atmosphere_common.slangh:1055` — `T_sun_to_ground` for atmosphere LUT

Both are updated to pass `args.cameraWorldPos` (verify field name during
implementation; cameraWorldPos field may need to be added to
`AtmosphereArgs` if not already plumbed).

The semantics: sun-disk transmittance and atmospheric inscatter both
reflect "cloud cover above the camera" — sampling the shadow map at
camera XZ is the right answer.

## Migration

Single commit, no feature flag, clean delete of `evalCloudGroundShadow`. The
function's `ATMOSPHERE_AVAILABLE` fallback semantics (return 1.0 in shaders
without cloud bindings) move to `evalCloudShadowAtWorld`'s
`CLOUD_SHADOW_MAP_AVAILABLE` guard.

## Verification

- **Compilation:** `rtx-build` clean, zero errors.
- **Audit:** `scripts/audit-fork-touchpoints.sh` passes.
- **Visual:** flicker on blue sky between clouds is gone. Cloud
  shadows visibly project as patches on FNV's open terrain.
- **Regression:** `cloudShadowStrength = 0` still returns
  unmodified atmospheric transmittance (no cloud occlusion).

## Out of scope

- Cascaded shadow map (Approach C) — defer until needed.
- Toroidal scrolling for partial-grid update — defer until profiled.
- Colored cloud shadows (RGB, captures forward-scatter through cloud) —
  defer; needs separate physics design.
- Animated cloud-drift sub-texel offset for smoother boundary
  transitions — defer; coarse 15.6m/texel snap is acceptable.

## Open questions

- Does `AtmosphereArgs` already plumb camera world position? If not, add it.
- Does `intersectCloudLayer` accept a non-origin ray start, or do we need
  a small generalization? Implementation pass to verify.
