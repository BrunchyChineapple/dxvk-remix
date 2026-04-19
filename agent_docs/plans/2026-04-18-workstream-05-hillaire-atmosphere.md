# Unity Fork Port — Workstream 5: Hillaire Atmosphere Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port `dxvk-remix-gmod` commit `63eda6b0` ("Hillaire atmosphere backported from atmos branch") onto the unity-fork port, on top of Workstream 1's tip, so the Skyrim Remix plugin's physical sky renders and `rtx.atmosphere.*` config-key warnings disappear.

**Architecture:** Single atomic feature commit + optional separate rtxdi submodule-bump commit, on a new worktree branched off `unity-workstream/01-api-skinning` tip. Patch is generated in the gmod repo via `git format-patch`, filtered to exclude the stray build script and the rtxdi pointer, then applied with `git apply --3way` in the port worktree. The three files overlapping with Workstream 1 (`rtx_context.cpp`, `integrator_direct.slangh`, `integrator_indirect.slangh`) get 3-way merged inline with both change sets preserved. Validation is behavioral (build clean + Skyrim physical-sky renders + atmosphere config warnings disappear) — this is a port, not a new feature with unit tests.

**Tech Stack:** C++20, Vulkan, HLSL/Slang shaders, Meson+Ninja build system, git worktrees. Validation via Skyrim Remix mod (primary iteration target).

---

## File Map — files touched by this port

**New files (purely additive, 8 files):**
- `src/dxvk/rtx_render/rtx_atmosphere.cpp` (~373 LOC) — `RtxAtmosphere` class implementation
- `src/dxvk/rtx_render/rtx_atmosphere.h` (~114 LOC) — `RtxAtmosphere` class declaration
- `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh` (~802 LOC) — math header
- `src/dxvk/shaders/rtx/pass/atmosphere/transmittance_lut.comp.slang` (~127 LOC)
- `src/dxvk/shaders/rtx/pass/atmosphere/multiscattering_lut.comp.slang` (~187 LOC)
- `src/dxvk/shaders/rtx/pass/atmosphere/sky_view_lut.comp.slang` (~96 LOC)
- `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h` (~59 LOC)
- `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_bindings.slangh` (~34 LOC)

**Modified files (non-overlapping with W1, 10 files):**
- `src/dxvk/imgui/dxvk_imgui.cpp` (+151 LOC — ImGui atmosphere presets UI)
- `src/dxvk/rtx_render/rtx_options.h` (+32 LOC — `rtx.atmosphere.*` options + `rtx.skyMode` enum)
- `src/dxvk/rtx_render/rtx_resources.cpp` / `.h` (+25 LOC combined — LUT texture registration)
- `src/dxvk/rtx_render/rtx_sky.h` (+6 LOC — sky mode enum)
- `src/dxvk/shaders/rtx/pass/gbuffer/gbuffer.slang` (+16 LOC — `ATMOSPHERE_AVAILABLE` variant flag)
- `src/dxvk/shaders/rtx/pass/gbuffer/geometry_resolver.slangh` (+56 LOC — `evalSkyRadiance` in miss)
- `src/dxvk/shaders/rtx/pass/common_binding_indices.h` (+10 LOC — slots 200/201/202)
- `src/dxvk/shaders/rtx/pass/common_bindings.slangh` (+10 LOC — LUT bindings)
- `src/dxvk/shaders/rtx/pass/raytrace_args.h` (+3 LOC)
- 3 × miss shaders (+1 LOC each — `evalSkyRadiance` call)
- `src/dxvk/meson.build` (+2 LOC — register compute shaders)

**Modified files (OVERLAP with W1 — expect 3-way merge):**
- `src/dxvk/rtx_render/rtx_context.cpp` (+71 LOC — LUT dispatch + `updateRaytraceArgs` hook)
- `src/dxvk/shaders/rtx/algorithm/integrator_direct.slangh` (+115 LOC — `evalSkyRadiance` in miss)
- `src/dxvk/shaders/rtx/algorithm/integrator_indirect.slangh` (+129 LOC — `evalSkyRadiance` in miss)

**Submodule (conditional):**
- `submodules/rtxdi` — pointer bump `87bb0061` → `a95f9403`, ONLY if port's current pin is older

**Excluded from port:**
- `build_dxvk_release (2).ps1` — local artifact, not feature code

---

## Task 0: Workstream branch and worktree setup

**Files:**
- Create: `.worktrees/unity-workstream-05-hillaire/` (worktree)
- Create: `unity-workstream/05-hillaire-atmosphere` (branch, based on `unity-workstream/01-api-skinning` tip)

- [ ] **Step 0.1: Confirm W1 branch tip and port repo state**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix
git worktree list
git -C .worktrees/unity-workstream-01-api-skinning log -1 --oneline
```

Expected: W1 worktree exists and its tip is `831afd42` or the current Workstream 1 tip as of port state. Note this SHA as the base.

- [ ] **Step 0.2: Create new worktree and branch**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix
git worktree add .worktrees/unity-workstream-05-hillaire -b unity-workstream/05-hillaire-atmosphere unity-workstream/01-api-skinning
```

Expected: new worktree at `.worktrees/unity-workstream-05-hillaire/`, new branch created off W1 tip. `git worktree list` now shows three worktrees (main repo, W1, W5).

- [ ] **Step 0.3: Initialize submodules in the new worktree**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-05-hillaire
git submodule update --init --recursive
```

Expected: all submodules present (should match W1 worktree's submodule state since we branched off W1 tip).

- [ ] **Step 0.4: Baseline release build in the new worktree**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-05-hillaire
meson setup --buildtype=release build-release
meson compile -C build-release
```

Expected: clean build matching W1's state (since no code has changed yet). If this fails, STOP — something is wrong with the environment, not the port.

---

## Task 1: Pre-flight verification

**Files:** (read-only verification — no modifications)

- [ ] **Step 1.1: Check binding slots 200/201/202 are unused in the port**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-05-hillaire
git grep -n "= 200\|= 201\|= 202" -- 'src/dxvk/shaders/rtx/pass/common_binding_indices.h'
git grep -n "200\|201\|202" -- 'src/dxvk/shaders/rtx/pass/common_bindings.slangh'
```

Expected: slots 200/201/202 are not defined in either file. If they are, ESCALATE — Hillaire uses these slots and a collision means the port has taken them for something unrelated that upstream added after the gmod fork base. Options: remap Hillaire's slots, or remap the conflicting code's slots. Decision needs human review.

- [ ] **Step 1.2: Check `rtx.atmosphere.*` option namespace is clean in the port**

```bash
git grep -n "rtx.atmosphere\|rtx_atmosphere\|RTX_OPTION.*atmosphere" -- 'src/dxvk/rtx_render/'
```

Expected: zero matches. If there are matches, ESCALATE — the port already has atmosphere-related options we need to reconcile with Hillaire's 17 new options. Scoping diagnostic confirmed no collision at spec-write time; this step re-verifies at implementation time.

- [ ] **Step 1.3: Check `rtx.skyMode` is not already defined**

```bash
git grep -n "skyMode\|RTX_OPTION.*skyMode" -- 'src/dxvk/rtx_render/'
```

Expected: zero matches. If matches exist, ESCALATE.

- [ ] **Step 1.4: Check port's rtxdi submodule pin**

```bash
git submodule status submodules/rtxdi
```

Record the current SHA. If it is `a95f9403` or newer (check via `git -C submodules/rtxdi log --oneline a95f9403..HEAD` returning output), no bump will be needed later. If it's older than `a95f9403` (`git -C submodules/rtxdi log --oneline HEAD..a95f9403` returns output), Task 3 will bump it.

- [ ] **Step 1.5: Note the starting commit SHA**

```bash
git log -1 --oneline
```

Record this SHA — this is the W5 base. It should match the W1 tip used in Step 0.2.

---

## Task 2: Generate and apply the Hillaire patch

**Files:**
- Create (temp): `/tmp/hillaire-63eda6b0.patch` (patch file, not committed to repo)
- Modify: all files in the File Map (via `git apply --3way`)

- [ ] **Step 2.1: Generate filtered patch from gmod repo using pathspec exclusions**

```bash
cd c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod
git diff 63eda6b0^..63eda6b0 -- \
  ':(exclude)*build_dxvk_release*' \
  ':(exclude)submodules/rtxdi' \
  > /tmp/hillaire-63eda6b0.patch
wc -l /tmp/hillaire-63eda6b0.patch
```

Expected: patch file produced, several thousand lines. Using `git diff` with pathspec exclusions is cleaner than `format-patch` + post-hoc filtering because it excludes atomically at generation time.

- [ ] **Step 2.2: Verify the patch has the right shape**

```bash
grep -c "^diff --git" /tmp/hillaire-63eda6b0.patch
grep "build_dxvk_release\|submodules/rtxdi" /tmp/hillaire-63eda6b0.patch
grep "^diff --git" /tmp/hillaire-63eda6b0.patch | head -30
```

Expected:
- `diff --git` count is 27 (29 files total minus stray script minus rtxdi submodule)
- grep for "build_dxvk_release" or "submodules/rtxdi" returns zero matches
- The listed `diff --git` lines cover the expected File Map files

If the count is wrong, STOP and investigate — re-check the pathspec exclusions.

- [ ] **Step 2.4: Apply the filtered patch with 3-way merge**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-05-hillaire
git apply --3way --index /tmp/hillaire-63eda6b0.patch
```

Two possible outcomes:

**Outcome A — clean apply:** patch applies without conflicts. Proceed to Task 3.

**Outcome B — conflicts:** `git apply --3way` reports conflicts in one or more of the three overlapping files (`rtx_context.cpp`, `integrator_direct.slangh`, `integrator_indirect.slangh`). Proceed to Step 2.5.

- [ ] **Step 2.5: Resolve conflicts (if any) in the three overlapping files**

For each conflicted file:

1. Read the conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`) to see the two versions.
2. Read the same file on the unity branch for context:

```bash
git -C c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod show unity:src/dxvk/rtx_render/rtx_context.cpp > /tmp/unity_rtx_context.cpp
```

3. Read the file on the W1 base (our side of the 3-way merge) for context:

```bash
git show unity-workstream/01-api-skinning:src/dxvk/rtx_render/rtx_context.cpp > /tmp/w1_rtx_context.cpp
```

4. **Preserve BOTH change sets** — W1's changes AND Hillaire's changes. They should land in different regions of each file (per the spec's reconciliation table). Edit the file to include both, removing conflict markers.

5. Stage the resolved file:

```bash
git add <file>
```

6. **Escalation rule:** if the conflict requires SEMANTIC resolution (not just concatenating two non-overlapping hunks), STOP and escalate to the human. Do not make up a merge policy.

Expected after resolution: `git status` shows all modified/new files staged, no unresolved conflict markers (`git diff --cached | grep -c '^<<<<<<<'` returns 0).

- [ ] **Step 2.6: Verify all expected files are staged**

```bash
git status --short | head -40
```

Expected: ~28 files in the staging area (29 minus the excluded build script). New files under `src/dxvk/shaders/rtx/pass/atmosphere/` and `src/dxvk/rtx_render/rtx_atmosphere.{cpp,h}`. Modifications to files in the File Map. NO `build_dxvk_release (2).ps1`. NO submodules/rtxdi change.

---

## Task 3: Build, verify, and commit the feature

**Files:**
- Modify: all staged files from Task 2 (committing them)

- [ ] **Step 3.1: Build in release mode**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-05-hillaire
meson compile -C build-release 2>&1 | tee /tmp/w5-build.log
```

Expected: clean compile. New files (`rtx_atmosphere.cpp`, three `.comp.slang`) compile. No new warnings in the tail.

If compile fails: diagnose the error. Common failure modes:
- Missing include: check that Hillaire's new headers are wired up in the file(s) that use them
- Missing ImGui helper: gmod may have had an ImGui helper the port doesn't. Stub or adapt minimally. If non-trivial, ESCALATE.
- Slang shader error: check `atmosphere_common.slangh` for any upstream-drifted macro/type references
- Type mismatch in `rtx_options.h`: check upstream's `RTX_OPTION` macro signature hasn't drifted

- [ ] **Step 3.2: Verify static_assert from W1 still holds**

```bash
grep -n "static_assert.*sizeof.*remixapi_Interface" src/dxvk/rtx_render/rtx_remix_api.cpp public/include/remix/remix.h
```

Expected: both should show `== 280`. This workstream does not touch the API surface, so this should be unchanged. If it's different, something went wrong.

- [ ] **Step 3.3: Commit the feature**

```bash
git commit -m "$(cat <<'EOF'
feat(atmosphere): port Hillaire atmosphere from gmod

Backports dxvk-remix-gmod commit 63eda6b0 ("Hillaire atmosphere
backported from atmos branch") on top of Workstream 1 tip. Adds:

- RtxAtmosphere class managing three precomputed LUTs
  (transmittance, multiscattering, sky view)
- Three compute shaders for LUT generation
- atmosphere_common.slangh math header (~800 LOC)
- 17 new rtx.atmosphere.* RtxOptions + rtx.skyMode enum
- evalSkyRadiance() call in every ray-miss path
- ImGui presets UI for tuning atmosphere parameters
- Frame-graph integration in rtx_context.cpp

Reconciled with Workstream 1's overlapping changes in rtx_context.cpp,
integrator_direct.slangh, and integrator_indirect.slangh. No conflicts
with Workstream 1's remixapi_Interface layout (280 bytes preserved).

Source: dxvk-remix-gmod commit 63eda6b0 (equivalent: acfc7e59).
Stray file build_dxvk_release (2).ps1 excluded. rtxdi submodule
bump (if needed) landed as a separate commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git log -1 --oneline
```

Expected: new commit on `unity-workstream/05-hillaire-atmosphere`. Record the SHA.

---

## Task 4: rtxdi submodule bump (conditional)

Skip this entire task if Step 1.4 established that the port's current rtxdi pin is at or newer than `a95f9403`.

**Files:**
- Modify: `submodules/rtxdi` (submodule pointer)

- [ ] **Step 4.1: Bump rtxdi submodule to `a95f9403`**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-05-hillaire
cd submodules/rtxdi
git fetch origin
git checkout a95f9403
cd ../..
git add submodules/rtxdi
```

Expected: `git diff --cached submodules/rtxdi` shows pointer change to `a95f9403`.

- [ ] **Step 4.2: Rebuild to confirm rtxdi bump doesn't break anything**

```bash
meson compile -C build-release
```

Expected: clean compile. If the bump breaks anything in the port, ESCALATE — may need to pick an intermediate rtxdi commit or skip the bump entirely.

- [ ] **Step 4.3: Commit the submodule bump as its own commit**

```bash
git commit -m "$(cat <<'EOF'
chore(submodule): bump rtxdi to a95f9403 for Hillaire atmosphere

Matches dxvk-remix-gmod's rtxdi pin at the Hillaire atmosphere commit
63eda6b0. Bump isolated from the feature commit so it can be reverted
independently if it introduces unrelated RTX-DI regressions.

Previous: <recorded SHA from Step 1.4>
New: a95f9403

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git log -1 --oneline
```

---

## Task 5: Deploy and runtime validation

**Files:**
- Copy: built `d3d9.dll` → `D:/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll`

- [ ] **Step 5.1: Install build artifacts**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-05-hillaire
meson install -C build-release --tags output
```

Expected: `_output/d3d9.dll` present and fresh.

- [ ] **Step 5.2: Deploy to Skyrim**

```bash
cp _output/d3d9.dll "/d/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll"
ls -la "/d/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll"
```

Expected: fresh mtime, size consistent with release build (~85 MB).

- [ ] **Step 5.3: Runtime test — launch Skyrim and load an outdoor save**

Launch Skyrim Special Edition. Load a save in an open exterior cell (Solitude, Whiterun plains, anywhere with visible sky). Let the scene render for 10-20 seconds.

- [ ] **Step 5.4: Visual confirmation of physical sky**

Inspect the Remix window. Confirm:
- Sky is rendered with atmospheric scattering (gradient from horizon to zenith, colored by sun position)
- Sun appears at the elevation the plugin configures (`rtx.atmosphere.sunElevation = 77.9°` per config)
- Distance fog uses Hillaire scattering, not flat skybox sampling

If the sky renders as a flat skybox or is black / solid color, physical sky is not active. Possible causes:
- `rtx.skyMode` is not set to `PhysicalAtmosphere` (check `rtx.conf`, may need default change)
- `evalSkyRadiance()` is not being called in the miss shader (build issue or integrator shader merge went wrong)
- LUTs are not being dispatched (check dxvk log for `RtxAtmosphere` compute dispatches)

- [ ] **Step 5.5: Log verification — plugin log**

```bash
grep "rtx.atmosphere" "C:/Users/mystery/Documents/My Games/Skyrim Special Edition/SKSE/SkyrimRemixPlugin.log" | head -20
```

Expected: NO `SetConfigVariable failed for key 'rtx.atmosphere.*' ... key not registered` warnings. The plugin should successfully set all atmosphere config variables.

- [ ] **Step 5.6: Log verification — dxvk log**

```bash
grep -i "atmosphere\|RtxAtmosphere\|transmittance_lut\|sky_view_lut\|multiscattering_lut" "D:/SteamLibrary/steamapps/common/Skyrim Special Edition/rtx-remix/logs/remix-dxvk.log" | head -20
```

Expected: evidence that `RtxAtmosphere` LUT compute passes ran (or were set up). Absence of errors related to atmosphere.

- [ ] **Step 5.7: Regression check — Workstream 1 features still work**

- Scene renders non-white (W1's vtable fix still in place)
- Plugin submits meshes (W1's API surface still correct)
- No new `[RTX-Diag]` logs (diagnostic instrumentation already reverted at W1 tip)

If ANY W1 regression appears, STOP. The shader-merge reconciliation may have damaged W1's view-model filter or Present path.

- [ ] **Step 5.8: Record final state**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-05-hillaire
git log --oneline unity-workstream/01-api-skinning..HEAD
```

Expected: 1 or 2 commits on top of W1 tip (feature commit + optional rtxdi bump).

---

## Workstream merge readiness (not part of task execution — documented for later)

When this workstream is validated, it becomes a candidate for merge into the unity integration branch alongside Workstream 1. Do NOT merge until:
- All runtime validation in Task 5 passes.
- The user has confirmed physical sky renders correctly on multiple save/cell combinations (not just one exterior).
- Any Bug 1 (overlay toggle) fix work has not introduced conflicts.

Merge is out of scope for this plan; it belongs to a separate integration-merge task.
