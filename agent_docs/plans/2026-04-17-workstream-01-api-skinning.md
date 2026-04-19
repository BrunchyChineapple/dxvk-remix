# Unity Fork Port — Workstream 1: Remix API + HW Skinning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the Remix API extensions and HW skinning fixes from `dxvk-remix-gmod` branch `unity` (tip `09ea9daf`) onto current upstream `dxvk-remix/main`, producing a workstream branch ready to merge into the `unity` integration branch. Binary compatibility with existing Skyrim / F4 / Minecraft / UnityRTX consumer plugins must be preserved.

**Architecture:** Squash-and-reapply per sub-feature on branch `unity-workstream/01-api-skinning`, branched off current upstream `main` HEAD. Five sub-features (Texture API, Lights API, Meshes API, UI/Overlay API, HW Skinning). Each sub-feature is applied **semantically** onto upstream rather than cherry-picked, because upstream has drifted 298 commits from the fork's merge base. The fork's `public/include/remix/remix_c.h` at `unity` tip is the ABI contract — no existing field, signature, struct size, or enum value may change. Each sub-feature lands as one commit, making the workstream branch bisectable.

**Tech Stack:** C++20, Vulkan, HLSL/Slang shaders, Meson+Ninja build system, git. Validation via Skyrim Remix mod (primary iteration target), plus Fallout 4, Minecraft, and UnityRTX Remix mods at the merge gate.

---

## File Map — upstream files that will be touched per sub-feature

These are the expected touch points. Exact file positions may have drifted upstream; Task 1 step 2 verifies the actual current state.

**Sub-feature 1 — Texture API** (commits: `fa4fad87`, `6d2b82ca`, `1843558f`, `951f4fcb`, `1a67dadb`, `cdb93e0b`)

- `public/include/remix/remix_c.h` — API header additions: `CreateTexture`/`DestroyTexture`, `AddTextureHash`/`RemoveTextureHash`, `dxvk_GetTextureHash`, `remixapi_TextureInfo`, `remixapi_Format`, `remixapi_TextureHandle`, version bump
- `public/include/remix/remix.h` — C++ wrapper for the above
- `src/dxvk/rtx_render/rtx_remix_api.cpp` / `.h` — server-side API implementation (file name may be different upstream)
- `src/dxvk/rtx_render/rtx_texture_manager.cpp` / `.h` — texture registration, hash management, category auto-apply

**Sub-feature 2 — Lights API** (commits: `be4ccfc3`, `65d8849a`, `d7dc9361`, `f1a86eff`, `602c6203`, `2a7f0187`)

- `public/include/remix/remix_c.h` — `remixapi_LightInfo.ignoreViewModel` field, `CreateLightBatched`, `UpdateLightDefinition`
- `src/dxvk/rtx_render/rtx_remix_api.cpp` / `.h` — light API endpoints, batching, temporal-state preservation
- `src/dxvk/rtx_render/rtx_light_manager.cpp` / `.h` — light tracking fix, sleep-when-inactive, viewmodel filter hookup

**Sub-feature 3 — Meshes API** (commits: `e62739fe`, `b3b0e284`, `3bce7e55`)

- `public/include/remix/remix_c.h` — `CreateMeshBatched`, object-picking metadata
- `src/dxvk/rtx_render/rtx_remix_api.cpp` / `.h` — mesh batching, replacement inclusion, picking metadata
- Replacement lookup path (likely `rtx_asset_replacer.cpp` or equivalent) — mesh-inclusion hook

**Sub-feature 4 — UI/Overlay API** (commits: `23fc439c`, `09ea9daf`, and imgui-state calls from `f3695cc8`)

- `public/include/remix/remix_c.h` — ImGui wrapper methods, screen overlay API
- `src/dxvk/imgui/dxvk_imgui_remix_wrapper.cpp` / `.h` (new) — ImGui wrapper implementation
- `src/dxvk/rtx_render/rtx_screen_overlay.cpp` / `.h` (new) — screen overlay compute shader + API
- `src/dxvk/shaders/rtx/pass/screen_overlay/` (new) — overlay compute shader

**Sub-feature 5 — HW Skinning + Capture/Replacement** (commits: `4ce38d1b`, `f567ad8b`, `0f4e6abb`, `eb3c337b`)

- `src/dxvk/rtx_render/rtx_geometry_utils.cpp` / `.h` or equivalent — bone-matrix hashing fix
- `src/dxvk/rtx_render/rtx_scene_manager.cpp` — coord-system handling for API content
- Capture path — `materialLookupHash` fix
- Indirect pass shader bindings — `VIEW_MODEL` flag propagation (consumes `ignoreViewModel` from Sub-feature 2)

---

## Task 0: Workstream branch setup

**Files:**
- Modify: build scripts for auto-deploy target
- Create: `unity-workstream/01-api-skinning` branch

- [ ] **Step 0.1: Confirm clean upstream state**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix
git status --short
git branch --show-current
```

Expected: no output from `git status --short` on `main` (or `unity-port-planning` — either is acceptable since we're branching off `main`). Note the current HEAD commit for the spec's "starting point" reference.

- [ ] **Step 0.2: Create workstream branch off upstream main HEAD**

```bash
git fetch origin main
git switch -c unity-workstream/01-api-skinning origin/main
git log -1 --oneline
```

Expected: new branch created, HEAD shows the latest upstream commit (`17d74001` or newer if upstream moved).

- [ ] **Step 0.3: Initialize submodules**

```bash
git submodule update --init --recursive
```

Expected: all submodules initialized including `submodules/xess` (new since fork base). If any fail, resolve before proceeding — the build depends on them.

- [ ] **Step 0.4: Baseline debug build**

```bash
meson setup --buildtype=debug build-debug
meson compile -C build-debug
```

Expected: clean build. Record the build time for later comparison. If the build fails on pristine upstream, stop — something is wrong with the environment, not the port.

- [ ] **Step 0.5: Baseline release build**

```bash
meson setup --buildtype=release build-release
meson compile -C build-release
```

Expected: clean build. The release DLL is what gets deployed to game plugin directories.

- [ ] **Step 0.6: Configure Skyrim auto-deploy target**

Locate the build-deploy script in the repo (search for `*.ps1` or `deploy` scripts). Verify it copies `build-release/d3d9.dll` (or the appropriate DXVK output) to the Skyrim Remix mod's plugins directory. If the script currently targets F4's F4SE plugins directory, duplicate it for Skyrim or parameterize the target.

Confirm path by checking where the user's Skyrim Remix mod expects DXVK. If uncertain, stop and ask the user.

- [ ] **Step 0.7: Smoke-test the baseline Skyrim deploy**

Run the deploy script. Launch Skyrim. Verify the Remix mod loads and renders normally with pristine upstream DXVK. This proves the baseline works before any port changes land.

Expected: Skyrim launches, Remix mod loads, scene renders. Record any baseline issues — they are not ours to fix in this workstream.

- [ ] **Step 0.8: Create workstream tracking directory**

```bash
mkdir -p docs/superpowers/plans/artifacts/workstream-01
```

This directory holds extracted diffs and re-verification notes produced in Task 1.

- [ ] **Step 0.9: Commit workstream setup marker**

```bash
git add docs/superpowers/plans/artifacts/workstream-01/.gitkeep 2>/dev/null || echo "(no files to add yet)"
git commit --allow-empty -m "Begin workstream 01: Remix API + HW skinning port

Branched from upstream main at $(git rev-parse --short HEAD^)."
```

Expected: one empty commit marking the workstream start.

---

## Task 1: Extract fork diffs and re-verify audit

**Files:**
- Create: `docs/superpowers/plans/artifacts/workstream-01/*.diff` (per sub-feature)
- Create: `docs/superpowers/plans/artifacts/workstream-01/audit-reverification.md`

- [ ] **Step 1.1: Add the fork as a git remote**

```bash
git remote add fork https://github.com/sambow23/dxvk-remix-gmod.git
git fetch fork unity
git log -1 fork/unity --oneline
```

Expected: fork's `unity` branch tip is `09ea9daf add screen overlay remix API and clean release build script`.

- [ ] **Step 1.2: Record the fork/upstream merge base**

```bash
FORK_BASE=$(git merge-base fork/unity origin/main)
echo "Fork merge base: $FORK_BASE"
```

Expected: `a8192ecd` (or full hash). Save this — every fork commit lives between this and `fork/unity`.

- [ ] **Step 1.3: Extract Sub-feature 1 (Texture API) combined diff**

```bash
git diff fa4fad87^..fa4fad87 6d2b82ca^..6d2b82ca 1843558f^..1843558f 951f4fcb^..951f4fcb 1a67dadb^..1a67dadb cdb93e0b^..cdb93e0b \
  > docs/superpowers/plans/artifacts/workstream-01/sub1-textures.diff 2>&1 || true

git show fa4fad87 6d2b82ca 1843558f 951f4fcb 1a67dadb cdb93e0b \
  > docs/superpowers/plans/artifacts/workstream-01/sub1-textures.show
```

The `.show` file captures commit messages and author context; the `.diff` is easier to apply semantically. Having both is cheap and useful.

- [ ] **Step 1.4: Extract Sub-feature 2 (Lights API) combined diff**

```bash
git show be4ccfc3 65d8849a d7dc9361 f1a86eff 602c6203 2a7f0187 \
  > docs/superpowers/plans/artifacts/workstream-01/sub2-lights.show
```

- [ ] **Step 1.5: Extract Sub-feature 3 (Meshes API) combined diff**

```bash
git show e62739fe b3b0e284 3bce7e55 \
  > docs/superpowers/plans/artifacts/workstream-01/sub3-meshes.show
```

- [ ] **Step 1.6: Extract Sub-feature 4 (UI/Overlay API) combined diff**

```bash
git show 23fc439c 09ea9daf f3695cc8 \
  > docs/superpowers/plans/artifacts/workstream-01/sub4-ui-overlay.show
```

Note: `f3695cc8` contains both UI/overlay work and splash/cursor unrelated changes. When applying, only take the imgui-state-API portions; the splash portions belong to Workstream 6.

- [ ] **Step 1.7: Extract Sub-feature 5 (HW Skinning) combined diff**

```bash
git show 4ce38d1b f567ad8b 0f4e6abb eb3c337b \
  > docs/superpowers/plans/artifacts/workstream-01/sub5-skinning.show
```

- [ ] **Step 1.8: Re-verify audit inconsistency — commit 23fc439c (imgui remix api wrapper)**

The upstream-overlap audit marked this LANDED but cited `11bb1270 Implement DXVK backend for ImGui` — which is an unrelated DXVK-layer ImGui backend, not a Remix API wrapper.

```bash
git -C . log origin/main --oneline | grep -i "remix.*imgui\|imgui.*remix\|remix api.*wrapper" || echo "no match"
git -C . show 23fc439c --stat
```

Expected: no upstream commit adds a Remix API ImGui wrapper. Classification should be NOT-LANDED. Record finding.

- [ ] **Step 1.9: Re-verify audit inconsistency — commit 1843558f (dxvk_GetTextureHash)**

The audit marked this LANDED but the audit's own surface-delta said upstream does NOT include `dxvk_GetTextureHash`.

```bash
git -C . show origin/main:public/include/remix/remix_c.h | grep -i "GetTextureHash\|TextureHash" || echo "no match"
```

Expected: no match. Classification should be NOT-LANDED. Record finding.

- [ ] **Step 1.10: Re-verify audit inconsistency — commit fa4fad87 (texture upload API)**

The audit marked this LANDED but cited evidence that upstream has no texture upload API.

```bash
git -C . show origin/main:public/include/remix/remix_c.h | grep -i "CreateTexture\|DestroyTexture\|TextureInfo\|TextureHandle" || echo "no match"
```

Expected: no match. Classification should be NOT-LANDED. Record finding.

- [ ] **Step 1.11: Write audit re-verification notes**

Create `docs/superpowers/plans/artifacts/workstream-01/audit-reverification.md` summarizing the three re-verifications with their `grep` outputs. This file becomes the record of what was actually NOT-LANDED so the sub-feature tasks don't have to re-litigate it.

- [ ] **Step 1.12: Commit the extracted artifacts**

```bash
git add docs/superpowers/plans/artifacts/workstream-01/
git commit -m "Workstream 01: extract fork diffs and re-verify audit

- Extracted per-sub-feature diffs from fork/unity branch
- Re-verified that 23fc439c, 1843558f, fa4fad87 are NOT-LANDED
  (audit report had contradictory classifications)"
```

---

## Task 2: Sub-feature 1 — Texture API

**Files to modify:**
- `public/include/remix/remix_c.h`
- `public/include/remix/remix.h`
- `src/dxvk/rtx_render/rtx_remix_api.cpp` / `.h` (actual filename may differ — use `git grep` to locate)
- `src/dxvk/rtx_render/rtx_texture_manager.cpp` / `.h`

- [ ] **Step 2.1: Read Sub-feature 1 source context**

```bash
cat docs/superpowers/plans/artifacts/workstream-01/sub1-textures.show | head -200
```

Read all six commit messages and diffs. Build a mental model of what each commit contributes. Note which files each touches.

- [ ] **Step 2.2: Map fork files to upstream equivalents**

For each file mentioned in the fork diff, run:

```bash
ls -la <fork-file-path>                                            # confirm it exists at fork
ls -la <same-path-in-new-repo>                                     # confirm/compare upstream
git -C . log --oneline --all -- <file-path> | head -20             # see upstream history
```

If a file was renamed or moved upstream, record the mapping. If a file was deleted upstream, flag for review — the fork's change may need to land in whatever replaced it.

- [ ] **Step 2.3: Apply header additions to `public/include/remix/remix_c.h`**

Open `public/include/remix/remix_c.h`. Apply these additions from the fork (see `sub1-textures.show` for exact content):

- `remixapi_TextureHandle` type
- `remixapi_Format` enum
- `remixapi_TextureInfo` struct
- `remixapi_Interface.CreateTexture` function pointer
- `remixapi_Interface.DestroyTexture` function pointer
- `remixapi_Interface.AddTextureHash` function pointer
- `remixapi_Interface.RemoveTextureHash` function pointer
- `remixapi_Interface.dxvk_GetTextureHash` function pointer
- Version bump to match fork's value

Preserve upstream's additions (e.g., `REMIX_WINAPI_NO_INCLUDE`) where they don't conflict. Interface field order must match the fork exactly — vtable position is part of the ABI contract.

- [ ] **Step 2.4: Apply C++ wrapper additions to `public/include/remix/remix.h`**

Apply the C++ convenience wrapper for each new function pointer. Mirror the fork's signatures exactly.

- [ ] **Step 2.5: Apply server-side API implementation**

Locate the server-side API file (likely `rtx_remix_api.cpp` or `rtx_remix_api_impl.cpp`). Apply the fork's implementations for `CreateTexture`, `DestroyTexture`, `AddTextureHash`, `RemoveTextureHash`, `dxvk_GetTextureHash`. Implementations call into the texture manager.

If upstream has reorganized this file, preserve the new organization but slot the fork's methods in where each logically belongs (constructor for init-time state, dispatch table for function pointers, etc.).

- [ ] **Step 2.6: Apply texture manager changes**

In the texture manager (`rtx_texture_manager.*` or equivalent), apply:

- Texture upload hook (backs `CreateTexture` / `DestroyTexture`)
- Texture hash registry (`AddTextureHash` / `RemoveTextureHash` storage)
- `dxvk_GetTextureHash` lookup path
- `0x`-prefix parsing for hash override (`951f4fcb`)

If upstream's texture manager has been reshaped, apply the fork's additions against the new shape — preserve the fork's API-observable behavior, not its internal structure.

- [ ] **Step 2.7: Apply auto-apply texture categories logic**

From `1a67dadb`: when an API-provided material is registered, automatically apply texture categories based on heuristics (see fork's diff for exact heuristic). Hook this into the texture registration path.

- [ ] **Step 2.8: Apply the API version bump**

From `cdb93e0b`: bump the API minor/patch version macro in `remix_c.h` to match the fork. This signals to consumers that the new methods are available.

- [ ] **Step 2.9: Debug build**

```bash
meson compile -C build-debug
```

Expected: clean compile. If errors, resolve them — the fork's additions should compile against upstream's current surrounding code. Common fixes: include path changes, renamed types, new namespace scoping.

- [ ] **Step 2.10: Release build**

```bash
meson compile -C build-release
```

Expected: clean compile. Both configs must succeed before iteration.

- [ ] **Step 2.11: Deploy and smoke-test in Skyrim**

Run the deploy script. Launch Skyrim with Remix mod. Verify:
- Skyrim launches, Remix mod loads, no startup crash.
- A scene renders at baseline quality.
- If possible, trigger a replacement that exercises the texture-upload path (the user or a collaborator knows which in-game asset does this).
- Check the Remix log for any API-related errors.

- [ ] **Step 2.12: Verify ABI contract**

```bash
# Dump struct sizes via compiler or grep header
grep -A5 "remixapi_LightInfo" public/include/remix/remix_c.h | head -30
# Confirm ignoreViewModel is NOT YET added (that's sub-feature 2). Other struct sizes should match fork.
```

Check that the public header's visible shape after Sub-feature 1 matches what the fork has after its equivalent commits — specifically that no fork-added methods/types are missing.

- [ ] **Step 2.13: Commit Sub-feature 1**

```bash
git add public/include/remix src/dxvk/rtx_render
git commit -m "Sub-feature 1: Texture API (Workstream 01)

Port of fork commits:
  fa4fad87 - unity and occluder stuff (CreateTexture/DestroyTexture, Format enum, TextureHandle)
  6d2b82ca - AddTextureHash/RemoveTextureHash
  1843558f - dxvk_GetTextureHash
  951f4fcb - tex hash 0x-prefix parsing
  1a67dadb - auto-apply texture categories
  cdb93e0b - API version bump

Squash-and-reapply; upstream has drifted 298 commits from fork base so
changes are applied semantically, not as cherry-picks.

ABI contract: fork's remix_c.h at unity tip 09ea9daf is preserved."
```

---

## Task 3: Sub-feature 2 — Lights API

**Files to modify:**
- `public/include/remix/remix_c.h`
- `public/include/remix/remix.h`
- `src/dxvk/rtx_render/rtx_remix_api.*`
- `src/dxvk/rtx_render/rtx_light_manager.*` (or equivalent)

- [ ] **Step 3.1: Read Sub-feature 2 source context**

```bash
cat docs/superpowers/plans/artifacts/workstream-01/sub2-lights.show | head -400
```

Commits: `be4ccfc3`, `65d8849a`, `d7dc9361`, `f1a86eff`, `602c6203`, `2a7f0187`. Build a mental model of each.

- [ ] **Step 3.2: Verify upstream light-manager has not rewritten the signatures you'll touch**

```bash
git -C . log origin/main --oneline -- src/dxvk/rtx_render/rtx_light_manager.* | head -20
```

If upstream has rewritten the light manager since the fork base, the fork's patches may need structural adaptation. Record any concerning upstream commits.

- [ ] **Step 3.3: Apply `ignoreViewModel` field to `remixapi_LightInfo`**

From `602c6203`: add `remixapi_Bool ignoreViewModel;` to the `remixapi_LightInfo` struct at its fork-correct offset. **Struct size must become 48 bytes** (from 40). Consumers expect this.

- [ ] **Step 3.4: Apply `CreateLightBatched` + `UpdateLightDefinition` API**

From `2a7f0187`: add the batched/deferred-update light API function pointers to the interface, their C++ wrappers, and the server-side implementation. This is the fork's temporal-state preservation mechanism for lights animated across frames.

- [ ] **Step 3.5: Apply light tracking fix**

From `65d8849a`: fix broken API light tracking. The fix is typically per-frame bookkeeping on how lights created via the API flow through the light manager's persistent state.

- [ ] **Step 3.6: Apply "sleep light if not active"**

From `f1a86eff`: inactive API lights should be suspended (not rendered, not updated) rather than kept in the active set. Apply the suspension logic.

- [ ] **Step 3.7: Apply viewmodel filter hookup**

Connect the new `ignoreViewModel` field to the light evaluation path — lights with `ignoreViewModel=true` must be filtered out during viewmodel rendering passes. Sub-feature 5 will hook this into the indirect pass shaders; this step is just the CPU-side filter plumbing.

- [ ] **Step 3.8: Risk check — light batching vs upstream light-manager internals**

Sit with the spec's known risk #3: upstream uses an immediate-update model for lights. The fork's batched update preserves state across frames. Verify the fork's batched path does not collide with upstream's immediate path — they should be independent code paths keyed by how the light was created.

If a collision exists, resolve by keeping API-created lights isolated in their own bucket that's evaluated after the immediate-update light set.

- [ ] **Step 3.9: Debug build**

```bash
meson compile -C build-debug
```

- [ ] **Step 3.10: Release build**

```bash
meson compile -C build-release
```

- [ ] **Step 3.11: Deploy and smoke-test in Skyrim**

Launch Skyrim. Exercise a scene that uses API-provided lights (replacement lighting in Remix mods typically hits this). Verify:
- No crash when lights are added/removed
- `ignoreViewModel` behaves correctly if exercised (weapon/first-person mesh lighting)
- Light temporal behavior is smooth (no flicker, no dropped frames due to state churn)

- [ ] **Step 3.12: Verify ABI contract for Lights**

```bash
# Confirm struct size via a small compile-time check snippet, or grep offset macros
grep -B1 -A10 "remixapi_LightInfo" public/include/remix/remix_c.h | head -40
```

`remixapi_LightInfo` must now include `ignoreViewModel`. Total struct size must match fork's 48-byte expectation.

- [ ] **Step 3.13: Commit Sub-feature 2**

```bash
git add public/include/remix src/dxvk/rtx_render
git commit -m "Sub-feature 2: Lights API (Workstream 01)

Port of fork commits:
  be4ccfc3 - gmod api light baseline
  65d8849a - fix broken api light tracking
  d7dc9361 - reconcile API lights with mod content
  f1a86eff - sleep light if not active
  602c6203 - ignoreViewModel flag (struct size 40->48 bytes)
  2a7f0187 - light batching + UpdateLightDefinition

ABI: remixapi_LightInfo grows to 48 bytes; fork consumers are built
against the 48-byte layout. Upstream's 40-byte layout is intentionally
broken at this point — restoring it would break fork consumers."
```

---

## Task 4: Sub-feature 3 — Meshes API

**Files to modify:**
- `public/include/remix/remix_c.h`
- `public/include/remix/remix.h`
- `src/dxvk/rtx_render/rtx_remix_api.*`
- Mesh/replacement lookup (likely `rtx_asset_replacer.*` or `rtx_scene_manager.cpp`)

- [ ] **Step 4.1: Read Sub-feature 3 source context**

```bash
cat docs/superpowers/plans/artifacts/workstream-01/sub3-meshes.show
```

Commits: `e62739fe`, `b3b0e284`, `3bce7e55`.

- [ ] **Step 4.2: Decide object-picking divergence strategy**

Per spec's known risk #2: upstream has its own object-picking API with a different shape than the fork's metadata approach. Before implementing `3bce7e55`, decide:

- **Option A:** Keep upstream's object picking, adapt fork's metadata to flow through upstream's API.
- **Option B:** Preserve fork's picking metadata alongside upstream's API, flagging the divergence.

Recommended default: **Option B** (preserve ABI). The fork's consumers rely on the fork's shape. Diff both approaches, confirm they can coexist without vtable collision, document the choice in the commit message.

If the two approaches cannot coexist (same vtable slot, same struct field offset), escalate to the user.

- [ ] **Step 4.3: Apply `CreateMeshBatched` API**

From `e62739fe`: add the batched mesh creation function pointer, C++ wrapper, and server-side implementation. Mesh batching parallels light batching from Sub-feature 2 — see that implementation for reference if the fork's patterns match.

- [ ] **Step 4.4: Apply mesh inclusion in replacement lookup**

From `b3b0e284`: the replacement lookup path must find API-created meshes in addition to USD-loaded meshes. Hook the API mesh registry into the replacement lookup function.

- [ ] **Step 4.5: Apply object-picking metadata (per Step 4.2 decision)**

From `3bce7e55`: apply the picking metadata path according to the strategy chosen in Step 4.2.

- [ ] **Step 4.6: Debug build**

```bash
meson compile -C build-debug
```

- [ ] **Step 4.7: Release build**

```bash
meson compile -C build-release
```

- [ ] **Step 4.8: Deploy and smoke-test in Skyrim**

Launch Skyrim. Verify API-provided mesh replacements appear correctly. If the user has a specific scene that exercises mesh batching or object picking, use it.

- [ ] **Step 4.9: Commit Sub-feature 3**

```bash
git add public/include/remix src/dxvk/rtx_render
git commit -m "Sub-feature 3: Meshes API (Workstream 01)

Port of fork commits:
  e62739fe - CreateMeshBatched
  b3b0e284 - mesh/light inclusion in replacement lookup
  3bce7e55 - object-picking metadata

Object-picking divergence: chose <A or B> per Step 4.2; see commit body
for rationale. Upstream's object-picking API <is preserved / coexists>
with the fork's metadata approach."
```

Fill in the `<A or B>` placeholders with the actual decision before committing.

---

## Task 5: Sub-feature 4 — UI/Overlay API

**Files to modify:**
- `public/include/remix/remix_c.h`
- `public/include/remix/remix.h`
- `src/dxvk/imgui/` (new file for Remix API ImGui wrapper)
- `src/dxvk/rtx_render/rtx_screen_overlay.*` (new)
- `src/dxvk/shaders/rtx/pass/screen_overlay/` (new shader files)

- [ ] **Step 5.1: Read Sub-feature 4 source context**

```bash
cat docs/superpowers/plans/artifacts/workstream-01/sub4-ui-overlay.show
```

Commits: `23fc439c`, `09ea9daf`, plus the imgui-state-API subset of `f3695cc8`.

- [ ] **Step 5.2: Identify the imgui-state-API-calls subset of `f3695cc8`**

Read `git -C <fork-path> show f3695cc8` in full. The commit contains three groupings:
1. Custom splash changes — **skip, belongs to Workstream 6**
2. Flickering mouse fix — **skip, belongs to Workstream 6**
3. ImGui state API calls — **take these, they belong here**

Mark the specific hunks that are group 3 for inclusion in this sub-feature.

- [ ] **Step 5.3: Apply ImGui Remix API wrapper header additions**

From `23fc439c`: add the ImGui API wrapper function pointers and C++ wrappers to `remix_c.h` / `remix.h`.

- [ ] **Step 5.4: Create ImGui Remix API wrapper implementation**

Create a new file for the wrapper implementation (name per upstream's `src/dxvk/imgui/` conventions). Implement the wrapper methods defined in 5.3.

- [ ] **Step 5.5: Apply ImGui state API calls (from `f3695cc8` group 3)**

Apply only the imgui-state-API hunks identified in Step 5.2.

- [ ] **Step 5.6: Create screen overlay implementation**

From `09ea9daf`: create `rtx_screen_overlay.cpp` / `.h` with the compute shader dispatch and API. Note: this commit is titled "add screen overlay remix API and clean release build script" — **only port the screen overlay; leave the build-script changes out** (they are fork-specific tooling).

- [ ] **Step 5.7: Create screen overlay shader**

Copy the fork's screen overlay compute shader(s) from `src/dxvk/shaders/rtx/pass/screen_overlay/` (or wherever they live in the fork) into the equivalent upstream path. Update `meson.build` entries if the build uses explicit shader listings.

- [ ] **Step 5.8: Wire screen overlay into the render graph**

Find where render passes are registered/dispatched in upstream. Register the screen overlay pass at the equivalent point to the fork's integration (typically post-composition, pre-presentation).

- [ ] **Step 5.9: Debug build**

```bash
meson compile -C build-debug
```

- [ ] **Step 5.10: Release build**

```bash
meson compile -C build-release
```

- [ ] **Step 5.11: Deploy and smoke-test in Skyrim**

Launch Skyrim. Verify:
- Remix ImGui menu still renders (baseline — the wrapper should be transparent to DXVK's own ImGui)
- If possible, trigger a consumer path that exercises the ImGui wrapper
- If the screen overlay is exercisable from Skyrim, verify it renders without corruption

- [ ] **Step 5.12: Commit Sub-feature 4**

```bash
git add public/include/remix src/dxvk/imgui src/dxvk/rtx_render src/dxvk/shaders
git commit -m "Sub-feature 4: UI/Overlay API (Workstream 01)

Port of fork commits:
  23fc439c - ImGui Remix API wrapper
  09ea9daf - screen overlay Remix API (build-script portion skipped)
  f3695cc8 - imgui state API calls (splash/cursor portions skipped,
             belong to workstream 06)"
```

---

## Task 6: Sub-feature 5 — HW Skinning + Capture/Replacement

**Files to modify:**
- `src/dxvk/rtx_render/rtx_geometry_utils.*` or equivalent — bone-matrix hashing
- `src/dxvk/rtx_render/rtx_scene_manager.cpp` — coord-system handling
- Capture path (search for `capture.*material.*hash`) — material lookup fix
- Indirect pass shader bindings — VIEW_MODEL flag propagation

- [ ] **Step 6.1: Read Sub-feature 5 source context**

```bash
cat docs/superpowers/plans/artifacts/workstream-01/sub5-skinning.show
```

Commits: `4ce38d1b`, `f567ad8b`, `0f4e6abb`, `eb3c337b`.

- [ ] **Step 6.2: Identify upstream's skinning implementation**

```bash
git -C . log origin/main --oneline --all -- src/dxvk/rtx_render/*skinning* src/dxvk/shaders/*skinning* | head -20
grep -r "skinning\|bone\|weightIndices" src/dxvk/rtx_render/ | head -20
```

Per audit: upstream added >4-bones-per-vertex support in commit `649a0689` via a different code path. Confirm what shape upstream's skinning path has now.

- [ ] **Step 6.3: Decide skinning-fix application strategy**

Two paths:
- **Path A — upstream-compatible:** If upstream's skinning now handles what the fork was fixing, the fork's `4ce38d1b` may be redundant. Verify by reading both fixes.
- **Path B — fork-style fix:** If upstream's rework doesn't cover the specific "hw skinning for unity" bug (bone-matrix hashing), apply the fork's fix against upstream's current skinning code. Translate any signature differences.

Default to Path B unless upstream's rework clearly obviates the need. Document the decision.

- [ ] **Step 6.4: Apply HW skinning fix**

From `4ce38d1b`: apply the bone-matrix hashing fix per Path A or B chosen in Step 6.3.

- [ ] **Step 6.5: Apply coordinate-system handling**

From `f567ad8b`: apply the coordinate-system fix. This likely hooks into the API-content transform path.

- [ ] **Step 6.6: Apply material lookup hash fix in capture path**

From `0f4e6abb`: in the capture/replacement code, use `materialLookupHash` for the correct material resolution. Find the capture path in upstream (`git grep capture.*material`).

- [ ] **Step 6.7: Apply indirect pass VIEW_MODEL flag propagation**

From `eb3c337b`: in the indirect/light-sampling passes, propagate the VIEW_MODEL flag (populated from `ignoreViewModel` in Sub-feature 2) through `customIndex`. This closes the loop on viewmodel-aware light filtering.

This step depends on `ignoreViewModel` existing, which was added in Sub-feature 2. Verify the field is accessible here.

- [ ] **Step 6.8: Debug build**

```bash
meson compile -C build-debug
```

- [ ] **Step 6.9: Release build**

```bash
meson compile -C build-release
```

- [ ] **Step 6.10: Deploy and smoke-test in Skyrim with skinned content**

Launch Skyrim. Load a scene with skinned meshes (NPCs, creatures — any animated character). Verify:
- No jitter or wrong-pose artifacts on skinned meshes.
- Animation plays smoothly.
- If a Unity-style content path is exercisable, test it.
- Viewmodel lighting behaves correctly if `ignoreViewModel` is exercised.

This is the most visually-obvious validation of the five sub-features.

- [ ] **Step 6.11: Commit Sub-feature 5**

```bash
git add src/dxvk/rtx_render src/dxvk/shaders
git commit -m "Sub-feature 5: HW skinning + capture/replacement (Workstream 01)

Port of fork commits:
  4ce38d1b - HW skinning bone-matrix hashing fix (for Unity/API content)
  f567ad8b - coord-system handling for API content
  0f4e6abb - captures use materialLookupHash
  eb3c337b - indirect-pass VIEW_MODEL flag propagation

Sub-feature 6 closes the ignoreViewModel loop from Sub-feature 2:
lights marked ignoreViewModel are now filtered out during viewmodel
indirect passes via customIndex flag."
```

---

## Task 7: Merge-gate validation — four-game pass

**Files:** none modified in this task.

- [ ] **Step 7.1: Full Skyrim pass**

Launch Skyrim with Remix mod. Run through a scripted smoke-test that exercises:
- Startup + mod load
- Scene render
- API-created replacements (textures, lights, meshes)
- Skinned content (NPCs, creatures)
- Viewmodel (first-person weapons with API lights)

Record any issues.

- [ ] **Step 7.2: Full Fallout 4 pass**

Launch Fallout 4 with Remix mod. Same test matrix as Skyrim. Auto-deploy script may need reconfiguration to target F4SE plugins.

- [ ] **Step 7.3: Full Minecraft pass**

Launch Minecraft with Remix mod. Same test matrix adapted to Minecraft's content (no skinned NPCs but still API lights/textures; voxel content stresses mesh replacement differently).

- [ ] **Step 7.4: Full UnityRTX pass**

Launch UnityRTX. Same test matrix. This is the original consumer; should be the most exhaustive exercise of the API.

- [ ] **Step 7.5: Document findings**

Create `docs/superpowers/plans/artifacts/workstream-01/merge-gate-results.md` with:
- One section per game
- Pass/fail per test
- Screenshots or logs for any failure
- Root-cause analysis for failures (which sub-feature is the likely source?)

- [ ] **Step 7.6: Iterate or proceed**

- **If all four pass:** proceed to Task 8.
- **If any fail:** loop back to the sub-feature identified as root cause, fix, re-run the merge gate from the top. Do not proceed to Task 8 until all four pass.

---

## Task 8: Merge workstream into `unity` integration branch

**Files:** branch-level integration, no code modified directly.

- [ ] **Step 8.1: Ensure `unity` integration branch exists**

```bash
git fetch origin main
git switch unity 2>/dev/null || git switch -c unity origin/main
git log -1 --oneline
```

If `unity` doesn't exist yet, create it from current upstream `main`. Its first commit will be the merge of Workstream 01.

- [ ] **Step 8.2: Merge workstream 01 branch**

```bash
git merge --no-ff unity-workstream/01-api-skinning -m "Merge workstream 01: Remix API + HW skinning

See docs/superpowers/specs/2026-04-17-unity-fork-port-workstream-01-api-skinning-design.md
for the workstream spec. Five sub-features landed:
  1. Texture API
  2. Lights API (adds ignoreViewModel, grows struct to 48 bytes)
  3. Meshes API
  4. UI/Overlay API
  5. HW skinning + capture/replacement

All four consumers (Skyrim, F4, Minecraft, UnityRTX) validated at merge gate."
```

`--no-ff` preserves the decomposition (one merge commit containing five sub-feature commits, visible in `git log --graph`).

- [ ] **Step 8.3: Verify the merged state still compiles**

```bash
meson compile -C build-debug
meson compile -C build-release
```

Expected: clean compile on the integration branch. If a merge conflict was auto-resolved incorrectly, this catches it.

- [ ] **Step 8.4: One last Skyrim smoke test on the merged branch**

Deploy the merged-branch DLL. Launch Skyrim. Verify the mod loads and renders. This is paranoia — we already validated the workstream branch — but the integration branch is where real consumer builds will come from.

- [ ] **Step 8.5: Mark Workstream 01 complete**

```bash
git tag workstream-01-complete
```

Tag preserves the exact state of the integration branch after Workstream 01 lands. Subsequent workstreams can be measured against this tag.

---

## Next: Workstream 02 (Tonemap)

When this plan is complete and Workstream 01 is merged into `unity`, return to the brainstorming skill to design Workstream 02 (Tonemap). The same per-category branch-and-merge pattern repeats.
