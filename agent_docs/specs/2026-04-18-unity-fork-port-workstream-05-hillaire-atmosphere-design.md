# Unity-fork port — Workstream 5: Hillaire atmosphere

## Context

This is the second workstream tackled in the unity-fork port (see [Workstream 1 design](./2026-04-17-unity-fork-port-workstream-01-api-skinning-design.md) for overall context). It is being addressed out of numerical order — Workstream 1 (Remix API + HW skinning) has shipped to its branch tip; Workstreams 2–4 (Tonemap, HDR, Texture categorization) are deferred because the Skyrim Remix plugin's `rtx.atmosphere.*` config-key warnings revealed Hillaire as the next blocking gap on the Skyrim iteration path.

Workstream 1 is now functional against the Skyrim Remix plugin: the plugin loads, the scene renders, the vtable layout matches plugin expectations (35 slots / 280 bytes after the `dxvk_GetSharedD3D11TextureHandle` slot was added). Two known issues remain on the Skyrim iteration path:

1. **The dxvk-remix dev-menu overlay does not toggle open** — separate diagnostic + fix track, NOT in this workstream's scope. Diagnostic done during brainstorming for this spec; root-cause hypothesis is HWND routing in separate-window mode.
2. **Physical sky / Hillaire atmosphere is missing** — this workstream addresses it.

## Goal

Port `dxvk-remix-gmod`'s Hillaire atmosphere feature from commit `63eda6b0` ("Hillaire atmosphere (backported from atmos branch)") into the port, on top of Workstream 1's tip, so the Skyrim Remix plugin's physical sky renders and `rtx.atmosphere.*` config-key warnings disappear.

## In scope

- The full Hillaire atmosphere feature: `RtxAtmosphere` C++ class (`rtx_atmosphere.cpp/.h`, ~487 LOC), 6 new compute shaders (`transmittance_lut.comp.slang`, `multiscattering_lut.comp.slang`, `sky_view_lut.comp.slang`, `atmosphere_args.h`, `atmosphere_bindings.slangh`, `atmosphere_common.slangh`), 17 new `rtx.atmosphere.*` RtxOptions, `rtx.skyMode` enum (`SkyboxRasterization` vs `PhysicalAtmosphere`).
- Frame-graph integration in `rtx_context.cpp`: dispatch LUT compute when `skyMode == PhysicalAtmosphere`, bind LUT textures to common-binding slots 200/201/202.
- Shader hook: `evalSkyRadiance()` call in every ray-miss path (geometry resolver primary + PSR, integrator direct + indirect, three gbuffer miss shaders).
- ImGui presets UI in `dxvk_imgui.cpp` (+151 LOC) for tuning `rtx.atmosphere.*` parameters from the dev menu.
- `rtxdi` submodule bump from `87bb0061` to `a95f9403`, only if port's current pin is older.
- Reconciliation with Workstream 1's overlapping changes in three files: `rtx_context.cpp`, `integrator_direct.slangh`, `integrator_indirect.slangh`.

## Out of scope

- **Bug 1 (overlay toggle)** — separate diagnostic/fix track.
- **Fog / volumetric `SetConfigVariable` warnings** (`rtx.enableFog`, `rtx.maxFogDistance`, `rtx.fogColorScale`, `rtx.volumetrics.enableHeterogeneousFog`, `rtx.volumetrics.noiseFieldDensityScale`) — those keys ARE registered in the port already, so the plugin's "key not registered" warnings for them indicate a separate communication-level bug. Likely shape: another vtable-offset mismatch or a config-namespace mismatch. Tracked as its own diagnostic task.
- Other gmod features: tonemap operators (Workstream 2), AgX/Hable, atmosphere-branch unrelated commits.
- Stray local artifact: `build_dxvk_release (2).ps1` — explicitly skipped during port.

## Source material

**Primary commit:** `63eda6b0` in `dxvk-remix-gmod` (path: `c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod`). Description: "Hillaire atmosphere (backported from atmos branch)". Equivalent: `acfc7e59` (the original on `remotes/origin/atmos`); both implement the same feature.

**Surface:** 29 files, +2471 / -30 LOC. Breakdown:
- New C++ (2 files, ~487 LOC): `rtx_atmosphere.cpp` (373), `rtx_atmosphere.h` (114)
- New shaders (6 files, ~1395 LOC): `atmosphere_common.slangh` (802 — math heavy), `multiscattering_lut.comp.slang` (187), `transmittance_lut.comp.slang` (127), `sky_view_lut.comp.slang` (96), `atmosphere_args.h` (59), `atmosphere_bindings.slangh` (34)
- Modified C++ (7 files, ~293 LOC): `dxvk_imgui.cpp` (+151 — ImGui presets), `rtx_context.cpp` (+71), `rtx_options.h` (+32), `rtx_resources.cpp/.h` (+25), `rtx_sky.h` (+6), `meson.build` (+2)
- Modified shaders (8 files, ~285 LOC): `geometry_resolver.slangh` (+56), `integrator_direct.slangh` (+115), `integrator_indirect.slangh` (+129), `gbuffer.slang` (+16 variant flags), 3 miss shaders (+1 each), `common_binding_indices.h` (+10), `common_bindings.slangh` (+10), `raytrace_args.h` (+3)
- Submodule: `rtxdi` pointer bumped one commit (`87bb0061` → `a95f9403`)
- Stray (skip): `build_dxvk_release (2).ps1` (+34 LOC)

## Code-migration approach

**Up to two commits** on top of Workstream 1 tip:

1. **Atomic feature commit** containing all C++, shader, header, RtxOption, ImGui, and `meson.build` changes from gmod commit `63eda6b0`.
2. **Optional separate `rtxdi` submodule-bump commit**, only if the port's current rtxdi pin is older than `a95f9403`. Isolating the bump from the feature commit allows independent revert if the submodule update introduces unrelated regressions.

Method:

1. In `dxvk-remix-gmod`: `git format-patch -1 63eda6b0` produces a patch file.
2. In the port worktree at `.worktrees/unity-workstream-05-hillaire/` (branch `unity-workstream/05-hillaire-atmosphere`, branched off `unity-workstream/01-api-skinning` tip): `git apply --3way <patch>` — but exclude the stray build script and the rtxdi submodule pointer change from the apply (use `git apply --exclude=...` or hand-edit the patch).
3. Resolve any 3-way merge conflicts inline (expected only in the three overlapping files — see Reconciliation below).
4. `git commit` the feature changes as a single atomic commit. Commit message references gmod commit `63eda6b0` (and equivalent `acfc7e59`) and lists any deviations.
5. If rtxdi bump is needed: `git -C submodules/rtxdi checkout a95f9403`, then `git add submodules/rtxdi`, then commit as a second atomic commit with message documenting the bump's source.

Rationale for atomic feature commit (NOT sub-feature decomposition like Workstream 1): Hillaire is a single coherent feature whose parts depend on each other (shaders need the new RtxOptions; RtxOptions need the C++ class; C++ class needs the shaders to bind to). Splitting the feature itself creates intermediate broken states without structural benefit. Workstream 1's split was justified because each sub-feature was a genuinely independent feature. The rtxdi submodule bump is genuinely independent (it's a pointer change, not feature code), so it ships as its own commit if needed.

## Reconciliation strategy

Three files have changes in BOTH Workstream 1 and Hillaire:

| File | Workstream 1 change | Hillaire change | Expected result |
|---|---|---|---|
| `src/dxvk/rtx_render/rtx_context.cpp` | Sub-feature 4 added `dispatchScreenOverlay` insertion in composite chain (~lines 698–710) | `RtxAtmosphere::updateRaytraceArgs()` call + LUT compute dispatch + sky binding setup | Different regions, clean 3-way merge expected |
| `src/dxvk/shaders/rtx/algorithm/integrator_direct.slangh` | Sub-feature 5 added view-model `customIndex` filter | `evalSkyRadiance()` call in ray-miss path | Different regions, clean |
| `src/dxvk/shaders/rtx/algorithm/integrator_indirect.slangh` | Same as ↑ | Same shape as ↑ | Clean |

If `git apply --3way` produces conflict markers, the implementer:
1. Reads both unity AND port versions of the affected file end-to-end.
2. Preserves BOTH change sets (Workstream 1's and Hillaire's).
3. If a textual conflict requires semantic resolution (not just textual concatenation), flags the conflict for review before committing.

## Special handling

- **rtxdi submodule bump (`87bb0061` → `a95f9403`)**: implementer first checks the port's current `rtxdi` submodule pin via `git submodule status submodules/rtxdi`. If at-or-newer than `a95f9403`, no bump needed — skip. If older, bump to `a95f9403` to match gmod. Document the chosen action in the commit message.
- **ImGui presets UI** (+151 LOC in `dxvk_imgui.cpp`): include verbatim. Provides UI controls for tuning `rtx.atmosphere.*` parameters from the dev menu. Reconcile any helper-function calls against the port's existing ImGui surface; stub or adapt missing helpers.
- **Stray file `build_dxvk_release (2).ps1`**: explicitly skip. Local artifact, not feature code.
- **`meson.build`** (+2 lines): include — registers the three new compute shaders for compilation.
- **`common_binding_indices.h`** (Hillaire uses slots 200, 201, 202 for transmittance/multiscattering/sky-view LUTs): verify these slots are unused in the port before applying. If a collision exists, escalate before proceeding.

## Validation

### Iteration-time (during implementation)

- `meson compile -C build-release` clean — no new warnings.
- New compute shaders (`transmittance_lut`, `multiscattering_lut`, `sky_view_lut`) compile.
- `static_assert(sizeof(remixapi_Interface) == 280)` from Workstream 1 still holds — this workstream does not touch the API surface.
- `git grep "[RTX-Diag]"` returns zero matches (Workstream 1's diagnostic commits are already reverted; this workstream adds no new diagnostic instrumentation).

### Runtime

- Deploy built `d3d9.dll` to `D:/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll`.
- Launch Skyrim, load a save, look outdoors at the sky (Solitude or any open exterior cell).
- **Visual confirm:** physical sky scattering visible — sunset gradient at sun elevation matching the plugin's configured `rtx.atmosphere.sunElevation = 77.9°`. Distance fog uses Hillaire scattering instead of skybox sample.
- **Plugin log check** (`C:/Users/mystery/Documents/My Games/Skyrim Special Edition/SKSE/SkyrimRemixPlugin.log`): `rtx.atmosphere.*` `SetConfigVariable failed ... key not registered` warnings should be ABSENT. The fog/volumetric warnings will remain — those are out of scope, separate bug.
- **dxvk log check** (`D:/SteamLibrary/steamapps/common/Skyrim Special Edition/rtx-remix/logs/remix-dxvk.log`): `RtxAtmosphere` LUT compute passes dispatch when atmosphere parameters change.

### Merge gate (before workstream branch merges into integration branch)

- Skyrim Remix plugin loads, renders a scene with physical sky enabled, no crashes.
- Workstream 1's existing fixes still hold (vtable layout 280 bytes, `LockDevice` in `SetupCamera`/`DrawInstance`, plugin Present path reaches `injectRTX`).
- No regression in scenes that don't use `rtx.skyMode == PhysicalAtmosphere` (skybox rasterization fallback still works).

## Git layout

- Port repo: `c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix`.
- Spec branch: `unity-port-planning` (this document and Workstream 1 spec live here).
- Implementation branch: `unity-workstream/05-hillaire-atmosphere`, branched off `unity-workstream/01-api-skinning` tip (currently `831afd42`).
- Worktree: `c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-05-hillaire/`.
- When validation passes, the workstream branch will merge into the unity integration branch alongside Workstream 1.

## Known risks

1. **Shader 3-way merge has semantic (not textual) conflicts.** The shader files use complex include chains and macro expansions; a clean textual 3-way merge may still produce semantically incorrect code. Mitigation: implementer reads both unity AND port versions of each overlapping shader end-to-end before committing the merge resolution.

2. **rtxdi submodule bump breaks unrelated RTX-DI behavior.** Bumping the submodule to a newer commit may bring in unrelated rtxdi changes that affect existing direct illumination. Mitigation: bump only if the port's current pin is older than `a95f9403`. If bumped, isolate the bump to its own commit (within the same workstream) so it can be rolled back independently.

3. **`atmosphere_common.slangh` (802 LOC) depends on gmod-only shader headers.** The math header may include gmod-specific helpers. Mitigation: implementer reads the file end-to-end before applying; cross-references include directives against the port's shader header inventory.

4. **Binding slot collision (200/201/202).** Hillaire allocates these slots for LUT bindings. If the port has used them for something else upstream, the collision will manifest as silent rendering corruption. Mitigation: verify slots are unused via `git grep` against the port's `common_binding_indices.h` and shader binding declarations before applying.

5. **ImGui presets section depends on gmod-only ImGui helpers.** The 151 LOC of presets UI may call helper functions that exist in gmod's `dxvk_imgui.cpp` but not the port's. Mitigation: compare the port's ImGui helper surface vs gmod's; stub or adapt missing pieces.

6. **New `rtx.atmosphere.*` config keys collide with existing port keys.** The scoping diagnostic confirmed no collision in current state, but this should be re-verified during implementation.

7. **Plugin's fog/volumetric warnings are NOT fixed by this workstream.** Those keys (`rtx.enableFog`, `rtx.maxFogDistance`, `rtx.fogColorScale`, `rtx.volumetrics.*`) ARE registered in the port already — the plugin's "key not registered" warnings for them indicate a separate communication-level bug, possibly another vtable-shape mismatch. After this workstream, the user will still see those warnings; they need their own diagnostic task.

## What comes next

When Workstream 5 is validated and merged into the unity integration branch, the next priorities (in suggested order):

1. **Bug 1 (overlay toggle)** — diagnostic + fix for the dxvk-remix dev menu not opening. Likely an HWND-routing issue in separate-window mode (per the diagnostic done during this brainstorming session — alt hypothesis A in that report).
2. **Fog/volumetric warning bug** — diagnostic for why the plugin reports already-registered fog keys as missing.
3. **Workstream 2 (Tonemap)** — return to original numerical order for remaining workstreams.
