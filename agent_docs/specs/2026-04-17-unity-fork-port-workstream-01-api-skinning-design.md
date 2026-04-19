# Unity-fork port — Workstream 1: Remix API + HW skinning

## Context

The `dxvk-remix-gmod` fork (branch `unity`) has 63 commits ahead of its merge base with NVIDIA's upstream `dxvk-remix`, touching 117 files (~11,331 insertions, ~600 deletions). Upstream has moved 298 commits ahead of that merge base (`a8192ecd`). This spec covers the first of six workstreams porting the fork's changes onto current upstream `main`.

The fork's consumers are Skyrim, Fallout 4, Minecraft, and UnityRTX mods. They are built against the fork's current `public/include/remix/remix_c.h` and depend on its binary shape. Any port must preserve that shape.

## Goal

Bring the fork's Remix API extensions and HW skinning fixes onto current upstream `dxvk-remix/main`, preserving binary compatibility with existing Skyrim / F4 / Minecraft / UnityRTX plugins.

## Overall port decomposition (all six workstreams)

1. **Workstream 1 (this spec): Remix API + HW skinning** — load-bearing for Unity/mod integration.
2. Workstream 2: Tonemap (AGX, Hable Filmic, Direct mode, booleans→enum refactor).
3. Workstream 3: HDR.
4. Workstream 4: Texture categorization (legacy emissive, alpha-as-mask, baked lighting rules).
5. Workstream 5: Hillaire atmosphere.
6. Workstream 6: Misc stability + custom splash remnant.

**Dropped from scope after upstream-overlap audit:**

- **Splash + UI theme** — absorbed by upstream commits `dc99dac3` and `37b06bf2` (major ImGui theme overhaul, Jul–Oct 2025). Only `f3695cc8` (custom splash + imgui state API calls) remains, folded into Workstream 6.
- **XeSS 2.1 SR** — landed upstream via PR 100 (commit `4cd488c5`).

## Workstream 1 — sub-feature decomposition

Each sub-feature is ported as one squash-and-reapply commit on the workstream branch. Order is intended; earlier sub-features provide infrastructure later ones use.

### Sub-feature 1: Texture API

Fork commits:

- `fa4fad87` — `CreateTexture` / `DestroyTexture`, `remixapi_TextureInfo`, `remixapi_Format` enum, `remixapi_TextureHandle` type, occluder plumbing
- `6d2b82ca` — `AddTextureHash` / `RemoveTextureHash` methods
- `1843558f` — `dxvk_GetTextureHash` method
- `951f4fcb` — additional texture hash handling (`0x`-prefix parsing)
- `1a67dadb` — auto-apply texture categories for API-provided materials

### Sub-feature 2: Lights API

Fork commits:

- `be4ccfc3` — API light handling baseline
- `65d8849a` — fix broken API light tracking
- `d7dc9361` — reconcile API-created lights with mod content
- `f1a86eff` — sleep light if not active
- `602c6203` — `ignoreViewModel` flag on `remixapi_LightInfo` (size goes from 40 → 48 bytes at fork)
- `2a7f0187` — light batching / deferred update work (`CreateLightBatched`, `UpdateLightDefinition`)

### Sub-feature 3: Meshes API

Fork commits:

- `e62739fe` — `CreateMeshBatched`
- `b3b0e284` — include meshes and lights in replacement lookup
- `3bce7e55` — object-picking metadata logic (see Known Risks below — upstream has its own picking API with different shape)

### Sub-feature 4: UI/Overlay API

Fork commits:

- `23fc439c` — ImGui Remix API wrapper
- `09ea9daf` — screen overlay API (split from the unrelated build-script changes in the same commit)
- `f3695cc8` — imgui state API calls (this is the splash-category survivor, folded in here)

### Sub-feature 5: HW skinning + capture/replacement

Fork commits:

- `4ce38d1b` — core HW skinning fix for Unity (bone-matrix hashing)
- `f567ad8b` — coordinate-system handling for API content
- `0f4e6abb` — capture/replacement material-lookup fix (`materialLookupHash`)
- `eb3c337b` — indirect-pass VIEW_MODEL flag propagation (depends on `ignoreViewModel` from Sub-feature 2)

### Rolling up

- `cdb93e0b` — API header version bump. Applied as part of Sub-feature 1 (the first sub-feature to touch the header).

## ABI contract

The fork's `public/include/remix/remix_c.h` on branch `unity` at tip `09ea9daf` is the ABI of record for this port. The following are invariant:

- `remixapi_LightInfo` remains 48 bytes, with `ignoreViewModel` at its current offset.
- All fork-added methods retain their signatures: `AddTextureHash`, `RemoveTextureHash`, `dxvk_GetTextureHash`, `CreateTexture`, `DestroyTexture`, `CreateMeshBatched`, `CreateLightBatched`, `UpdateLightDefinition`, screen overlay methods.
- New enums (`remixapi_Format`) and new types (`remixapi_TextureHandle`) retain their values and definitions.

Non-conflicting upstream additions since 0.6.1 (e.g., `REMIX_WINAPI_NO_INCLUDE` conditional header handling from upstream's `remixapi-win-header` branch) may be taken.

No existing field order, method vtable position, struct size, or enum value present in the `unity` branch header may be altered by this port.

## Code-migration approach

Squash-and-reapply per sub-feature. For each sub-feature:

1. Produce the combined diff of all its fork commits (`git diff` against their parent commits).
2. Read the upstream file state for every touched file. Identify where upstream has moved code, renamed types, or reshaped surrounding structures.
3. Apply the fork's changes *semantically* — not line-by-line — onto upstream. Conflict resolution happens once per sub-feature, informed by understanding of the fork's intent.
4. Land the sub-feature as a single clean commit on the workstream branch. Commit message lists the fork commit hashes it condenses and a one-line description.

This keeps the port at roughly five merge-conflict resolutions instead of 25 and produces bisectable commits on the workstream branch.

## Validation

### Iteration-time (per sub-feature, during implementation)

- Build succeeds (release and debug configs).
- The Skyrim Remix mod loads against the built DLL, renders a scene, exercises the sub-feature's APIs without crash.
- The build's auto-deploy hop continues to drop the DLL into the correct plugins directory for Skyrim iteration.

### Merge gate (before the workstream branch merges into `unity`)

All four consumers launch and render correctly:

- Skyrim Remix mod (primary iteration target — most mature)
- Fallout 4 Remix mod
- Minecraft Remix mod
- UnityRTX

"Render correctly" here means:

- No crashes in code paths that exercise API-provided content (textures, lights, meshes, replacements).
- Replacements appear in the expected geometry.
- Skinned meshes deform correctly (no jitter, no wrong-pose artifacts).
- Visual parity with the fork's current behavior on the same save/scene.

## Git layout

- New dxvk-remix repo integration branch: `unity`, tracking upstream `main`.
- Workstream 1 branch: `unity-workstream/01-api-skinning`, branched off upstream `main` at the HEAD current when implementation begins.
- When validation passes, workstream branch merges into `unity` via merge commit (preserves sub-feature decomposition in history).
- Subsequent workstreams (`unity-workstream/02-tonemap`, etc.) branch from `main` independently and merge into `unity` the same way.

## Known risks and follow-ups

1. **API audit inconsistencies to re-verify.** The parallel audit produced contradictory LANDED classifications for three commits: `23fc439c` (imgui Remix API wrapper), `1843558f` (`dxvk_GetTextureHash`), and `fa4fad87` (texture upload API). Each must be verified against upstream's current `remix_c.h` during its sub-feature's implementation — a fresh `diff` of the upstream header against the fork's header will expose any missed upstream additions.

2. **Object-picking divergence.** Upstream has its own object-picking API with a different shape than the fork's metadata-based approach. During Sub-feature 3 (Meshes API) we must decide whether to (a) adapt the fork's picking metadata to upstream's shape, or (b) preserve the fork's shape alongside upstream's and document the divergence. Decision deferred to implementation.

3. **Light-batching temporal-state preservation.** `CreateLightBatched` / `UpdateLightDefinition` preserve per-light state across frames for animation purposes. Upstream's light manager uses an immediate-update model. Verify during Sub-feature 2 that the fork's frame-to-frame preservation doesn't collide with upstream internals.

4. **Auto-deploy destination mismatch.** The auto-deploy build step currently targets the F4SE plugins directory, but Skyrim is the primary iteration target for this workstream. If Skyrim Remix uses a different plugin directory, the build script needs updating before iteration begins.

## What comes next

When Workstream 1 is validated and merged into `unity`, brainstorm Workstream 2 (Tonemap).
