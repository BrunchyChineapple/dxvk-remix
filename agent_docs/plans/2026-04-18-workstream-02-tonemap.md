# Unity Fork Port — Workstream 2: Tonemap Operators Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the TonemapOperator enum + Hable Filmic / AgX / Lottes 2016 operators + Direct mode + per-operator parameter UI from `dxvk-remix-gmod` into the unity-fork port, shaped from day one to the fork-touchpoint pattern.

**Architecture:** Five commits on a new worktree branched off `unity-fork-touchpoint-refactor` at tip `dfe2e43c`. Commit 1 scaffolds the fork-owned `rtx_fork_tonemap` module and hook declarations. Commit 2 introduces the `TonemapOperator` enum and routes existing ACES behavior through a fork-owned dispatcher (behavior-preserving refactor). Commits 3–5 add Hable Filmic (with Direct mode and sliders), AgX, and Lottes 2016 as additive extensions. Each commit is independently buildable and runtime-green. Validation is behavioral: build clean + Skyrim renders each operator correctly.

**Tech Stack:** C++20 with `RTX_OPTION` macros, Slang/HLSL shaders, Meson+Ninja build system, Git worktrees. Validation via Skyrim Remix mod (primary iteration target, plugin log baseline from Workstream 5).

**Commit authorship:** All commits in this plan author as `Kim2091 <jpavatargirl@gmail.com>` only. **No AI co-author trailers on any commit.**

**Reference repo for source material (read-only):** `c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod`. The four gmod commits (`acfaa6ab`, `baad5e79`, `f3501d46`, `cdf2c723`) are source-of-truth for behavior, parameter defaults, and shader operator implementations. This plan does NOT apply them as patches — gmod's commits sit on top of a different base (gmod had a pre-existing `useAgX` boolean that the port lacks). Instead, each commit in this plan synthesizes its target state from gmod's tip-of-Lottes (`cdf2c723`) for the relevant operator, applied to the port's actual upstream-tonemap baseline. Read the gmod files for concrete implementations; paste operator function bodies verbatim where named below.

---

## File Structure

### Created by this plan

**New fork-owned C++:**
- `src/dxvk/rtx_render/rtx_fork_tonemap.h` — `TonemapOperator` enum declaration, hook function declarations (mirror of the entries added to `rtx_fork_hooks.h`).
- `src/dxvk/rtx_render/rtx_fork_tonemap.cpp` — per-operator `RTX_OPTION` declarations (Hable, AgX, Lottes, Direct-mode), hook implementations.

**New fork-owned shaders:**
- `src/dxvk/shaders/rtx/pass/tonemap/fork_tonemap_operators.slangh` — `hable()` function, `applyTonemapOperator(uint op, vec3 color, …)` dispatcher, `#include`s `AgX.hlsl` and `Lottes.hlsl`.
- `src/dxvk/shaders/rtx/pass/tonemap/AgX.hlsl` — port byte-for-byte from gmod `f3501d46`.
- `src/dxvk/shaders/rtx/pass/tonemap/Lottes.hlsl` — port byte-for-byte from gmod `cdf2c723`.

### Modified by this plan (upstream touchpoints)

Each modification is accompanied by a matching entry in `docs/fork-touchpoints.md` in the same commit.

| File | Change |
|---|---|
| `src/dxvk/rtx_render/rtx_fork_hooks.h` | Add 5 hook declarations under `fork_hooks::` namespace. |
| `src/dxvk/rtx_render/rtx_tone_mapping.h` | Remove `finalizeWithACES` RtxOption. Remove `useAgX` if present. Add `#include "rtx_fork_tonemap.h"`. |
| `src/dxvk/rtx_render/rtx_tone_mapping.cpp` | 3 one-line hook calls (args population, UI, curve-skip check). |
| `src/dxvk/rtx_render/rtx_local_tone_mapping.h` | Remove local equivalent of `finalizeWithACES` if present. Add `#include "rtx_fork_tonemap.h"`. |
| `src/dxvk/rtx_render/rtx_local_tone_mapping.cpp` | Same 3 hook call pattern as global. |
| `src/dxvk/rtx_render/rtx_context.cpp` | Direct-mode dispatch branch via `shouldSkipToneCurve()` hook. |
| `src/dxvk/rtx_render/rtx_options.h` | Remove `useLegacyACES` + `showLegacyACESOption` RtxOptions if present here (they may live in rtx_tone_mapping.h instead — verify at execution time). |
| `src/dxvk/imgui/dxvk_imgui.cpp` | Replace the `Finalize With ACES` checkbox with a call to `fork_hooks::showTonemapOperatorUI()`. |
| `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h` | Swap `finalizeWithACES`/`useLegacyACES` uint fields for `tonemapOperator` + `directOperatorMode` + per-operator param fields. Add `tonemapOperator*` constants. `static_assert(sizeof(ToneMappingApplyToneMappingArgs) == N)` where N is captured before the swap. |
| `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.slangh` | Add `#include "fork_tonemap_operators.slangh"`. Replace inline ACES call with `applyTonemapOperator(...)` dispatcher call. |
| `src/dxvk/shaders/rtx/pass/tonemap/tonemapping_apply_tonemapping.comp.slang` | Replace inline ACES branch with dispatcher call. |
| `src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h` | Same args-struct swap as global header. |
| `src/dxvk/shaders/rtx/pass/local_tonemap/final_combine.comp.slang` | Dispatcher call. |
| `src/dxvk/shaders/rtx/pass/local_tonemap/luminance.comp.slang` | Propagate operator field where touched in gmod `acfaa6ab`. |
| `src/dxvk/meson.build` | Register `rtx_fork_tonemap.cpp`, `AgX.hlsl`, `Lottes.hlsl`, `fork_tonemap_operators.slangh` as the commits that add them land. |
| `docs/fork-touchpoints.md` | Add entries for every file listed above, in the commit that introduces each touch. |

### Not modified

- `public/include/remix/remix_c.h` — Remix plugin ABI. Untouched.
- Any `remixapi_*` struct — plugin ABI. Untouched.

---

## Task 0: Worktree and branch setup

**Files:**
- Create: `.worktrees/unity-workstream-02-tonemap/` (worktree).
- Create: `unity-workstream/02-tonemap` (branch, based on `unity-fork-touchpoint-refactor` at `dfe2e43c`).

- [ ] **Step 0.1: Confirm port integration tip**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix
git worktree list
git -C .worktrees/unity-fork-touchpoint-refactor log -1 --oneline
```

Expected: `unity-fork-touchpoint-refactor` worktree exists, tip is `dfe2e43c` (`Fix build errors surfaced by first full meson compile`).

If the tip is a different SHA, use that SHA for Step 0.2 — the touchpoint refactor may have advanced since this plan was written.

- [ ] **Step 0.2: Create worktree and branch**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix
git worktree add .worktrees/unity-workstream-02-tonemap -b unity-workstream/02-tonemap unity-fork-touchpoint-refactor
```

Expected: worktree created at `.worktrees/unity-workstream-02-tonemap/`; branch `unity-workstream/02-tonemap` checked out inside it.

- [ ] **Step 0.3: Initialize submodules**

Fresh worktrees don't inherit submodule checkouts. Initialize them before the first build.

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap
git submodule update --init --recursive
```

Expected: all submodules present and at the branch's pinned SHAs.

- [ ] **Step 0.4: Baseline release build**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap
powershell.exe -NoProfile -Command "& { cd '$(pwd -W)'; . .\build_common.ps1; PerformBuild -BuildFlavour release -BuildSubDir _Comp64Release -Backend ninja -EnableTracy false }"
```

Expected: clean build matching the touchpoint-refactor tip (no source changes yet). If this fails, STOP — environment issue, not port work.

Record the location of the built `d3d9.dll` (under `_Comp64Release/`); it is used for runtime validation in Task 7.

---

## Task 1: Pre-flight verification

**Files:** (read-only verification — no modifications)

- [ ] **Step 1.1: Confirm port's tonemap files are at clean upstream-NVIDIA shape**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap
grep -n "finalizeWithACES\|useAgX\|useLegacyACES\|tonemapOperator" \
  src/dxvk/rtx_render/rtx_tone_mapping.h \
  src/dxvk/rtx_render/rtx_tone_mapping.cpp \
  src/dxvk/rtx_render/rtx_local_tone_mapping.h \
  src/dxvk/rtx_render/rtx_local_tone_mapping.cpp \
  src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h \
  src/dxvk/shaders/rtx/pass/tonemap/tonemapping.slangh
```

Expected:
- `finalizeWithACES` appears in `rtx_tone_mapping.h` (as RTX_OPTION), in `rtx_tone_mapping.cpp` (in `dispatchApplyToneMapping`), and in `tonemapping.h` (as uint field in `ToneMappingApplyToneMappingArgs`).
- `useLegacyACES` appears in `tonemapping.h` (as uint field) and possibly in `rtx_options.h`.
- `useAgX` does NOT appear anywhere — gmod's AgX never landed in the port.
- `tonemapOperator` does NOT appear — enum refactor has not been ported yet.

If `useAgX` or `tonemapOperator` is already present, STOP and escalate — the port has drifted from the expected baseline and this plan needs reconciliation.

- [ ] **Step 1.2: Record the current size of the shader args structs**

Capture the exact byte size of `ToneMappingApplyToneMappingArgs` and the local equivalent. These are the invariants for Task 3's `static_assert`.

```bash
grep -n "struct ToneMappingApplyToneMappingArgs" -A 30 \
  src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h
```

Count the fields: 4× `uint` block, 4× float block, `vec3 colorBalance` + `uint colorGradingEnabled`, then `float saturation`, `float toneCurveMinStops`, `float toneCurveMaxStops`, `uint finalizeWithACES`, `uint ditherMode`, `uint frameIndex`, `uint useLegacyACES`, `uint pad1`.

Compute the size in bytes (each uint = 4, each float = 4, vec3 = 12). Record the number — it is the value pinned in the `static_assert` in Task 3 Step 3.

Do the same for the local args struct in `src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h`.

- [ ] **Step 1.3: Confirm no tonemap entries exist in `docs/fork-touchpoints.md` yet**

```bash
grep -n "tonemap\|tone_mapping\|tonemapping" docs/fork-touchpoints.md || echo "no tonemap entries — expected"
```

Expected: no tonemap entries. This workstream is adding them fresh.

- [ ] **Step 1.4: Confirm `rtx_fork_hooks.h` is at the touchpoint-refactor tip state**

```bash
grep -c "^    void " src/dxvk/rtx_render/rtx_fork_hooks.h
```

Expected: approximately 30 hook declarations (the touchpoint-refactor ended at 15 migrated commits). The exact count doesn't need to match a hardcoded number — this is just a sanity check that the header is populated as expected.

- [ ] **Step 1.5: Record starting commit SHA**

```bash
git log -1 --oneline
```

Expected: tip is `dfe2e43c` (or whatever Step 0.1 confirmed). Record — this is the W2 base.

---

## Task 2: Commit 1 — Scaffold `rtx_fork_tonemap` module

This commit creates empty scaffolding. No behavior change. Makes the module visible to the build system so Task 3's refactor has a home for its hook implementations.

**Files:**
- Create: `src/dxvk/rtx_render/rtx_fork_tonemap.h`
- Create: `src/dxvk/rtx_render/rtx_fork_tonemap.cpp`
- Modify: `src/dxvk/rtx_render/rtx_fork_hooks.h` (add 5 hook declarations)
- Modify: `src/dxvk/meson.build` (register new .cpp)
- Modify: `docs/fork-touchpoints.md` (seed entries marked pending)

- [ ] **Step 2.1: Create `rtx_fork_tonemap.h`**

Create `src/dxvk/rtx_render/rtx_fork_tonemap.h`:

```cpp
#pragma once

// rtx_fork_tonemap.h — fork-owned declarations for the tonemap operator
// enum and per-operator parameter plumbing. The operator logic itself
// lives in rtx_fork_tonemap.cpp and in the fork-owned shader headers
// under src/dxvk/shaders/rtx/pass/tonemap/fork_tonemap_operators.slangh
// (plus AgX.hlsl and Lottes.hlsl).
//
// See docs/fork-touchpoints.md for the index of upstream files that
// call into fork_hooks::... for tonemap operator dispatch and UI.

#include <cstdint>

namespace dxvk {

  // Tonemapping operator applied after the dynamic tone curve.
  // Shader-side constants live in shaders/rtx/pass/tonemap/tonemapping.h
  // as `tonemapOperator*` uints; these two enumerations MUST stay in
  // lockstep. The populateTonemapOperatorArgs hook is the single place
  // that casts between them.
  enum class TonemapOperator : uint32_t {
    None        = 0, // Dynamic curve only; no additional operator.
    ACES        = 1,
    ACESLegacy  = 2,
    HableFilmic = 3,
    AgX         = 4,
    Lottes      = 5,
  };

  // Tonemap dispatch mode. Selects whether the global tonemapper runs
  // the full dynamic-curve pipeline, or the operator alone applied to
  // the exposure-adjusted input (Direct mode, from gmod baad5e79).
  enum class TonemappingMode : uint32_t {
    Global = 0,
    Local  = 1,
    Direct = 2,
  };

} // namespace dxvk
```

- [ ] **Step 2.2: Create empty `rtx_fork_tonemap.cpp`**

Create `src/dxvk/rtx_render/rtx_fork_tonemap.cpp`:

```cpp
// rtx_fork_tonemap.cpp
//
// Fork-owned implementations of the tonemap operator hooks declared in
// rtx_fork_hooks.h. Populated incrementally across Workstream 2 commits:
//   - Commit 1 (this file): scaffold only.
//   - Commit 2: TonemapOperator enum + ACES-through-dispatcher.
//   - Commit 3: Hable Filmic + Direct mode + Hable sliders.
//   - Commit 4: AgX operator + AgX sliders.
//   - Commit 5: Lottes 2016 operator + Lottes sliders.
//
// See docs/fork-touchpoints.md for the catalogue of fork hooks and
// which upstream files call each one.

#include "rtx_fork_hooks.h"
#include "rtx_fork_tonemap.h"

namespace dxvk {
  namespace fork_hooks {

    // Hook implementations land here in subsequent commits.

  } // namespace fork_hooks
} // namespace dxvk
```

- [ ] **Step 2.3: Add hook declarations to `rtx_fork_hooks.h`**

Open `src/dxvk/rtx_render/rtx_fork_hooks.h`. Near the bottom of the `namespace fork_hooks { ... }` block (before the closing brace, alongside the existing alphabetical grouping), add:

```cpp
    // Populates the tonemap-operator-related fields of the global tonemapper's
    // shader args struct (tonemapOperator, directOperatorMode, Hable params,
    // AgX params, Lottes params). Called from DxvkToneMapping::dispatchApplyToneMapping.
    // No private-member access — uses public RtxOption accessors only.
    // Implementation in rtx_fork_tonemap.cpp.
    void populateTonemapOperatorArgs(struct ToneMappingApplyToneMappingArgs& args);

    // Populates the tonemap-operator-related fields of the local tonemapper's
    // shader args struct. Called from DxvkLocalToneMapping's args-population site.
    // No private-member access.
    // Implementation in rtx_fork_tonemap.cpp.
    void populateLocalTonemapOperatorArgs(struct ToneMappingFinalCombineArgs& args);

    // Renders the Tonemapping Operator combo + per-operator parameter sliders +
    // Direct-mode toggle inside DxvkToneMapping::showImguiSettings. Called from
    // the fork-hook call site replacing the old "Finalize With ACES" checkbox.
    // No private-member access — uses only public RtxOption / ImGui APIs.
    // Implementation in rtx_fork_tonemap.cpp.
    void showTonemapOperatorUI();

    // Same as showTonemapOperatorUI, but rendered inside
    // DxvkLocalToneMapping::showImguiSettings. Separate hook because the local
    // panel hosts a subset of operator controls (no global-only options).
    // Implementation in rtx_fork_tonemap.cpp.
    void showLocalTonemapOperatorUI();

    // Returns true when TonemappingMode::Direct is active. Callers in the
    // global tonemap dispatch path (RtxContext / DxvkToneMapping::dispatch)
    // use this to skip histogram, tone-curve, and local-pyramid passes and
    // apply the operator alone to the exposure-adjusted input.
    // No private-member access.
    // Implementation in rtx_fork_tonemap.cpp.
    bool shouldSkipToneCurve();
```

Note the `struct ` keyword prefix on both args-struct parameter types — the struct definitions live in the shader-shared header `tonemapping.h` which may not be transitively included by every file that pulls in `rtx_fork_hooks.h`. Forward-referencing via `struct <name>` keeps the declaration compiling without forcing callers to pull the shader header.

- [ ] **Step 2.4: Register the new .cpp in meson**

```bash
grep -n "rtx_fork_" src/dxvk/meson.build | head -10
```

Locate the block where other `rtx_fork_*.cpp` files are listed. Add (alphabetical insertion):

```meson
  'rtx_render/rtx_fork_tonemap.cpp',
```

- [ ] **Step 2.5: Seed fork-touchpoints.md entries (pending)**

Open `docs/fork-touchpoints.md`. Under the top-level "Upstream files touched by the fork" section, add sections for the files Task 3 onward will modify. Each entry is marked `[pending commit 2]` and gets filled in when that commit lands.

Template entries to add alphabetically:

```markdown
## src/dxvk/imgui/dxvk_imgui.cpp

- **[pending commit 2]** Hook at tonemapper ImGui settings → `fork_hooks::showTonemapOperatorUI` in `rtx_fork_tonemap.cpp`.
  *Replaces the old "Finalize With ACES" checkbox with the operator combo + per-operator sliders.*

## src/dxvk/rtx_render/rtx_context.cpp

- **[pending commit 3]** Inline tweak at tonemap dispatch point — Direct-mode branch via `fork_hooks::shouldSkipToneCurve`.
  *Skips histogram + tone-curve + local-pyramid passes when Direct mode is selected.*

## src/dxvk/rtx_render/rtx_local_tone_mapping.cpp

- **[pending commit 2]** Hook calls at local tonemapper args-population + ImGui settings → `fork_hooks::populateLocalTonemapOperatorArgs` + `fork_hooks::showLocalTonemapOperatorUI` in `rtx_fork_tonemap.cpp`.
  *Routes local tonemap through the fork operator dispatcher.*

## src/dxvk/rtx_render/rtx_local_tone_mapping.h

- **[pending commit 2]** Inline tweak — add `#include "rtx_fork_tonemap.h"`; remove `finalizeWithACES` local RtxOption if present.
  *Adopts the fork enum.*

## src/dxvk/rtx_render/rtx_options.h

- **[pending commit 2]** Inline tweak — remove `useLegacyACES` + `showLegacyACESOption` RtxOptions (replaced by `tonemapOperator == ACESLegacy`).
  *Superseded by the fork enum.*

## src/dxvk/rtx_render/rtx_tone_mapping.cpp

- **[pending commit 2]** Hook calls at args-population + ImGui settings → `fork_hooks::populateTonemapOperatorArgs` + `fork_hooks::showTonemapOperatorUI` in `rtx_fork_tonemap.cpp`.
  *Routes global tonemap through the fork operator dispatcher.*

## src/dxvk/rtx_render/rtx_tone_mapping.h

- **[pending commit 2]** Inline tweak — add `#include "rtx_fork_tonemap.h"`; remove `finalizeWithACES` RtxOption.
  *Adopts the fork enum.*

## src/dxvk/shaders/rtx/pass/local_tonemap/final_combine.comp.slang

- **[pending commit 2]** Inline tweak — replace inline ACES branch with `applyTonemapOperator(...)` dispatcher call.
  *Operator dispatch via fork-owned shader header.*

## src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h

- **[pending commit 2]** Inline tweak — swap `finalizeWithACES`/`useLegacyACES` uints in local args struct for `tonemapOperator` + operator-param fields; preserve struct size via pad slots.
  *Operator field propagation.*

## src/dxvk/shaders/rtx/pass/local_tonemap/luminance.comp.slang

- **[pending commit 2]** Inline tweak — propagate operator field in the local-luminance pass (same shape as gmod acfaa6ab).
  *Operator field propagation.*

## src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h

- **[pending commit 2]** Inline tweak — swap `finalizeWithACES`/`useLegacyACES` uints in global args struct for `tonemapOperator` + operator-param fields; add `tonemapOperator*` shader constants; preserve struct size with `static_assert`.
  *Operator field canonicalized.*

## src/dxvk/shaders/rtx/pass/tonemap/tonemapping.slangh

- **[pending commit 2]** Inline tweak — `#include "fork_tonemap_operators.slangh"` + replace inline ACES call with `applyTonemapOperator(...)` dispatcher call.
  *Routes global tonemap shader through the fork operator dispatcher.*

## src/dxvk/shaders/rtx/pass/tonemap/tonemapping_apply_tonemapping.comp.slang

- **[pending commit 2]** Inline tweak — replace inline ACES finalize branch with `applyTonemapOperator(...)` dispatcher call.
  *Operator dispatch via fork-owned shader header.*
```

If any file listed above already has a section in `fork-touchpoints.md` from prior workstreams, append new entries to it instead of duplicating the heading.

- [ ] **Step 2.6: Build — confirm scaffolding compiles**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap
powershell.exe -NoProfile -Command "& { cd '$(pwd -W)'; . .\build_common.ps1; PerformBuild -BuildFlavour release -BuildSubDir _Comp64Release -Backend ninja -EnableTracy false }"
```

Expected: clean build. No warnings. The empty `rtx_fork_tonemap.cpp` compiles, `rtx_fork_hooks.h` parses, and the hook declarations are recognized by the build system (even though no implementations exist yet — the declarations have no call sites yet, so no link errors).

- [ ] **Step 2.7: Commit**

```bash
git add src/dxvk/rtx_render/rtx_fork_tonemap.h \
        src/dxvk/rtx_render/rtx_fork_tonemap.cpp \
        src/dxvk/rtx_render/rtx_fork_hooks.h \
        src/dxvk/meson.build \
        docs/fork-touchpoints.md
git status
git commit -m "$(cat <<'EOF'
fork(tonemap): scaffold rtx_fork_tonemap module + hook decls

Workstream 2 (tonemap operators), commit 1 of 5. Pure scaffolding;
no upstream code changes, no behavior change.

Adds the fork-owned rtx_fork_tonemap module (empty .cpp + .h with
TonemapOperator + TonemappingMode enums), registers it in the build,
and declares five new hooks under fork_hooks:: for operator dispatch
and ImGui surfacing. Seeds docs/fork-touchpoints.md with [pending]
entries for the upstream files commits 2-5 will touch.

Refs: gmod acfaa6ab, baad5e79, f3501d46, cdf2c723.
EOF
)"
git log -1 --format=full
```

Expected: single new commit. `Author:` shows `Kim2091`. No `Co-Authored-By:` line. Record the SHA.

---

## Task 3: Commit 2 — TonemapOperator enum + ACES routed through dispatcher

This is the risky commit. Behavior-preserving refactor that swaps the shader args struct shape, replaces the old ACES checkbox with an operator combo, and routes existing ACES rendering through the fork dispatcher. After this commit, ACES default still renders identically; the other enum values (`ACESLegacy`, `None`) are selectable and behave as expected.

**Files (see File Structure section for full list — reproduced here for this commit's scope):**
- Modify: `src/dxvk/rtx_render/rtx_fork_tonemap.cpp` — implement args, UI, and curve-skip hooks (ACES/ACESLegacy/None only).
- Modify: `src/dxvk/rtx_render/rtx_tone_mapping.h` — remove `finalizeWithACES` RtxOption; `#include "rtx_fork_tonemap.h"`.
- Modify: `src/dxvk/rtx_render/rtx_tone_mapping.cpp` — replace inline args assignments + ImGui with hook calls.
- Modify: `src/dxvk/rtx_render/rtx_local_tone_mapping.h` + `.cpp` — same pattern for local path.
- Modify: `src/dxvk/rtx_render/rtx_options.h` — remove `useLegacyACES` + `showLegacyACESOption` if present.
- Modify: `src/dxvk/imgui/dxvk_imgui.cpp` — one-line hook call replacing the checkbox.
- Create: `src/dxvk/shaders/rtx/pass/tonemap/fork_tonemap_operators.slangh` — dispatcher + ACES routing.
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h` — args struct swap + operator constants + static_assert.
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.slangh` — include fork helper + dispatcher call.
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/tonemapping_apply_tonemapping.comp.slang` — dispatcher call.
- Modify: `src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h` — args swap.
- Modify: `src/dxvk/shaders/rtx/pass/local_tonemap/final_combine.comp.slang` — dispatcher call.
- Modify: `src/dxvk/shaders/rtx/pass/local_tonemap/luminance.comp.slang` — operator field propagation.
- Modify: `src/dxvk/meson.build` — register `fork_tonemap_operators.slangh`.
- Modify: `docs/fork-touchpoints.md` — convert `[pending commit 2]` entries to final form.

- [ ] **Step 3.1: Swap global shader args struct — `tonemapping.h`**

Open `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h`.

**3.1a — Add operator constants** near the top (alongside the `ditherMode*` constants):

```c
// Tonemapping operator constants (matches TonemapOperator enum in rtx_fork_tonemap.h).
// Commit 2 lands None/ACES/ACESLegacy; later commits extend the range.
static const uint32_t tonemapOperatorNone        = 0; // Dynamic curve only.
static const uint32_t tonemapOperatorACES        = 1;
static const uint32_t tonemapOperatorACESLegacy  = 2;
```

**3.1b — Swap the uint fields in `ToneMappingApplyToneMappingArgs`:**

Current layout (port baseline, as captured in Step 1.2):

```c
struct ToneMappingApplyToneMappingArgs {
  uint toneMappingEnabled;
  uint debugMode;
  uint performSRGBConversion;
  uint enableAutoExposure;

  float shadowContrast;
  float shadowContrastEnd;
  float exposureFactor;
  float contrast;

  vec3 colorBalance;
  uint colorGradingEnabled;

  float saturation;
  float toneCurveMinStops;
  float toneCurveMaxStops;
  uint finalizeWithACES;

  uint ditherMode;
  uint frameIndex;
  uint useLegacyACES;
  uint pad1;
};
```

Target layout (after commit 2):

```c
struct ToneMappingApplyToneMappingArgs {
  uint toneMappingEnabled;
  uint debugMode;
  uint performSRGBConversion;
  uint enableAutoExposure;

  float shadowContrast;
  float shadowContrastEnd;
  float exposureFactor;
  float contrast;

  vec3 colorBalance;
  uint colorGradingEnabled;

  float saturation;
  float toneCurveMinStops;
  float toneCurveMaxStops;
  uint tonemapOperator;      // One of tonemapOperator* constants.

  uint ditherMode;
  uint frameIndex;
  uint directOperatorMode;   // 1 = Direct mode (commit 3). 0 in commit 2.
  uint pad1;                 // Filled by Hable/AgX/Lottes params in later commits.
};
```

**3.1c — Pin the size with `static_assert`:**

Immediately after the struct definition, add:

```c
#ifdef __cplusplus
static_assert(sizeof(ToneMappingApplyToneMappingArgs) == 96,
              "ToneMappingApplyToneMappingArgs size must be preserved by the operator-enum refactor.");
#endif
```

Verify the size value (`96`) by counting: 4× uint (16) + 4× float (16) + vec3 (12) + uint (4) + float ×3 (12) + uint (4) + 4× uint (16) = 80 … wait, that doesn't match. Re-count before writing the literal. The CORRECT way: after the swap, run `meson compile` and if the `static_assert` fires, read the actual size from the error message, update the literal, and re-compile. Until then, leave the size captured in Step 1.2 as a placeholder comment and verify in Step 3.13. **Do not commit a wrong size.**

- [ ] **Step 3.2: Swap local shader args struct — `local_tonemapping.h`**

Open `src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h`. Apply the same pattern: wherever `finalizeWithACES` and `useLegacyACES` appear in the local args struct, swap them for `tonemapOperator` + `directOperatorMode` + pad slots, preserving field positions and total size.

Add the same operator constants near the top if not already pulled in transitively from `tonemapping.h`.

Capture the local struct's original size from Step 1.2 for the matching `static_assert`.

- [ ] **Step 3.3: Create the fork-owned dispatcher shader — `fork_tonemap_operators.slangh`**

Create `src/dxvk/shaders/rtx/pass/tonemap/fork_tonemap_operators.slangh`:

```c
// fork_tonemap_operators.slangh — fork-owned tonemap operator dispatcher
// and operator function bodies.
//
// Upstream shader passes (tonemapping_apply_tonemapping.comp.slang,
// final_combine.comp.slang, and any code in tonemapping.slangh that
// previously branched on finalizeWithACES / useAgX) call
// applyTonemapOperator() from here instead of routing inline.
//
// Commit 2: dispatcher handles None / ACES / ACESLegacy.
// Commit 3: adds HableFilmic.
// Commit 4: adds AgX (includes AgX.hlsl).
// Commit 5: adds Lottes (includes Lottes.hlsl).

#ifndef FORK_TONEMAP_OPERATORS_SLANGH
#define FORK_TONEMAP_OPERATORS_SLANGH

#include "tonemapping.h"          // operator constants + existing args structs
#include "ACES.hlsl"               // ACES functions (upstream, imported here)
#include "tonemapping.slangh"      // ACESFilm / legacy ACES entry points

// applyTonemapOperator — dispatches to the operator implementation selected
// by `op`. Returns the operator-adjusted color. When op == None, returns
// `color` unchanged. The selection matches the TonemapOperator enum in
// rtx_fork_tonemap.h.
float3 applyTonemapOperator(uint op, float3 color, bool suppressBlackLevelClamp)
{
  if (op == tonemapOperatorACES)
  {
    return ACESFilm(color, /*useLegacyACES=*/false, suppressBlackLevelClamp);
  }
  else if (op == tonemapOperatorACESLegacy)
  {
    return ACESFilm(color, /*useLegacyACES=*/true, suppressBlackLevelClamp);
  }
  // op == tonemapOperatorNone (0) → identity.
  return color;
}

#endif // FORK_TONEMAP_OPERATORS_SLANGH
```

Verify the included header names match the port's actual file names. Adjust the `#include` paths if the relative-path convention differs from what's shown above.

- [ ] **Step 3.4: Update upstream shader `tonemapping.slangh` to route through the dispatcher**

Open `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.slangh`.

**3.4a — Add the fork-operator include** near the top alongside existing includes. (Or leave it out if the consuming compute shader includes the helper directly — decide based on the file's include layering.)

**3.4b — Replace inline ACES branch.** Search for the function that accepts `finalizeWithACES` / `useLegacyACES` bools and performs the final ACES pass. In the port baseline this is likely `ACESFilm(color, useLegacyACES, ...)` called from inside a broader apply function, or a direct branch inside `tonemapping_apply_tonemapping.comp.slang`.

Replace the inline decision with:

```c
color = applyTonemapOperator(cb.tonemapOperator, color, /*suppressBlackLevelClamp=*/false);
```

where `cb` is the constant-buffer alias used in the calling pass.

- [ ] **Step 3.5: Update `tonemapping_apply_tonemapping.comp.slang`**

Open `src/dxvk/shaders/rtx/pass/tonemap/tonemapping_apply_tonemapping.comp.slang`. Add `#include "fork_tonemap_operators.slangh"` if not transitively pulled in. Replace the inline `if (finalizeWithACES) { color = ACESFilm(...); }` block with:

```c
color = applyTonemapOperator(cb.tonemapOperator, color, /*suppressBlackLevelClamp=*/false);
```

Preserve whatever surrounding logic (sRGB conversion, dither, etc.) the file already has — this change is narrow.

- [ ] **Step 3.6: Update local tonemap shaders**

Open `src/dxvk/shaders/rtx/pass/local_tonemap/final_combine.comp.slang`. Apply the same dispatcher-call replacement for the local path's ACES branch.

Open `src/dxvk/shaders/rtx/pass/local_tonemap/luminance.comp.slang`. Propagate the `tonemapOperator` field wherever the old `finalizeWithACES` or `useLegacyACES` was read (gmod `acfaa6ab`'s local-luminance touch is usually a 2-line change).

- [ ] **Step 3.7: Register `fork_tonemap_operators.slangh` in meson**

```bash
grep -n "tonemapping.slangh\|tonemap/" src/dxvk/meson.build | head -10
```

Locate the shader inclusion block. Add `fork_tonemap_operators.slangh` to the dependency list if headers are listed, or rely on implicit pickup if that's the convention.

- [ ] **Step 3.8: Remove the old `finalizeWithACES` / legacy-ACES RtxOptions**

Open `src/dxvk/rtx_render/rtx_tone_mapping.h`. Delete the line:

```cpp
RTX_OPTION("rtx.tonemap", bool,  finalizeWithACES, false, "A flag to enable applying a final pass of ACES tonemapping to the tonemapped result.");
```

If `rtx_tone_mapping.h` also declares `useAgX` (it shouldn't for the port, but Step 1.1 would've flagged it), delete that line too.

Add `#include "rtx_fork_tonemap.h"` near the top of `rtx_tone_mapping.h` so the enum is visible to code that consumes the tonemap settings (if any do — otherwise this include is optional but harmless).

Open `src/dxvk/rtx_render/rtx_options.h`. Search for `useLegacyACES` and `showLegacyACESOption`. Delete their RtxOption declarations if present.

Open `src/dxvk/rtx_render/rtx_local_tone_mapping.h`. Remove the local equivalent of `finalizeWithACES` if present. Add `#include "rtx_fork_tonemap.h"` if needed.

- [ ] **Step 3.9: Add the `tonemapOperator` RtxOption**

Open `src/dxvk/rtx_render/rtx_fork_tonemap.h`. Just above the `} // namespace dxvk` closing brace, add the RtxOption declaration (if the project requires RtxOptions to live in specific class scopes, declare in the upstream tonemap class instead and document this in the spec; otherwise a free RtxOption works):

Actually — the cleaner shape: put the RtxOption in `rtx_fork_tonemap.cpp` as a static RtxOption tied to the `rtx.tonemap` namespace. This keeps the enum header clean and matches how gmod structures it.

Open `rtx_fork_tonemap.cpp` and inside `namespace dxvk { namespace fork_hooks { ... } }`, add (above the hook implementations you'll add in Step 3.10):

```cpp
// Tonemap operator selection. Replaces the upstream finalizeWithACES /
// useLegacyACES booleans. Default is None (dynamic curve only, matches the
// upstream behavior when finalizeWithACES was false — which is also its
// default).
static ::dxvk::RtxOption<TonemapOperator> s_tonemapOperator(
  "rtx.tonemap", "tonemapOperator", TonemapOperator::None,
  "Tonemapping operator applied after the dynamic tone curve.\n"
  "Supported values are 0 = None (dynamic curve only), 1 = ACES, "
  "2 = ACES (Legacy), 3 = Hable Filmic (commit 3), 4 = AgX (commit 4), "
  "5 = Lottes (commit 5).");

// Tonemapping mode. Direct (2) skips dynamic curve + local pyramid and
// applies the operator alone. Added incrementally in commit 3.
static ::dxvk::RtxOption<TonemappingMode> s_tonemappingMode(
  "rtx.tonemap", "tonemappingMode", TonemappingMode::Global,
  "Tonemap dispatch mode. 0 = Global (standard), 1 = Local (local "
  "tonemapper), 2 = Direct (operator-only, no tone curve).");
```

Verify the exact `RtxOption` constructor signature the project uses — the canonical pattern in the codebase is the `RTX_OPTION` macro; a direct `RtxOption<T>(namespace, name, default, desc)` may differ. If the macro form is required, use:

```cpp
RTX_OPTION("rtx.tonemap", TonemapOperator, tonemapOperator, TonemapOperator::None,
           "Tonemapping operator applied after the dynamic tone curve. ...");
RTX_OPTION("rtx.tonemap", TonemappingMode, tonemappingMode, TonemappingMode::Global,
           "Tonemap dispatch mode. ...");
```

Use whichever form matches other fork RtxOptions in the port (check `rtx_fork_api_entry.cpp` or `rtx_fork_light.cpp` for precedent).

- [ ] **Step 3.10: Implement the three commit-2 hooks in `rtx_fork_tonemap.cpp`**

Inside `namespace fork_hooks`, add the three hook implementations that are functional at commit 2 (populate, UI, curve-skip — all enum-value-routing only; no operator-specific params yet):

```cpp
    void populateTonemapOperatorArgs(ToneMappingApplyToneMappingArgs& args) {
      args.tonemapOperator     = static_cast<uint32_t>(tonemapOperator());
      args.directOperatorMode  = (tonemappingMode() == TonemappingMode::Direct) ? 1u : 0u;
      // Hable / AgX / Lottes parameter fields are populated in commits 3-5.
    }

    void populateLocalTonemapOperatorArgs(ToneMappingFinalCombineArgs& args) {
      args.tonemapOperator     = static_cast<uint32_t>(tonemapOperator());
      args.directOperatorMode  = (tonemappingMode() == TonemappingMode::Direct) ? 1u : 0u;
    }

    void showTonemapOperatorUI() {
      // Commit 2: only ACES operators are selectable; later commits extend the combo.
      const char* k_operatorItems = "None\0ACES\0ACES (Legacy)\0\0";

      int current = static_cast<int>(tonemapOperator());
      if (ImGui::Combo("Tonemapping Operator", &current, k_operatorItems)) {
        // Clamp to the set of operators enabled at this commit.
        if (current < 0 || current > 2) current = 0;
        tonemapOperatorObject().setDeferred(static_cast<TonemapOperator>(current));
      }

      // Direct mode toggle is added in commit 3.
    }

    void showLocalTonemapOperatorUI() {
      // Local panel intentionally omits Direct-mode selector (global-only).
      const char* k_operatorItems = "None\0ACES\0ACES (Legacy)\0\0";

      int current = static_cast<int>(tonemapOperator());
      if (ImGui::Combo("Tonemapping Operator (Local)", &current, k_operatorItems)) {
        if (current < 0 || current > 2) current = 0;
        tonemapOperatorObject().setDeferred(static_cast<TonemapOperator>(current));
      }
    }

    bool shouldSkipToneCurve() {
      return tonemappingMode() == TonemappingMode::Direct;
    }
```

Replace `tonemapOperatorObject()` / `tonemapOperator()` / `tonemappingMode()` with whatever accessor macro the RtxOption declaration generates — check the existing codebase pattern.

Add the necessary `#include` directives at the top of `rtx_fork_tonemap.cpp`:

```cpp
#include "imgui/imgui.h"
#include "rtx/pass/tonemap/tonemapping.h"
#include "rtx/pass/local_tonemap/local_tonemapping.h"
```

- [ ] **Step 3.11: Wire the hooks into upstream tonemap C++ files**

Open `src/dxvk/rtx_render/rtx_tone_mapping.cpp`. Find `DxvkToneMapping::dispatchApplyToneMapping` where it populates the shader args struct. Locate the old `pushArgs.finalizeWithACES = finalizeWithACES();` (and, if present, `pushArgs.useLegacyACES = ...`). Replace with:

```cpp
fork_hooks::populateTonemapOperatorArgs(pushArgs);
```

Find `DxvkToneMapping::showImguiSettings`. Locate the old `ImGui::Checkbox("Finalize With ACES", &finalizeWithACESObject());` and replace with:

```cpp
fork_hooks::showTonemapOperatorUI();
```

Add `#include "rtx_fork_hooks.h"` at the top of the file if not already present.

Repeat the same pattern in `src/dxvk/rtx_render/rtx_local_tone_mapping.cpp` using `populateLocalTonemapOperatorArgs` and `showLocalTonemapOperatorUI`.

Open `src/dxvk/imgui/dxvk_imgui.cpp`. If this file renders the tonemap ImGui panel directly (as opposed to `DxvkToneMapping::showImguiSettings` handling it), find the old ACES checkbox there and replace with a `fork_hooks::showTonemapOperatorUI()` call. Otherwise leave this file alone — the change lives inside the DxvkToneMapping method.

- [ ] **Step 3.12: Finalize `docs/fork-touchpoints.md` entries**

Open `docs/fork-touchpoints.md`. For each entry marked `[pending commit 2]` that this commit completes, convert it to the final format used elsewhere in the file:

```markdown
- **Hook** at `<upstream-symbol>` (<position>) → `fork_hooks::<hook-symbol>` in `rtx_fork_tonemap.cpp`
  *<one-line intent>*
```

or

```markdown
- **Inline tweak** at <location> — <what> for <purpose>.
  *<one-line intent>*
```

Remove all `[pending commit 2]` markers from entries this commit lands. Leave `[pending commit 3]`, `[pending commit 4]`, `[pending commit 5]` markers in place for later commits.

- [ ] **Step 3.13: Build and iterate until clean**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap
powershell.exe -NoProfile -Command "& { cd '$(pwd -W)'; . .\build_common.ps1; PerformBuild -BuildFlavour release -BuildSubDir _Comp64Release -Backend ninja -EnableTracy false }"
```

Expected outcomes and how to address:

- **`static_assert(sizeof(ToneMappingApplyToneMappingArgs) == N)` fires.** Read the compiler's actual-vs-expected output, update the literal to the reported size, rebuild. Document the chosen `N` in the struct's comment.
- **Undefined reference to `tonemapOperatorObject()` / similar RtxOption accessor.** The macro form used doesn't match the project's pattern. Check `rtx_fork_light.cpp` for the established pattern and adapt.
- **Shader compile error in `fork_tonemap_operators.slangh`.** Usually an `#include` path mismatch. Compare against how `AgX.hlsl` was included in the gmod file and adjust.
- **Missing ImGui header.** Add the correct imgui include path (check `dxvk_imgui.cpp` for the project's preferred path).
- **Link error on a fork_hooks symbol.** The implementation file isn't compiled — check `meson.build`.

Iterate until build is fully clean with zero new warnings.

- [ ] **Step 3.14: Runtime smoke test — ACES default still works**

Deploy and launch Skyrim (same pattern as Task 7):

```bash
cp _Comp64Release/src/d3d9/d3d9.dll "/d/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll"
```

Launch Skyrim, load a save. With the default `rtx.tonemap.tonemapOperator = None`, confirm the scene renders. Open the dev menu (Alt+X), go to Rendering, find the new `Tonemapping Operator` combo. Select `ACES`. Confirm visual is identical to the pre-port ACES-rendered baseline.

If rendering shows a black screen, stale tone curve, or obvious color shift: escalate. The args struct swap or the dispatcher wiring has a defect.

- [ ] **Step 3.15: Commit**

```bash
git add src/dxvk/rtx_render/rtx_fork_tonemap.cpp \
        src/dxvk/rtx_render/rtx_tone_mapping.h \
        src/dxvk/rtx_render/rtx_tone_mapping.cpp \
        src/dxvk/rtx_render/rtx_local_tone_mapping.h \
        src/dxvk/rtx_render/rtx_local_tone_mapping.cpp \
        src/dxvk/rtx_render/rtx_options.h \
        src/dxvk/imgui/dxvk_imgui.cpp \
        src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h \
        src/dxvk/shaders/rtx/pass/tonemap/tonemapping.slangh \
        src/dxvk/shaders/rtx/pass/tonemap/tonemapping_apply_tonemapping.comp.slang \
        src/dxvk/shaders/rtx/pass/tonemap/fork_tonemap_operators.slangh \
        src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h \
        src/dxvk/shaders/rtx/pass/local_tonemap/final_combine.comp.slang \
        src/dxvk/shaders/rtx/pass/local_tonemap/luminance.comp.slang \
        src/dxvk/meson.build \
        docs/fork-touchpoints.md
git status
git commit -m "$(cat <<'EOF'
fork(tonemap): introduce TonemapOperator enum, route ACES via enum

Workstream 2 commit 2 of 5. Behavior-preserving refactor.

Replaces the finalizeWithACES / useLegacyACES booleans with a
TonemapOperator enum (None / ACES / ACESLegacy) and routes ACES
rendering through a fork-owned dispatcher in the new shader header
fork_tonemap_operators.slangh. The global and local tonemapper args
structs preserve their byte sizes via pad-slot trick; static_assert
added to lock the sizes in.

ACES default renders identically to pre-refactor. None (dynamic curve
only) and ACESLegacy (gmod's legacy branch) are now selectable via the
new Tonemapping Operator ImGui combo.

Refs: gmod acfaa6ab (operator enum + Hable — Hable lands in commit 3).

Upstream files touched (all indexed in docs/fork-touchpoints.md):
- rtx_tone_mapping.{h,cpp}, rtx_local_tone_mapping.{h,cpp},
  rtx_options.h, dxvk_imgui.cpp, rtx_fork_hooks.h
- Shaders: tonemapping.{h,slangh}, tonemapping_apply_tonemapping,
  local_tonemapping.h, final_combine, luminance
- meson.build
EOF
)"
git log -1 --format=full
```

Expected: single commit. `Kim2091` author. No Co-Authored-By trailer.

---

## Task 4: Commit 3 — Hable Filmic operator + Direct mode + sliders

Purely additive on top of commit 2's enum.

**Files:**
- Modify: `src/dxvk/rtx_render/rtx_fork_tonemap.cpp` — add Hable RtxOptions, extend hooks.
- Modify: `src/dxvk/rtx_render/rtx_fork_tonemap.h` — (no change needed — enum already has HableFilmic).
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h` — add `tonemapOperatorHableFilmic = 3` constant; add Hable param fields to args struct (using pad slots, preserving size — adjust `static_assert`).
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/fork_tonemap_operators.slangh` — add `hable()` function + HableFilmic dispatcher branch.
- Modify: `src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h` — same struct extension as global.
- Modify: `src/dxvk/rtx_render/rtx_context.cpp` — Direct-mode dispatch branch.
- Modify: `docs/fork-touchpoints.md` — finalize `[pending commit 3]` entries.

- [ ] **Step 4.1: Add Hable parameter RtxOptions**

Open `src/dxvk/rtx_render/rtx_fork_tonemap.cpp`. Add (above the hook implementations):

```cpp
// Hable Filmic operator parameters. Defaults match gmod baad5e79's
// Half-Life: Alyx reference values (W=4.0, exposureBias=2.0) rather than
// Uncharted 2's original (W=11.2). HLA values produce a less-aggressive
// shoulder that most modern titles prefer.
RTX_OPTION("rtx.tonemap", float, hableExposureBias,     2.00f,  "Hable Filmic: pre-operator exposure multiplier.");
RTX_OPTION("rtx.tonemap", float, hableShoulderStrength, 0.15f,  "Hable Filmic: A — shoulder strength.");
RTX_OPTION("rtx.tonemap", float, hableLinearStrength,   0.50f,  "Hable Filmic: B — linear strength.");
RTX_OPTION("rtx.tonemap", float, hableLinearAngle,      0.10f,  "Hable Filmic: C — linear angle.");
RTX_OPTION("rtx.tonemap", float, hableToeStrength,      0.20f,  "Hable Filmic: D — toe strength.");
RTX_OPTION("rtx.tonemap", float, hableToeNumerator,     0.02f,  "Hable Filmic: E — toe numerator.");
RTX_OPTION("rtx.tonemap", float, hableToeDenominator,   0.30f,  "Hable Filmic: F — toe denominator.");
RTX_OPTION("rtx.tonemap", float, hableWhitePoint,       4.00f,  "Hable Filmic: W — linear-scene white point (HLA default 4.0; Uncharted 2 used 11.2).");
```

- [ ] **Step 4.2: Extend shader args struct with Hable fields**

Open `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h`. Add `tonemapOperatorHableFilmic = 3` alongside the existing constants.

Extend `ToneMappingApplyToneMappingArgs` with Hable fields. Reuse the `pad1` slot and appended space. The layout after this step:

```c
struct ToneMappingApplyToneMappingArgs {
  // ... fields unchanged from commit 2 up through `pad1` ...

  // Hable Filmic parameters (op == tonemapOperatorHableFilmic).
  // Sharing these slots with Lottes in commit 5 (mutually exclusive operators).
  float hableExposureBias;
  float hableShoulderStrength;  // A
  float hableLinearStrength;    // B
  float hableLinearAngle;       // C

  float hableToeStrength;       // D
  float hableToeNumerator;      // E
  float hableToeDenominator;    // F
  float hableWhitePoint;        // W
};
```

Update the `static_assert` to the new expected size (previous size + 32 bytes). Let the compiler verify the exact number via the assert failure if the literal is wrong, then correct.

Apply the same extension to the local-tonemap args struct in `local_tonemapping.h`.

- [ ] **Step 4.3: Add `hable()` function and dispatcher branch in `fork_tonemap_operators.slangh`**

Add to the shader header (inside the same `#ifndef`/`#define` guard):

```c
// Hable Filmic tonemapping core (Uncharted 2 formulation).
// Reference: http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 hablePartial(float3 x, float A, float B, float C, float D, float E, float F)
{
  return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 hableFilmicToneMapper(float3 color,
                             float exposureBias,
                             float A, float B, float C, float D, float E, float F,
                             float W)
{
  float3 curr = hablePartial(exposureBias * color, A, B, C, D, E, F);
  float3 whiteScale = float3(1.0f) / hablePartial(float3(W), A, B, C, D, E, F);
  return curr * whiteScale;
}
```

Extend the dispatcher. Replace the prior body of `applyTonemapOperator` (from Step 3.3) with:

```c
float3 applyTonemapOperator(uint op, float3 color, bool suppressBlackLevelClamp,
                            ToneMappingApplyToneMappingArgs args)
{
  if (op == tonemapOperatorACES)
    return ACESFilm(color, /*useLegacyACES=*/false, suppressBlackLevelClamp);
  if (op == tonemapOperatorACESLegacy)
    return ACESFilm(color, /*useLegacyACES=*/true, suppressBlackLevelClamp);
  if (op == tonemapOperatorHableFilmic)
    return hableFilmicToneMapper(color,
                                 args.hableExposureBias,
                                 args.hableShoulderStrength,
                                 args.hableLinearStrength,
                                 args.hableLinearAngle,
                                 args.hableToeStrength,
                                 args.hableToeNumerator,
                                 args.hableToeDenominator,
                                 args.hableWhitePoint);
  return color;
}
```

Call sites (tonemapping_apply_tonemapping.comp.slang, final_combine.comp.slang) now pass the args struct as the final parameter. Update those call sites to pass `cb` (or the local args binding) through.

- [ ] **Step 4.4: Add Direct-mode dispatch branch in `rtx_context.cpp`**

Open `src/dxvk/rtx_render/rtx_context.cpp`. Find where `DxvkToneMapping::dispatch` is invoked from the context's frame-graph. Add a check before the invocation:

```cpp
if (!fork_hooks::shouldSkipToneCurve()) {
  // existing tonemap dispatch path
  m_toneMapping->dispatch(...);
} else {
  // Direct mode: skip histogram + curve + local pyramid, apply operator alone.
  // The global tonemapper's dispatchApplyToneMapping still runs because it
  // produces the final output from the exposure-adjusted input; shouldSkipToneCurve
  // guards the upstream passes that feed into it.
  m_toneMapping->dispatchApplyOperatorOnly(...);   // new entry point if needed
}
```

The exact shape depends on how the port structures the dispatch. The safest minimal change: inside `DxvkToneMapping::dispatch`, at the top, check `fork_hooks::shouldSkipToneCurve()` and early-skip the histogram + tone-curve + (for local) pyramid sub-dispatches, while still calling `dispatchApplyToneMapping` at the end. This keeps the public entry-point stable.

If the port's dispatch already has a mode-switch on `tonemappingMode`, augment it rather than duplicating the logic.

- [ ] **Step 4.5: Extend `populateTonemapOperatorArgs` to write Hable params**

In `rtx_fork_tonemap.cpp`, extend the hook:

```cpp
    void populateTonemapOperatorArgs(ToneMappingApplyToneMappingArgs& args) {
      args.tonemapOperator     = static_cast<uint32_t>(tonemapOperator());
      args.directOperatorMode  = (tonemappingMode() == TonemappingMode::Direct) ? 1u : 0u;

      // Hable Filmic parameters — written unconditionally; shader gates on the
      // operator enum. Cheap: 8 float writes.
      args.hableExposureBias     = hableExposureBias();
      args.hableShoulderStrength = hableShoulderStrength();
      args.hableLinearStrength   = hableLinearStrength();
      args.hableLinearAngle      = hableLinearAngle();
      args.hableToeStrength      = hableToeStrength();
      args.hableToeNumerator     = hableToeNumerator();
      args.hableToeDenominator   = hableToeDenominator();
      args.hableWhitePoint       = hableWhitePoint();
    }
```

Apply the same change to `populateLocalTonemapOperatorArgs`.

- [ ] **Step 4.6: Extend the UI hook with Hable sliders + Direct-mode toggle**

Replace `showTonemapOperatorUI` body with:

```cpp
    void showTonemapOperatorUI() {
      const char* k_operatorItems = "None\0ACES\0ACES (Legacy)\0Hable Filmic\0\0";

      int current = static_cast<int>(tonemapOperator());
      if (ImGui::Combo("Tonemapping Operator", &current, k_operatorItems)) {
        if (current < 0 || current > 3) current = 0;
        tonemapOperatorObject().setDeferred(static_cast<TonemapOperator>(current));
      }

      // Direct mode toggle (applies regardless of operator).
      bool directMode = tonemappingMode() == TonemappingMode::Direct;
      if (ImGui::Checkbox("Direct Mode (operator only, no tone curve)", &directMode)) {
        tonemappingModeObject().setDeferred(
          directMode ? TonemappingMode::Direct : TonemappingMode::Global);
      }

      // Per-operator parameter panels (shown only when the operator is selected).
      if (tonemapOperator() == TonemapOperator::HableFilmic) {
        ImGui::Indent();
        ImGui::Text("Hable Filmic Parameters:");
        ImGui::DragFloat("exposureBias",  &hableExposureBiasObject(),     0.05f, 0.0f,  8.0f);
        ImGui::DragFloat("A (shoulder)",  &hableShoulderStrengthObject(), 0.01f, 0.0f,  1.0f);
        ImGui::DragFloat("B (linear)",    &hableLinearStrengthObject(),   0.01f, 0.0f,  1.0f);
        ImGui::DragFloat("C (linAngle)",  &hableLinearAngleObject(),      0.01f, 0.0f,  1.0f);
        ImGui::DragFloat("D (toe)",       &hableToeStrengthObject(),      0.01f, 0.0f,  1.0f);
        ImGui::DragFloat("E (toeNum)",    &hableToeNumeratorObject(),     0.01f, 0.0f,  1.0f);
        ImGui::DragFloat("F (toeDenom)",  &hableToeDenominatorObject(),   0.01f, 0.0f,  1.0f);
        ImGui::DragFloat("W (white)",     &hableWhitePointObject(),       0.10f, 0.1f, 32.0f);
        ImGui::Unindent();
      }
    }
```

Apply the equivalent (without Direct-mode toggle — that's global-only) to `showLocalTonemapOperatorUI`.

- [ ] **Step 4.7: Finalize `[pending commit 3]` fork-touchpoint entries**

Open `docs/fork-touchpoints.md`. Convert any entries marked `[pending commit 3]` (chiefly `rtx_context.cpp`'s Direct-mode branch) to final form.

- [ ] **Step 4.8: Build and runtime-verify**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap
powershell.exe -NoProfile -Command "& { cd '$(pwd -W)'; . .\build_common.ps1; PerformBuild -BuildFlavour release -BuildSubDir _Comp64Release -Backend ninja -EnableTracy false }"
```

Expected: clean build. `static_assert` on the grown struct size passes (update the size literal if the compiler reports the actual size — document the new number).

Deploy and launch:

```bash
cp _Comp64Release/src/d3d9/d3d9.dll "/d/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll"
```

Load a save, open dev menu. Select `Hable Filmic`. Verify:
- Image renders with a softer-highlight look relative to ACES (Hable's signature).
- Slider changes to `A`–`F`/`W`/`exposureBias` produce visually-tracking effects.
- Toggle `Direct Mode`. With Hable + Direct active, the dynamic tone curve is bypassed; the image may look noticeably different (operator-alone behavior).

If any slider has no visible effect, the args struct field isn't being read by the shader — check field-ordering in `tonemapping.h` vs. the dispatcher's access pattern.

- [ ] **Step 4.9: Commit**

```bash
git add [all modified files + docs/fork-touchpoints.md]
git status
git commit -m "$(cat <<'EOF'
fork(tonemap): add Hable Filmic operator + Direct mode + sliders

Workstream 2 commit 3 of 5. Purely additive on top of the enum refactor.

Adds the HableFilmic operator (Uncharted 2 filmic formulation, defaults
from Half-Life: Alyx — W=4.0, exposureBias=2.0), its 8 parameter
RtxOptions, and dev-menu sliders visible when HableFilmic is selected.
Adds the Direct tonemapping mode (operator-only, skips dynamic curve +
local pyramid) with its ImGui toggle in the global panel.

Extends the shader args struct with 8 float fields for Hable params;
static_assert updated to the new struct size. Lottes will share these
slots in commit 5.

Refs: gmod acfaa6ab (Hable function + operator routing), baad5e79
(Direct mode + Hable parameter RtxOptions).
EOF
)"
git log -1 --format=full
```

---

## Task 5: Commit 4 — AgX operator

**Files:**
- Create: `src/dxvk/shaders/rtx/pass/tonemap/AgX.hlsl` — port byte-for-byte from gmod `f3501d46`.
- Modify: `src/dxvk/rtx_render/rtx_fork_tonemap.cpp` — AgX RtxOptions + hook extensions.
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h` — add `tonemapOperatorAgX = 4`; extend args struct with AgX fields.
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/fork_tonemap_operators.slangh` — include `AgX.hlsl`; add AgX dispatcher branch.
- Modify: `src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h` — same struct extension.
- Modify: `src/dxvk/meson.build` — register `AgX.hlsl`.
- Modify: `docs/fork-touchpoints.md` — finalize `[pending commit 4]` entries (extended `tonemapping.h` section).

- [ ] **Step 5.1: Copy `AgX.hlsl` from gmod**

```bash
cp /c/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/src/dxvk/shaders/rtx/pass/tonemap/AgX.hlsl \
   c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap/src/dxvk/shaders/rtx/pass/tonemap/AgX.hlsl
```

Verify:

```bash
diff /c/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/src/dxvk/shaders/rtx/pass/tonemap/AgX.hlsl \
     src/dxvk/shaders/rtx/pass/tonemap/AgX.hlsl
```

Expected: empty diff.

- [ ] **Step 5.2: Add AgX RtxOptions**

Append to `rtx_fork_tonemap.cpp`:

```cpp
// AgX tonemapper parameters (only used when tonemapOperator == AgX).
RTX_OPTION("rtx.tonemap", float, agxGamma,          2.0f, "AgX gamma / contrast control. Lower values increase contrast. Range [0.5, 3.0].");
RTX_OPTION("rtx.tonemap", float, agxSaturation,     1.1f, "AgX saturation multiplier. Higher values increase color saturation. Range [0.5, 2.0].");
RTX_OPTION("rtx.tonemap", float, agxExposureOffset, 0.0f, "AgX exposure offset (EV stops). Positive values brighten. Range [-2.0, 2.0].");
RTX_OPTION("rtx.tonemap", int,   agxLook,           0,    "AgX look preset: 0 = None, 1 = Punchy, 2 = Golden, 3 = Greyscale.");
RTX_OPTION("rtx.tonemap", float, agxContrast,       1.0f, "AgX contrast adjustment. Range [0.5, 2.0].");
RTX_OPTION("rtx.tonemap", float, agxSlope,          1.0f, "AgX slope adjustment (highlight rolloff). Range [0.5, 2.0].");
RTX_OPTION("rtx.tonemap", float, agxPower,          1.0f, "AgX power adjustment (midtone response). Range [0.5, 2.0].");
```

- [ ] **Step 5.3: Extend shader args struct with AgX fields**

Open `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h`. Add `tonemapOperatorAgX = 4` constant.

Extend `ToneMappingApplyToneMappingArgs` by appending AgX fields AFTER the Hable block:

```c
  // AgX parameters (op == tonemapOperatorAgX).
  float agxGamma;
  float agxSaturation;
  float agxExposureOffset;
  uint  agxLook;

  float agxContrast;
  float agxSlope;
  float agxPower;
  float agxPad;   // Reserved for struct alignment.
```

Update `static_assert` to new size.

Apply same to `local_tonemapping.h`.

- [ ] **Step 5.4: Register `AgX.hlsl` in meson**

Add `AgX.hlsl` to the appropriate shader list in `src/dxvk/meson.build`, in the same block as the other tonemap shaders.

- [ ] **Step 5.5: Include AgX in dispatcher + add branch**

Extend `fork_tonemap_operators.slangh`. Add near the top (inside the header guard, after `#include "tonemapping.h"`):

```c
#include "AgX.hlsl"
```

Extend the dispatcher's body:

```c
  if (op == tonemapOperatorAgX)
    return AgXToneMapping(color,
                          args.agxGamma, args.agxSaturation, args.agxExposureOffset,
                          args.agxLook,
                          args.agxContrast, args.agxSlope, args.agxPower);
```

The exact `AgXToneMapping` function signature comes from gmod's `AgX.hlsl` — match it exactly.

- [ ] **Step 5.6: Extend `populateTonemapOperatorArgs` to write AgX params**

```cpp
  args.agxGamma          = agxGamma();
  args.agxSaturation     = agxSaturation();
  args.agxExposureOffset = agxExposureOffset();
  args.agxLook           = static_cast<uint32_t>(agxLook());
  args.agxContrast       = agxContrast();
  args.agxSlope          = agxSlope();
  args.agxPower          = agxPower();
```

Apply to local variant too.

- [ ] **Step 5.7: Extend UI with AgX selector + sliders**

Update the combo items:

```cpp
const char* k_operatorItems = "None\0ACES\0ACES (Legacy)\0Hable Filmic\0AgX\0\0";
```

And after the Hable slider block:

```cpp
if (tonemapOperator() == TonemapOperator::AgX) {
  ImGui::Indent();
  ImGui::Text("AgX Parameters:");
  ImGui::DragFloat("Gamma",           &agxGammaObject(),          0.01f, 0.5f,  3.0f);
  ImGui::DragFloat("Saturation",      &agxSaturationObject(),     0.01f, 0.5f,  2.0f);
  ImGui::DragFloat("Exposure Offset", &agxExposureOffsetObject(), 0.05f, -2.0f, 2.0f);
  ImGui::Combo(    "Look",            &agxLookObject(), "None\0Punchy\0Golden\0Greyscale\0\0");
  ImGui::DragFloat("Contrast",        &agxContrastObject(),       0.01f, 0.5f,  2.0f);
  ImGui::DragFloat("Slope",           &agxSlopeObject(),          0.01f, 0.5f,  2.0f);
  ImGui::DragFloat("Power",           &agxPowerObject(),          0.01f, 0.5f,  2.0f);
  ImGui::Unindent();
}
```

Mirror in `showLocalTonemapOperatorUI`.

- [ ] **Step 5.8: Finalize `[pending commit 4]` fork-touchpoint entries**

In `docs/fork-touchpoints.md`, for the `tonemapping.h` section, add a second bullet noting the AgX-field extension. Similarly for `local_tonemapping.h` if touched.

- [ ] **Step 5.9: Build and runtime-verify**

Build as in Step 4.8. Deploy, launch, select AgX in the combo. Verify:
- Image renders with AgX's characteristic look (reduced saturation in highlights, cleaner midtones).
- Look combo switches between Punchy / Golden / Greyscale and each produces a visible difference.
- Numeric sliders track.

- [ ] **Step 5.10: Commit**

```bash
git add [all modified + new files]
git commit -m "$(cat <<'EOF'
fork(tonemap): add AgX operator

Workstream 2 commit 4 of 5. Additive on top of commit 3.

Adds the AgX tonemapper as a new fork-owned shader (AgX.hlsl, ported
byte-for-byte from gmod f3501d46) plus 7 AgX parameter RtxOptions
(gamma, saturation, exposureOffset, look, contrast, slope, power) and
their ImGui sliders. Extends the shader args struct with the AgX
parameter fields; static_assert updated.

Refs: gmod f3501d46.
EOF
)"
git log -1 --format=full
```

---

## Task 6: Commit 5 — Lottes 2016 operator

**Files:**
- Create: `src/dxvk/shaders/rtx/pass/tonemap/Lottes.hlsl` — port byte-for-byte from gmod `cdf2c723`.
- Modify: `src/dxvk/rtx_render/rtx_fork_tonemap.cpp` — Lottes RtxOptions + hook extensions.
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h` — add `tonemapOperatorLottes = 5`; NOTE Lottes shares Hable's parameter slots.
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/fork_tonemap_operators.slangh` — include `Lottes.hlsl`; add Lottes branch.
- Modify: `src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h` — (no struct change — slots shared).
- Modify: `src/dxvk/meson.build` — register `Lottes.hlsl`.
- Modify: `docs/fork-touchpoints.md` — finalize `[pending commit 5]` entries.

- [ ] **Step 6.1: Copy `Lottes.hlsl` from gmod**

```bash
cp /c/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/src/dxvk/shaders/rtx/pass/tonemap/Lottes.hlsl \
   c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap/src/dxvk/shaders/rtx/pass/tonemap/Lottes.hlsl
```

Verify byte-for-byte match against gmod.

- [ ] **Step 6.2: Add Lottes RtxOptions**

Append to `rtx_fork_tonemap.cpp`:

```cpp
// Lottes 2016 tonemapper parameters (only used when tonemapOperator == Lottes).
// Shares struct slots with Hable — operators are mutually exclusive.
RTX_OPTION("rtx.tonemap", float, lottesHdrMax,   16.0f, "Lottes: peak HDR white value. Higher = more highlight detail preserved. Range [1.0, 64.0].");
RTX_OPTION("rtx.tonemap", float, lottesContrast,  2.0f, "Lottes: contrast control (also drives saturation / crosstalk). Range [1.0, 3.0].");
RTX_OPTION("rtx.tonemap", float, lottesShoulder,  1.0f, "Lottes: shoulder strength (highlight compression). Range [0.5, 2.0].");
RTX_OPTION("rtx.tonemap", float, lottesMidIn,    0.18f, "Lottes: mid-grey input (scene linear). Range [0.01, 1.0].");
RTX_OPTION("rtx.tonemap", float, lottesMidOut,   0.18f, "Lottes: mid-grey output. Range [0.01, 1.0].");
```

- [ ] **Step 6.3: Add `tonemapOperatorLottes` constant; document slot sharing**

Open `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h`. Add:

```c
static const uint32_t tonemapOperatorLottes = 5;
```

Above the Hable-param-field block in the struct, update the comment:

```c
  // Hable Filmic parameters (op == tonemapOperatorHableFilmic) and
  // Lottes 2016 parameters (op == tonemapOperatorLottes) share these
  // slots — the two operators are mutually exclusive. Parameter mapping
  // for Lottes:
  //   hableExposureBias     -> lottesHdrMax
  //   hableShoulderStrength -> lottesContrast
  //   hableLinearStrength   -> lottesShoulder
  //   hableLinearAngle      -> lottesMidIn
  //   hableToeStrength      -> lottesMidOut
  //   (hableToeNumerator/Denominator/WhitePoint unused by Lottes)
```

No struct-size change; the `static_assert` stays at its post-commit-3 value.

- [ ] **Step 6.4: Register `Lottes.hlsl` in meson**

Append to `src/dxvk/meson.build` alongside `AgX.hlsl`.

- [ ] **Step 6.5: Include Lottes in dispatcher + add branch**

Extend `fork_tonemap_operators.slangh`:

```c
#include "Lottes.hlsl"
```

Extend dispatcher:

```c
  if (op == tonemapOperatorLottes)
    return LottesToneMapping(color,
                             /*hdrMax=*/   args.hableExposureBias,
                             /*contrast=*/ args.hableShoulderStrength,
                             /*shoulder=*/ args.hableLinearStrength,
                             /*midIn=*/    args.hableLinearAngle,
                             /*midOut=*/   args.hableToeStrength);
```

Verify `LottesToneMapping`'s signature from gmod's `Lottes.hlsl` and adapt if different.

- [ ] **Step 6.6: Extend `populateTonemapOperatorArgs` — overwrite Hable slots with Lottes when Lottes is selected**

```cpp
    void populateTonemapOperatorArgs(ToneMappingApplyToneMappingArgs& args) {
      args.tonemapOperator     = static_cast<uint32_t>(tonemapOperator());
      args.directOperatorMode  = (tonemappingMode() == TonemappingMode::Direct) ? 1u : 0u;

      if (tonemapOperator() == TonemapOperator::Lottes) {
        // Map Lottes params into the Hable-shared slots.
        args.hableExposureBias     = lottesHdrMax();
        args.hableShoulderStrength = lottesContrast();
        args.hableLinearStrength   = lottesShoulder();
        args.hableLinearAngle      = lottesMidIn();
        args.hableToeStrength      = lottesMidOut();
        args.hableToeNumerator     = 0.0f;
        args.hableToeDenominator   = 0.0f;
        args.hableWhitePoint       = 0.0f;
      } else {
        // Hable (or other operator — Hable-slot values are still written so
        // HableFilmic renders correctly).
        args.hableExposureBias     = hableExposureBias();
        args.hableShoulderStrength = hableShoulderStrength();
        args.hableLinearStrength   = hableLinearStrength();
        args.hableLinearAngle      = hableLinearAngle();
        args.hableToeStrength      = hableToeStrength();
        args.hableToeNumerator     = hableToeNumerator();
        args.hableToeDenominator   = hableToeDenominator();
        args.hableWhitePoint       = hableWhitePoint();
      }

      // AgX params (unchanged from commit 4)
      args.agxGamma          = agxGamma();
      args.agxSaturation     = agxSaturation();
      args.agxExposureOffset = agxExposureOffset();
      args.agxLook           = static_cast<uint32_t>(agxLook());
      args.agxContrast       = agxContrast();
      args.agxSlope          = agxSlope();
      args.agxPower          = agxPower();
    }
```

Apply the same branching to `populateLocalTonemapOperatorArgs`.

- [ ] **Step 6.7: Extend UI with Lottes selector + sliders**

Update the combo items:

```cpp
const char* k_operatorItems = "None\0ACES\0ACES (Legacy)\0Hable Filmic\0AgX\0Lottes 2016\0\0";
```

After the AgX slider block:

```cpp
if (tonemapOperator() == TonemapOperator::Lottes) {
  ImGui::Indent();
  ImGui::Text("Lottes 2016 Parameters:");
  ImGui::DragFloat("hdrMax",   &lottesHdrMaxObject(),   0.10f, 1.0f, 64.0f);
  ImGui::DragFloat("contrast", &lottesContrastObject(), 0.01f, 1.0f,  3.0f);
  ImGui::DragFloat("shoulder", &lottesShoulderObject(), 0.01f, 0.5f,  2.0f);
  ImGui::DragFloat("midIn",    &lottesMidInObject(),    0.01f, 0.01f, 1.0f);
  ImGui::DragFloat("midOut",   &lottesMidOutObject(),   0.01f, 0.01f, 1.0f);
  ImGui::Unindent();
}
```

Mirror in `showLocalTonemapOperatorUI`.

- [ ] **Step 6.8: Finalize `[pending commit 5]` fork-touchpoint entries**

In `docs/fork-touchpoints.md`, complete any outstanding entries.

Run a final sweep to confirm zero `[pending` markers remain:

```bash
grep -n '\[pending' docs/fork-touchpoints.md || echo "no pending entries"
```

Expected: `no pending entries`.

- [ ] **Step 6.9: Build and runtime-verify**

Build. Struct size should be unchanged from post-commit-4 (the `static_assert` literal stays the same — Lottes shares slots).

Deploy. Select Lottes 2016. Verify distinctive Lottes signature (aggressive highlight compression, punchy midtones). Toggle through all 5 operators and confirm each renders differently and correctly. Confirm Direct-mode toggle still works with all operators.

- [ ] **Step 6.10: Commit**

```bash
git add [all modified + new files]
git commit -m "$(cat <<'EOF'
fork(tonemap): add Lottes 2016 operator

Workstream 2 commit 5 of 5, completing the tonemap operator port.

Adds Lottes 2016 as a new fork-owned shader (Lottes.hlsl, ported
byte-for-byte from gmod cdf2c723) plus 5 Lottes parameter RtxOptions
(hdrMax, contrast, shoulder, midIn, midOut) and their ImGui sliders.

Lottes and Hable Filmic share the same shader-args slots — the two
operators are mutually exclusive, so the struct size does not grow.
The populateTonemapOperatorArgs hook branches on the selected operator
to write the correct parameter set into the shared slots.

static_assert on ToneMappingApplyToneMappingArgs size is preserved
from commit 3 (no further growth).

All five operators (None, ACES, ACES Legacy, Hable Filmic, AgX,
Lottes 2016) are now selectable and functional in both the global
and local tonemap paths. Direct mode works with all.

Refs: gmod cdf2c723.
EOF
)"
git log -1 --format=full
```

---

## Task 7: Runtime validation + merge readiness

**Files:**
- Copy: built `d3d9.dll` → `D:/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll` (or whatever path the build script's deploy hop uses).

- [ ] **Step 7.1: Clean release build from tip**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap
powershell.exe -NoProfile -Command "& { cd '$(pwd -W)'; . .\build_common.ps1; PerformBuild -BuildFlavour release -BuildSubDir _Comp64Release -Backend ninja -EnableTracy false }"
```

Expected: clean build, zero new warnings.

- [ ] **Step 7.2: Deploy to Skyrim**

```bash
cp _Comp64Release/src/d3d9/d3d9.dll "/d/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll"
ls -la "/d/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll"
```

Expected: fresh mtime, release-build size (~85 MB).

- [ ] **Step 7.3: Runtime test — each operator renders**

Launch Skyrim, load an outdoor save (Solitude or anywhere with clear sky and varied lighting). For each of the following combinations, confirm the scene renders without artifacts:

| Operator | Direct mode | Expected behavior |
|---|---|---|
| None | off | Dynamic tone curve only (upstream-NVIDIA default). |
| ACES | off | Default ACES rendering (matches pre-port baseline). |
| ACES Legacy | off | gmod's legacy ACES branch (slightly different tonal response than ACES). |
| Hable Filmic | off | Softer shoulder, warmer midtones. |
| AgX | off | Reduced highlight saturation, more filmic midtones; cycle through looks. |
| Lottes 2016 | off | Aggressive highlight compression, punchy midtones. |
| Hable Filmic | on  | Same Hable look but with no dynamic curve pre-pass — typically brighter. |
| AgX | on  | AgX-only response. |
| Lottes 2016 | on  | Lottes-only response. |

Per-operator slider changes produce visually-tracking effects.

- [ ] **Step 7.4: Regression check — W1 + W5 functionality intact**

Exercise features from prior workstreams:

- Plugin-submitted Unity geometry still renders (W1).
- Skinned meshes animate (W1).
- Physical sky (Hillaire atmosphere) still renders when `rtx.skyMode = PhysicalAtmosphere` (W5).
- Dev menu opens (Alt+X), all tabs navigable.

If any prior-workstream feature regresses, STOP. The enum refactor or hook wiring may have damaged shader-args layout for another pass.

- [ ] **Step 7.5: Log baselines**

```bash
tail -n 200 "C:/Users/mystery/Documents/My Games/Skyrim Special Edition/SKSE/SkyrimRemixPlugin.log" | \
  grep -E "warn|error" | head -20
```

Expected: zero new warnings/errors beyond pre-port baseline. The one pre-existing integrated-GPU skip warning is ignored.

A new warning of shape `SetConfigVariable failed for key 'rtx.tonemap.finalizeWithACES' ... key not registered` is **expected** if any consumer still tries to set that key. Log it as a known harmless side-effect; do NOT treat it as a failure.

- [ ] **Step 7.6: Record final state**

```bash
cd c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap
git log --oneline unity-fork-touchpoint-refactor..HEAD
```

Expected: exactly 5 commits on top of the touchpoint-refactor tip, authored Kim2091 only, no Co-Authored-By trailers.

---

## Workstream merge + push readiness (not part of task execution — documented for later)

When this workstream is validated, the user decides when to push and whether to open a PR. Per the standing rule:

- **Push to `kim2091` remote only when the user has approved.**
- **No auto-PR.** User opens the PR manually or requests it explicitly.

Merge into the unity integration branch is out of scope for this plan; it belongs to a separate integration-merge task.

---

## Self-Review

**1. Spec coverage:**

| Spec requirement | Plan task(s) |
|---|---|
| TonemapOperator enum (None/ACES/ACESLegacy/HableFilmic/AgX/Lottes) | Task 2 Step 2.1 (enum), Task 3 Step 3.3 (shader constants), Task 3 Step 3.9 (RtxOption), Task 4 (HableFilmic), Task 5 (AgX), Task 6 (Lottes) |
| Hable Filmic operator + params | Task 4 |
| AgX operator + look/contrast/offset/saturation | Task 5 |
| Lottes 2016 operator + params | Task 6 |
| Direct mode | Task 4 (hook impl + rtx_context dispatch + toggle) |
| Dev-menu UI (combo + per-operator sliders + Direct toggle) | Task 3 Step 3.10 (combo skeleton), Task 4 Step 4.6 (Hable + Direct), Task 5 Step 5.7 (AgX), Task 6 Step 6.7 (Lottes) |
| Fork-owned files | Task 2 (scaffold), Task 5 (AgX.hlsl), Task 6 (Lottes.hlsl), Task 3 (fork_tonemap_operators.slangh) |
| Thin hooks in upstream | Task 3 Step 3.11, Task 4 Step 4.4 (rtx_context Direct mode) |
| Fridge-list entries in the same commit | Task 2 Step 2.5 (seeding), Task 3 Step 3.12, Task 4 Step 4.7, Task 5 Step 5.8, Task 6 Step 6.8 |
| Struct-size preservation via pad-slot trick | Task 3 Step 3.1c (+static_assert), Task 4 Step 4.2 (extension + assert), Task 5 Step 5.3, Task 6 Step 6.3 (slot sharing) |
| Config-key removal (finalizeWithACES, useLegacyACES) | Task 3 Step 3.8 |
| Both global and local tonemap paths | Each task addresses both (global + local struct swaps, both hooks, both UI renders) |
| No HDR plumbing touched | Plan does not touch `HDRProcessingArgs` / swapchain / format code |
| No AI co-author trailers | Every commit uses `$(cat <<'EOF'...EOF)` heredoc with no trailer |
| Author Kim2091 | Inherited from git config; verified via `git log -1 --format=full` after each commit |

All spec elements covered.

**2. Placeholder scan:**

- No `TODO`, `TBD`, `fill in later`, or "handle edge cases" placeholders.
- Bracketed placeholders like `[all modified files + docs/fork-touchpoints.md]` in the final `git add` commands are intentional — the executor lists the files they actually touched in that task. The list is reconstructable from the step-by-step file changes above.
- The `static_assert` size literal (`96` in Step 3.1c) is explicitly flagged as "verify from compiler error if wrong"; the correct value is extracted from the build failure. This is a known-placeholder pattern with a clear resolution path, not ambiguity.

**3. Type consistency:**

- `TonemapOperator` enum values are consistent across `rtx_fork_tonemap.h` (Task 2) and `tonemapping.h` shader constants (Tasks 3–6): `None=0, ACES=1, ACESLegacy=2, HableFilmic=3, AgX=4, Lottes=5`.
- Hook signatures match across declarations (Task 2.3) and implementations (Tasks 3–6): `populateTonemapOperatorArgs`, `populateLocalTonemapOperatorArgs`, `showTonemapOperatorUI`, `showLocalTonemapOperatorUI`, `shouldSkipToneCurve`.
- RtxOption names are consistent: `tonemapOperator`, `tonemappingMode`, `hableExposureBias`, `hableShoulderStrength` (etc.), `agxGamma` (etc.), `lottesHdrMax` (etc.).
- `ToneMappingApplyToneMappingArgs` / `ToneMappingFinalCombineArgs` as struct names are consistent between hook declarations (Task 2.3), shader-header edits (Task 3.1, 3.2), and hook implementations (Task 3.10, 4.5, 5.6, 6.6). If the port's local-tonemap args struct has a different name (local_tonemapping.h may use `ToneMappingLocalArgs`), the executor adjusts the hook signature at Task 2.3 to match; this is called out in Step 3.10 ("ToneMappingFinalCombineArgs or its local-path name").

---

## Execution handoff

Plan complete. Awaiting choice of execution mode (subagent-driven vs inline) before execution begins.
