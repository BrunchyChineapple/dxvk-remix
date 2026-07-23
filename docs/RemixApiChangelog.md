## Remix API Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1005.0]

### Added
- `remixapi_InstanceInfoRetainedStaticOwnershipEXT`, an instance `pNext` provenance marker for explicitly mapped retained statics that may own matching near-scene submissions.

### Changed
- Retained near-scene claims now require the marker, an independent `remixapi_MeshInfoReplacementEXT` source identity, and materialized renderable geometry. Generated or unmapped statics and Terrain remain fail-open and cannot suppress ordinary geometry.
- The x64 `remixapi_Interface` remains 360 bytes because this release adds no function slot. Consumers must rebuild against the 0.1005.0 header.

### Fixed
- The 32-bit bridge now defines and transports the retained-static ownership extension and forwards `SetRetainedInstanceActivityBatch` atomically instead of exposing a null interface slot.
- Equivalent retained claims with mixed activity now select the lowest active handle for acceleration-structure membership while retaining ordinary-geometry ownership when all claims are inactive.

## [0.1004.0]

### Added
- `remixapi_RetainedInstanceActivity` and the append-only `SetRetainedInstanceActivityBatch` interface slot for changing retained acceleration-structure activity in one validated batch.

### Changed
- Retained instances remain active by default. Callers may omit selected retained geometry from BLAS/TLAS construction without destroying its identity or draw state; pooled acceleration structures may rebuild after reactivation.
- The x64 `remixapi_Interface` size is now 360 bytes. Consumers must rebuild against the 0.1004.0 header.

## [0.1003.0]

### Added
- `remixapi_StartupInfo.combineGuiInFinalColor` and `REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_GUI` from the NVIDIA GUI-output integration.
- `REMIXAPI_INSTANCE_CATEGORY_BIT_HAIR_CARDS` for preserving alpha-tested hair geometry at distance.

### Fixed
- `remixapi_dxvk_CopyRenderingOutput` resource validation and ownership across the NVIDIA merge.
- The `remixapi_HasMeshReplacement` implementation now tests `Rc<DxvkDevice>::ptr()` explicitly, fixing MSVC C2678/C2088 in DebugOptimized builds.

## [0.1002.0]

### Added
- `remixapi_MeshInfoReplacementEXT`, which separates source-draw replacement/capture identity from the independently owned `remixapi_MeshInfo.hash` resource handle.
- Standalone optional `remixapi_HasMeshReplacement`, including bridge forwarding, for querying loaded replacement maps without extending the fixed-size `remixapi_Interface` table.

### Changed
- External mesh capture and replacement lookup use `replacementHash` when the extension is present while preserving the mesh resource hash for ownership.

## [0.1001.0]

### Added
- `remixapi_InstanceHandle` and the append-only `CreateRetainedInstance`, `UpdateRetainedInstance`, and `DestroyRetainedInstance` interface slots.
- Renderer-owned retained-instance replay before generic scene garbage collection and TLAS preparation.

### Fixed
- Retained instances are destroyed before referenced external mesh storage during explicit mesh destruction and shutdown.

## [0.4.2]

### Added
- MaterialInfoOpaqueEXT.displaceOut

### Changed
- renamed MaterialInfoOpaqueEXT.heightTextureStrength to MaterialInfoOpaqueEXT.displaceIn

### Fixed

### Removed


## [0.4.3]

### Added
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceDiffusionProfile
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceRadius
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceRadiusScale
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceMaxSampleRadius
- GameStateStore keys `__weather.drift_speed` and `__weather.drift_intensity` — plugin-controlled cloud-drift speed and intensity multipliers. Both default to 1.0 when unset. Smoothed inside the renderer with tau = 1.0s. See [`docs/integrators/weather-presets.md`](integrators/weather-presets.md) section 8 for the recommended per-preset values and integration pattern.

### Changed

### Fixed

### Removed
- `rtx.atmosphere.sunDisc` (GameStateStore/config key) — removed. The option had no consumer (the sun disc is rendered via the sun-as-distant-light / NEE path); setting it had no effect.


## [0.1000.0]

Remix Plus adopts its own ABI version line (reserved MINOR `1000`), distinct
from stock NVIDIA dxvk-remix `0.6.x`. Because the runtime treats every minor as
breaking while MAJOR is 0, this version makes the runtime reject binaries built
against stock Remix `0.6.x` or against older Remix Plus `0.6.x` — the
`remixapi_Interface` layout and `remixapi_InstanceCategoryBit` ABI differ
between them. Rebuild plugins/hosts against this header.

### Added
- `REMIXAPI_INSTANCE_CATEGORY_BIT_SMOOTH_NORMALS` (bit 24) — the upstream name
  for the category previously exposed (under the fork) only as `LEGACY_EMISSIVE`.
  Use `SMOOTH_NORMALS`; the old alias has been removed (see below).

### Changed
- `remixapi_InstanceCategoryBit` bit values now match upstream NVIDIA exactly.
  An earlier fork build had shifted `IGNORE_ALPHA_CHANNEL` to bit 8 (cascading
  bits 8–20 up by one) to mirror the internal `InstanceCategories` order; this
  is reverted. The C↔internal mapping in `toRtCategories()` is by-name, so the
  public bit values are free to — and now do — match upstream. **Breaking** for
  any consumer that had serialized or hard-coded the shifted bit values.
- `remixapi_Interface.SetCameraMediumMaterial` moved from the middle of the
  struct (between `SetupCamera` and `DrawInstance`) to immediately after
  `Present`, mirroring upstream's canonical layout (upstream `2bac8874`). Fork
  extension functions remain appended after it. **Breaking** struct-offset
  change; rebuild consumers. The `sizeof(remixapi_Interface)` sentinel is
  unchanged (move, not add/remove).
- `REMIXAPI_VERSION` is now `0.1000.0` (was `0.6.4`).

### Fixed
- `remixapi_AutoInstancePersistentLights` no longer emits a per-frame
  `LockDevice` + empty `EmitCs` on the native-D3D9 present path when no external
  (C-API) light has ever been registered and no C-API scene work is queued.
  This empty per-frame dispatch disturbed the light pipeline for native-only
  consumers, manifesting as "persistent lights break all lights / heavy
  flicker." Genuine C-API light consumers are unaffected (the persistent
  re-instancing path is preserved).

### Removed
- `REMIXAPI_INSTANCE_CATEGORY_BIT_LEGACY_EMISSIVE` is **removed**. Its name
  implied emissive behavior, but it routed to bit 24 / `InstanceCategories::SmoothNormals`
  — so any caller using it got a silent wrong-category result. Removing it turns
  that into a compile error; use `REMIXAPI_INSTANCE_CATEGORY_BIT_SMOOTH_NORMALS`.
  Source-only break — the bit layout, enum size, and struct offsets are unchanged
  (bit 24 still exists as `SMOOTH_NORMALS`), so no binary/ABI change and the
  `0.1000.0` version line is unaffected.
