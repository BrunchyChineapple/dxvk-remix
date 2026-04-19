# Unity-fork port — Workstream 2: Tonemap operators

## Context

Workstream 2 of the unity-fork port ports four `dxvk-remix-gmod` commits that extend the tonemapper with a TonemapOperator enum, Hable Filmic / AgX / Lottes 2016 operators, Direct-mode dispatch, and per-operator parameter UI. Workstream 1 (API + HW skinning) and Workstream 5 (Hillaire atmosphere) are already merged into the port; the fork-touchpoint-pattern refactor landed on top at `dfe2e43c` and is the base for this workstream.

The four source commits, in dependency order:

| Hash | Date | Summary |
|---|---|---|
| `acfaa6ab` | 2026-03-17 | Hable Filmic operator + refactor of `finalizeWithACES`/`useLegacyACES` bools into a `TonemapOperator` enum. Foundational — commits 3–5 extend this enum. |
| `baad5e79` | 2026-03-17 | Direct tonemapping mode (operator-only, no tone curve) + Hable parameter sliders. |
| `f3501d46` | 2025-06-18 | AgX tonemapper + look-mode and contrast/offset/saturation sliders. |
| `cdf2c723` | 2026-04-08 | Lottes 2016 tonemapper + parameter sliders. Preserves struct size via Hable push-constant slot aliasing. |

The port's current tonemap surface matches gmod's pre-refactor state — `finalizeWithACES` boolean + `useLegacyACES` boolean, ACES-only operator set, no Hable/AgX/Lottes anywhere. This is a clean starting point for the port.

**HDR entanglement audit:** none. The four commits touch only tonemap C++, tonemap shaders, ImGui surfaces, and add two new `.hlsl` files. They do not touch swapchain format, output color space, surface format, or any HDR-adjacent plumbing. Workstream 3 (HDR) remains orthogonal and separately scoped.

## Goal

Port the four gmod tonemap commits into the unity-fork port, shaped from day one to the fork-touchpoint pattern. The tonemap operator logic lives in fork-owned files; upstream tonemap files receive thin hook calls and small indexed inline tweaks. Every upstream-file touch is listed in `docs/fork-touchpoints.md` in the same commit that introduces it.

## In scope

- **TonemapOperator enum** (`None` / `ACES` / `ACESLegacy` / `HableFilmic` / `AgX` / `Lottes2016`) replacing the `finalizeWithACES` + `useLegacyACES` bool pair. Applied to both global (`DxvkToneMapping`) and local (`DxvkLocalToneMapping`) tonemappers.
- **Hable Filmic operator** — `hable(...)` function and per-parameter RtxOptions (`exposureBias`, `A`–`F`, `W`), defaults matching gmod's Half-Life: Alyx values.
- **AgX operator** — new `AgX.hlsl` shader file, look mode (Base / Golden / Punchy) + contrast / offset / saturation RtxOptions.
- **Lottes 2016 operator** — new `Lottes.hlsl` shader file, RtxOptions for `hdrMax` / `contrast` / `shoulder` / `midIn` / `midOut`. Shares push-constant slots with Hable since operators are mutually exclusive (preserves shader args struct size).
- **Direct tonemapping mode** — operator-only dispatch path that skips the dynamic tone curve and local pyramid.
- **Dev-menu UI** — operator combo, per-operator parameter sliders (Hable, AgX, Lottes), Direct-mode toggle. Shown in both the `DxvkToneMapping` and `DxvkLocalToneMapping` ImGui settings.
- **Fork-touchpoint fridge-list entries** for every upstream file touched, added in the same commit as the touch.

## Out of scope

- Workstream 3 (HDR / swapchain format / output color space).
- Workstream 4 (Texture categorization).
- Workstream 6 (misc stability + custom splash remnant).
- Upstream PRs — the fork stays a fork. These commits ship only to the port, referenced by gmod hashes in commit messages for provenance.
- CI enforcement of the fridge list — remains opt-in convention per the touchpoint-pattern spec.

## ABI / config contract

- `remixapi_Interface` and all `remixapi_*` struct layouts are **untouched** by this workstream. No plugin ABI risk.
- The `rtx.tonemap.finalizeWithACES` and `rtx.localtonemap.finalizeWithACES` RtxOptions are **removed** under the enum refactor. Plugin-side code that calls `SetConfigVariable("rtx.tonemap.finalizeWithACES", ...)` or the local equivalent will receive a "key not registered" warning — harmless log entry, not a crash. Plugins migrate to the new `rtx.tonemap.tonemapOperator` / `rtx.localtonemap.tonemapOperator` keys when convenient.
- `rtx.useLegacyACES` and `rtx.showLegacyACESOption` RtxOptions (at global `rtx` namespace, NOT `rtx.tonemap`) are also removed; both are superseded by `tonemapOperator == ACESLegacy`. The standalone "Use Legacy ACES" checkbox at `dxvk_imgui.cpp` is removed alongside them.
- **Two** `tonemapOperator` RtxOptions ship, not one — `rtx.tonemap.tonemapOperator` defaults to `None` (matches port's current global default `finalizeWithACES=false`); `rtx.localtonemap.tonemapOperator` defaults to `ACESLegacy` (matches port's current local defaults `finalizeWithACES=true, useLegacyACES=true`). This preserves both paths' existing visual behavior; they're independently tunable in the dev menu.
- No other RtxOption removals.

## Architecture

### Fork-owned files

| File | Contents |
|---|---|
| `src/dxvk/rtx_render/rtx_fork_tonemap.h` | `enum class TonemapOperator { None, ACES, ACESLegacy, HableFilmic, AgX, Lottes2016 }`. Hook function declarations (mirror of the entries in `rtx_fork_hooks.h`). |
| `src/dxvk/rtx_render/rtx_fork_tonemap.cpp` | **Two** `tonemapOperator` RtxOptions — `rtx.tonemap.tonemapOperator` (default `None`) and `rtx.localtonemap.tonemapOperator` (default `ACESLegacy`). One `rtx.tonemap.tonemappingMode` RtxOption for Direct mode. All per-operator `RTX_OPTION` declarations (Hable `exposureBias` + `A`–`F` + `W`; AgX look + contrast/offset/saturation; Lottes `hdrMax`/`contrast`/`shoulder`/`midIn`/`midOut`). Implementations of the five hook functions. |
| `src/dxvk/shaders/rtx/pass/tonemap/AgX.hlsl` | AgX operator function body. Port from gmod `f3501d46` byte-for-byte. |
| `src/dxvk/shaders/rtx/pass/tonemap/Lottes.hlsl` | Lottes 2016 operator function body. Port from gmod `cdf2c723` byte-for-byte. |
| `src/dxvk/shaders/rtx/pass/tonemap/fork_tonemap_operators.slangh` | Hable function + `applyTonemapOperator(uint op, vec3 color, …)` dispatcher. `#include`s `AgX.hlsl` and `Lottes.hlsl`. Consumers (`tonemapping.slangh`, local-tonemap shaders) `#include` this header and call the dispatcher. |

### Hook surface (additions to `rtx_fork_hooks.h`, `fork_hooks::` namespace)

| Hook | Call site (upstream) | Role |
|---|---|---|
| `populateTonemapOperatorArgs(ToneMappingApplyToneMappingArgs&)` | `DxvkToneMapping::dispatchApplyToneMapping` | Reads `rtx.tonemap.tonemapOperator` and writes the operator enum value + per-operator parameter values into the global args struct. |
| `populateLocalTonemapOperatorArgs(…)` | `DxvkLocalToneMapping` args-population equivalent | Reads `rtx.localtonemap.tonemapOperator` (separate option from the global one) and writes into the local args struct. |
| `showTonemapOperatorUI()` | `DxvkToneMapping::showImguiSettings` | Renders operator combo + per-operator sliders + Direct-mode toggle. |
| `showLocalTonemapOperatorUI()` | `DxvkLocalToneMapping::showImguiSettings` | Same, for the local tonemapper's ImGui panel. |
| `shouldSkipToneCurve()` | `RtxContext` tonemap dispatch | Returns `true` when `tonemappingMode == Direct`; caller skips histogram + tone-curve + local-pyramid passes. |

### Upstream touchpoints (indexed in `docs/fork-touchpoints.md`)

| Upstream file | Touch shape | Approx size |
|---|---|---|
| `src/dxvk/rtx_render/rtx_tone_mapping.h` | Remove `finalizeWithACES` RtxOption (the global's fork replacement lives in `rtx_fork_tonemap.cpp`). Add `#include "rtx_fork_tonemap.h"`. | ~2-line swap |
| `src/dxvk/rtx_render/rtx_tone_mapping.cpp` | Two one-line hook calls (args population + UI). Switch `RemixGui::Checkbox("Finalize With ACES", ...)` → `fork_hooks::showTonemapOperatorUI()`. | 2 one-liners |
| `src/dxvk/rtx_render/rtx_local_tone_mapping.h` | Remove local `finalizeWithACES` RtxOption (default was `true`). Add `#include "rtx_fork_tonemap.h"`. | ~2 lines |
| `src/dxvk/rtx_render/rtx_local_tone_mapping.cpp` | Two one-line hook calls (local args population + local UI). | 2 one-liners |
| `src/dxvk/rtx_render/rtx_context.cpp` | Direct-mode dispatch branch via `shouldSkipToneCurve()` (lands in commit 3, not 2). | ~3 lines |
| `src/dxvk/imgui/dxvk_imgui.cpp` | Remove the standalone `RemixGui::Checkbox("Use Legacy ACES", ...)` at line 3888 (RtxOption it binds to is being deleted). | ~3-line delete |
| `src/dxvk/rtx_render/rtx_options.h` | Remove `rtx.useLegacyACES` + `rtx.showLegacyACESOption` RtxOptions (both at `rtx` namespace, NOT `rtx.tonemap`). | ~3-line delete |
| `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h` | Swap `finalizeWithACES`/`useLegacyACES` uints for `tonemapOperator` uint + operator-param fields. Preserve struct size via pad-slot trick. Add operator constants (`OPERATOR_NONE = 0`, etc.). | ~10 lines |
| `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.slangh` | One `#include "fork_tonemap_operators.slangh"` + one `applyTonemapOperator(…)` call replacing the inline ACES branch. | 2 lines |
| `src/dxvk/shaders/rtx/pass/tonemap/tonemapping_apply_tonemapping.comp.slang` | Operator-dispatch call replacing the inline ACES finalize branch. | 1–2 lines |
| `src/dxvk/shaders/rtx/pass/local_tonemap/local_tonemapping.h` | Same args-struct swap as the global header. | ~5 lines |
| `src/dxvk/shaders/rtx/pass/local_tonemap/final_combine.comp.slang` | Operator-dispatch call, mirroring the global apply. | 1–2 lines |
| `src/dxvk/shaders/rtx/pass/local_tonemap/luminance.comp.slang` | Tiny touch from gmod `acfaa6ab`; propagates operator field. | ~2 lines |
| `meson.build` | Register `fork_tonemap_operators.slangh`, `AgX.hlsl`, `Lottes.hlsl`, `rtx_fork_tonemap.cpp`. | 4 additions |

Enum/constant sharing pattern: operator constants are defined in the shader-shared header `tonemapping.h` as `static const uint` literals. C++ side has a matching `enum class TonemapOperator` in `rtx_fork_tonemap.h`. The `populateTonemapOperatorArgs` hook casts C++ enum → uint when writing shader args. Single source of truth for values; shader and C++ cannot drift.

### Struct-size preservation

`ToneMappingApplyToneMappingArgs` currently holds `finalizeWithACES` and `useLegacyACES` uints. After the refactor, these slots hold `tonemapOperator` (uint) and operator-specific parameter fields. Lottes and Hable share push-constant slots, per gmod `cdf2c723`, so the total struct size does not grow. A `static_assert(sizeof(ToneMappingApplyToneMappingArgs) == N)` is added after the change to lock the size in.

The local tonemap equivalent (`ToneMappingLocalArgs` or its local-path name) receives the same treatment.

## Code-migration approach

Five commits on branch `unity-workstream/02-tonemap`, each independently buildable and verified before moving on.

### Commit 1 — Scaffold fork_tonemap module

```
fork(tonemap): scaffold rtx_fork_tonemap module + hook decls
```

- Create empty `src/dxvk/rtx_render/rtx_fork_tonemap.h` and `.cpp` with the `fork_hooks::` namespace skeleton.
- Add the five new hook declarations to `rtx_fork_hooks.h` with doc comments matching existing hooks' style.
- Register the new `.cpp` in `meson.build`.
- Seed `docs/fork-touchpoints.md` entries for the upstream files that commits 2–5 will touch. Each entry marked as "pending — landing in commit N."

No upstream code changes. Building the port at this point should produce zero behavior change.

### Commit 2 — TonemapOperator enum + ACES routed through enum

```
fork(tonemap): introduce TonemapOperator enum, route ACES via enum (refs gmod acfaa6ab)
```

- Add `TonemapOperator` enum to `rtx_fork_tonemap.h`.
- Add `OPERATOR_NONE`/`OPERATOR_ACES`/`OPERATOR_ACES_LEGACY` constants to shader header `tonemapping.h`.
- Swap `finalizeWithACES`/`useLegacyACES` uints in shader args structs (global + local) for `tonemapOperator` uint + pad slots for future operator params. Add `static_assert` on struct size.
- Implement `populateTonemapOperatorArgs` and `populateLocalTonemapOperatorArgs` — only the ACES routing path is functional at this point.
- Implement `shouldSkipToneCurve` as `return false` for now (Direct mode lands in commit 3).
- Create `fork_tonemap_operators.slangh` with a dispatcher that handles `None`/`ACES`/`ACESLegacy` cases (calls into the existing ACES implementation).
- Replace inline ACES branches in upstream tonemap shaders (`tonemapping.slangh`, `tonemapping_apply_tonemapping.comp.slang`, `final_combine.comp.slang`, `luminance.comp.slang`) with calls to the dispatcher.
- Remove `rtx.tonemap.finalizeWithACES`, `rtx.tonemap.useLegacyACES`, `rtx.tonemap.showLegacyACESOption` RtxOptions from upstream (`rtx_tone_mapping.h`, `rtx_local_tone_mapping.h`, `rtx_options.h` as applicable).
- Replace the old `Finalize With ACES` checkbox in `dxvk_imgui.cpp` with a call to `fork_hooks::showTonemapOperatorUI()`. UI hook at this point shows only the operator combo populated with `ACES`/`ACESLegacy`/`None`.
- Update fridge list for each upstream file touched.

**Visual behavior:** identical to pre-commit. ACES default still renders ACES; selecting `ACESLegacy` reproduces the old `useLegacyACES == true` behavior.

### Commit 3 — Hable Filmic + Direct mode + Hable sliders

```
fork(tonemap): add Hable Filmic operator + Direct mode + sliders (refs gmod acfaa6ab, baad5e79)
```

- Extend `TonemapOperator` enum with `HableFilmic`.
- Add shader-side `OPERATOR_HABLE_FILMIC` constant in `tonemapping.h`.
- Add the `hable(...)` function to `fork_tonemap_operators.slangh`, extend dispatcher to route `OPERATOR_HABLE_FILMIC`.
- Add Hable parameter RtxOptions to `rtx_fork_tonemap.cpp` (`exposureBias`, `A`, `B`, `C`, `D`, `E`, `F`, `W`, defaults from gmod).
- Wire parameters into the shader args struct in `populateTonemapOperatorArgs` / `populateLocalTonemapOperatorArgs`.
- Extend `showTonemapOperatorUI` / `showLocalTonemapOperatorUI` to render Hable sliders when Hable is the selected operator, plus the Direct-mode toggle.
- Implement `shouldSkipToneCurve` — returns true when `tonemappingMode == Direct`.
- Add the Direct-mode dispatch branch in `rtx_context.cpp` (or equivalent) via the hook.

### Commit 4 — AgX operator

```
fork(tonemap): add AgX operator (refs gmod f3501d46)
```

- Add `AgX.hlsl` to `src/dxvk/shaders/rtx/pass/tonemap/` (fork-owned).
- Register in `meson.build`.
- Extend `TonemapOperator` enum with `AgX`.
- Add shader-side `OPERATOR_AGX` constant.
- Extend dispatcher in `fork_tonemap_operators.slangh`.
- Add AgX parameter RtxOptions to `rtx_fork_tonemap.cpp` (look mode, contrast, offset, saturation).
- Wire into shader args + UI surface.

### Commit 5 — Lottes 2016 operator

```
fork(tonemap): add Lottes 2016 operator (refs gmod cdf2c723)
```

- Add `Lottes.hlsl` to `src/dxvk/shaders/rtx/pass/tonemap/` (fork-owned).
- Register in `meson.build`.
- Extend `TonemapOperator` enum with `Lottes2016`.
- Add shader-side `OPERATOR_LOTTES_2016` constant.
- Extend dispatcher in `fork_tonemap_operators.slangh`.
- Add Lottes parameter RtxOptions to `rtx_fork_tonemap.cpp` (`hdrMax`, `contrast`, `shoulder`, `midIn`, `midOut`).
- Wire into shader args + UI, reusing Hable push-constant slots (gmod's trick — mutually exclusive operators).
- Final `static_assert` on struct size confirms no size growth.

## Validation

### Iteration-time (per commit, during implementation)

- `meson compile -C _Comp64Release` clean, zero new warnings.
- All new shaders compile.
- The build's auto-deploy hop continues to drop the DLL for Skyrim iteration.
- `git grep "\[RTX-Diag\]"` returns zero matches — no diagnostic instrumentation introduced.
- `fork-touchpoints.md` updated in the same commit for every upstream file touched.
- Shader-args-struct `static_assert` holds after each commit that touches those structs (commits 2–5).

### Runtime (after commit 5)

- Built DLL deployed per the user's build script.
- Skyrim launched, save loaded, dev menu opened (Alt+X) → Rendering tab.
- Operator combo lists: None, ACES, ACES Legacy, Hable Filmic, AgX, Lottes 2016.
- Per-operator parameter sliders visible under their selected operator (Hable A–F/W, AgX look + sliders, Lottes hdrMax/contrast/shoulder/midIn/midOut).
- Direct-mode toggle visible.
- Each operator rendered in a test scene without obvious artifacts (no NaN pixels, no clipped blacks, no saturation divergence).
- ACES default still matches pre-port baseline visually.

### Log baselines

| Signal | Target |
|---|---|
| Plugin log `[info]` lines over ~9 min active play | Comparable to Hillaire workstream baseline (~200K lines) |
| Plugin log `[warn]` / `[error]` | 0 / 0 (pre-existing integrated-GPU skip warning ignored) |
| DXVK log shutdown noise | Pre-existing swap-chain-recreate + undisposed-objects noise — ignored |
| `SetConfigVariable failed ... rtx.tonemap.finalizeWithACES` | Expected if any consumer still sets that key. Harmless. |

### Merge gate (before workstream branch merges into `unity` integration)

- Skyrim Remix plugin loads, renders a scene with each operator selectable, no crashes.
- Workstream 1 and Workstream 5 functionality still intact (API + HW skinning + Hillaire sky).
- Touchpoint-pattern refactor's rebase-time win still holds — `fork-touchpoints.md` reflects every new upstream touch.

## Git layout

- **Port repo:** `c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix`.
- **Spec branch:** `unity-port-planning` (this document and W1/W5 specs live here).
- **Execution branch:** `unity-workstream/02-tonemap`, branched off `unity-fork-touchpoint-refactor` tip `dfe2e43c`.
- **Worktree:** `c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-02-tonemap/`. Fresh worktree — `git submodule update --init --recursive` before first build.
- **Merge target:** the port's unity integration branch, same target as W1 and W5.
- **Commit authorship:** `Kim2091 <jpavatargirl@gmail.com>` only. Zero AI co-author trailers on any commit.
- **Remote:** push to `kim2091` only when work is green and the user has approved. No auto-PR.

## Known risks

1. **Shader args struct size drift.** Swapping `finalizeWithACES`/`useLegacyACES` uints for operator enum + per-operator parameter fields risks growing the struct beyond its current size, breaking constant-buffer layout. *Mitigation:* use gmod's pad-slot trick (replaced fields map 1:1 to pad slots); Lottes and Hable share slots per gmod `cdf2c723`. Add `static_assert(sizeof(...) == N)` on each struct after commit 2. Verified on every subsequent commit touching those structs.

2. **`rtx.tonemap.finalizeWithACES` config-key removal.** Any consumer (Skyrim plugin, mod config files) setting that key post-port receives a "key not registered" warning. *Mitigation:* harmless log noise only, no crash. Documented in this spec. Plugin consumers migrate to `rtx.tonemap.tonemapOperator` at their leisure.

3. **Port's local tonemapper dispatch shape differs from gmod's.** The local-tonemap code path has evolved on both forks; gmod's commits may not apply line-for-line. *Mitigation:* per-file compare of port vs. gmod before applying each commit's local-path changes; any non-mechanical semantic bridging noted in the commit message.

4. **Push-constant / shader binding-slot overlap for Lottes.** Lottes reuses Hable slots because operators are mutually exclusive. Must preserve that alias exactly as gmod has it. *Mitigation:* port Lottes byte-for-byte from `cdf2c723`; document the slot aliasing in `rtx_fork_tonemap.cpp`.

5. **New `.hlsl` / `.slangh` files silently not compiled.** If `AgX.hlsl`, `Lottes.hlsl`, or `fork_tonemap_operators.slangh` aren't registered in `meson.build`, they won't be compiled and their functions won't link. *Mitigation:* every commit that adds a shader file edits `meson.build` in the same commit. Iteration-time build catches the miss within minutes.

6. **Gmod's `f3501d46` AgX commit predates the enum refactor.** `f3501d46` is dated 2025-06-18 (gmod's own tip at the time), while `acfaa6ab` (the enum refactor) is dated 2026-03-17. Gmod merged AgX into the new enum shape when `acfaa6ab` landed, so the gmod-tip version of AgX already sits inside the enum world. *Mitigation:* apply AgX to the port at the enum-refactored state (commit 4 of this workstream) — the working reference is gmod's tip, not AgX's original pre-enum form.

7. **UI surface-area growth in the Rendering tab.** Adding per-operator slider groups may crowd the dev menu. *Mitigation:* render per-operator sliders under their respective operator's selection only (collapsed when another operator is active). Matches gmod's UX.

## What comes next

When Workstream 2 is validated and merged into the unity integration branch:

1. **Workstream 3 (HDR)** — now unblocked. Output color-space plumbing, swapchain format, HDR display path. Separate spec.
2. **Workstream 4 (Texture categorization)** — legacy emissive, alpha-as-mask, baked lighting rules. Separate spec.
3. **Workstream 6 (misc stability + splash)** — the remnant of the dropped splash scope + stability cleanup.
