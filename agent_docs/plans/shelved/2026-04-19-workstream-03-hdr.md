# Workstream 3 (HDR) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port gmod's HDR output feature into the unity-fork port using the fork-touchpoint pattern, delivering PQ (HDR10) and HLG transfer functions, ACES HDR tone mapper, color grading, and a full dev-menu UI — in 8 commits, each independently buildable, runtime-validated on an HDR monitor at the end.

**Architecture:** One new fork-owned module (`rtx_fork_hdr.h/cpp`) holds all 17 RtxOptions, hooks, and UI. One fork-owned shader (`hdr_processing.comp.slang`) is ported byte-for-byte from gmod. When HDR is enabled, a single hook at the top of `DxvkToneMapping::dispatch` routes to the HDR shader, bypassing the SDR operator pipeline entirely. When HDR is off, nothing changes from W2. Upstream files receive ~45 lines of hook calls + small inline tweaks across ~7 files, all fridge-list-catalogued.

**Tech Stack:** C++17, HLSL/Slang compute shaders, meson+ninja build, DXVK Vulkan backend, ImGui for UI, RtxOptions for config plumbing.

**Reference paths** (read-only):
- **Port worktree (our workspace):** `c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/`
- **Port main repo:** `c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/`
- **Gmod reference (READ-ONLY):** `c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/`
- **W3 spec:** `docs/superpowers/specs/2026-04-19-unity-fork-port-workstream-03-hdr-design.md`

**Build command (always the same):**
```bash
powershell.exe -NoProfile -Command "& { Set-Location 'c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr'; . .\build_common.ps1; PerformBuild -BuildFlavour release -BuildSubDir _Comp64Release -Backend ninja -EnableTracy false }"
```

**Non-negotiables:**
- All commits authored by Kim2091 (default git config). **NO AI co-author trailer** under any circumstances.
- **Do NOT push** to any remote unless the user explicitly approves.
- Use `git -C <worktree-path>` for all git ops, or `cd` into the worktree at the start of each bash block. Do NOT drift into the gmod reference repo's working directory.
- If you need to read gmod source, use Read tool with absolute paths — never `cd` there.

---

## Task 0: Set up W3 worktree

**Files:**
- Create: `.worktrees/unity-workstream-03-hdr/` (new worktree off `unity-port-planning`)

- [ ] **Step 0.1: Verify current branch state in main port repo**

```bash
git -C "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix" branch --show-current
```
Expected: `unity-port-planning` (where the W3 spec was just committed).

- [ ] **Step 0.2: Create W3 worktree on a new branch**

```bash
git -C "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix" worktree add -b unity-workstream/03-hdr .worktrees/unity-workstream-03-hdr unity-port-planning
```
Expected: `Preparing worktree (new branch 'unity-workstream/03-hdr')` + `HEAD is now at <sha> Add Workstream 3 (HDR) design spec`.

- [ ] **Step 0.3: Verify worktree is clean + on the right branch**

```bash
git -C "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" status
git -C "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" log --oneline -3
```
Expected: `On branch unity-workstream/03-hdr`, nothing to commit, tip is the W3 spec commit.

---

## Commit 1 — Scaffold HDR module

**Purpose:** Create the fork-owned HDR module skeleton, hook declarations, binding constants, and meson registration. No behavior change.

**Files:**
- Create: `src/dxvk/rtx_render/rtx_fork_hdr.h`
- Create: `src/dxvk/rtx_render/rtx_fork_hdr.cpp`
- Modify: `src/dxvk/rtx_render/rtx_fork_hooks.h`
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h`
- Modify: `src/dxvk/meson.build` (or the nearest enclosing meson.build that registers `rtx_fork_*.cpp` — find it by grep)
- Modify: `docs/fork-touchpoints.md`

### Files first

- [ ] **Step 1.1: Locate the meson.build line that registers existing rtx_fork_*.cpp files**

```bash
grep -rn "rtx_fork_tonemap.cpp\|rtx_fork_atmosphere.cpp\|rtx_fork_overlay.cpp" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/meson.build" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/rtx_render/meson.build" 2>/dev/null | head -5
```
Expected: Shows one or more lines naming existing fork `.cpp` files in a source list. Note the exact file + line so you know where to add `rtx_fork_hdr.cpp`.

### Step 1.2 — Create rtx_fork_hdr.h

- [ ] Create `src/dxvk/rtx_render/rtx_fork_hdr.h` with this content:

```cpp
#pragma once

// rtx_fork_hdr.h — fork-owned declarations for HDR output. All HDR logic
// (17 RtxOptions across 2 classes, 2 enums, HDR processing shader class,
// 5 hooks) lives in this module + rtx_fork_hdr.cpp + the fork-owned
// shader at src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang.
//
// When HDR is ON, DxvkToneMapping::dispatch routes to dispatchHDRProcessing
// via the isHDREnabled/dispatchHDRProcessing hook pair; the SDR operator
// pipeline is bypassed entirely. When HDR is OFF, this module's code is
// inert.

#include <cstdint>

#include "rtx_option.h"

namespace dxvk {

  // HDR transfer function selection for the composed color space output.
  // Values must stay in lockstep with the uint constants used in
  // hdr_processing.comp.slang (0 = Linear / scRGB BT.709, 1 = PQ / HDR10,
  // 2 = HLG / BT.2100).
  enum class HDRFormat : uint32_t {
    Linear = 0,
    PQ     = 1,
    HLG    = 2,
  };

  // HDR tone mapping method. Values stay in lockstep with
  // hdr_processing.comp.slang's `cb.hdrToneMapper` switch.
  enum class HDRToneMapper : uint32_t {
    None     = 0,
    ACES_HDR = 1,
  };

  // Main HDR output options. All 13 defaults match gmod's rtx_tone_mapping.h
  // at its current tip; enableHDR defaults false so HDR is opt-in.
  class RtxForkHDR {
    // (RtxOption declarations added in commit 2.)
  };

  // HDR-specific auto-exposure options. Separate from RtxForkHDR because
  // they live under the rtx.autoExposure namespace and are read by the
  // auto-exposure pass rather than the HDR processing shader.
  class RtxForkHDRAutoExposure {
    // (RtxOption declarations added in commit 2.)
  };

} // namespace dxvk
```

### Step 1.3 — Create rtx_fork_hdr.cpp skeleton

- [ ] Create `src/dxvk/rtx_render/rtx_fork_hdr.cpp` with this content:

```cpp
// rtx_fork_hdr.cpp
//
// Fork-owned implementations of the HDR hooks declared in rtx_fork_hooks.h.
// Populated incrementally:
//   - Commit 1 (this commit): scaffold only.
//   - Commit 2: RtxOption declarations + HDRProcessingArgs shader struct.
//   - Commit 3: HDRProcessingShader + dispatchHDRProcessing impl.
//   - Commit 4: isHDREnabled + tonemap-branch wiring.
//   - Commit 5: populateCompositeHDRArgs.
//   - Commit 6: auto-exposure HDR param resolution + UI.
//   - Commit 7: main HDR UI section.
//
// See docs/fork-touchpoints.md for the catalogue of fork hooks and which
// upstream files call each one.

#include "rtx_fork_hooks.h"
#include "rtx_fork_hdr.h"

namespace dxvk {
  namespace fork_hooks {

    // Hook implementations land in commits 3-7.

  } // namespace fork_hooks
} // namespace dxvk
```

### Step 1.4 — Add hook declarations to rtx_fork_hooks.h

- [ ] Read the existing `src/dxvk/rtx_render/rtx_fork_hooks.h` and locate the end of the `fork_hooks::` namespace (before the closing `}`). Add these declarations there, grouped with a comment:

```cpp
    // ---- HDR (W3) ----
    //
    // Implementations live in rtx_fork_hdr.cpp.

    // Returns RtxForkHDR::enableHDR() — the central query used by all
    // HDR-aware call sites to gate their HDR-vs-SDR behavior.
    bool isHDREnabled();

    // Dispatches the HDR processing compute shader. Called from
    // DxvkToneMapping::dispatch when isHDREnabled() is true; bypasses
    // the entire SDR tonemap pipeline.
    //
    // Reads all 13 RtxForkHDR options + relevant RtxForkHDRAutoExposure
    // state to populate HDRProcessingArgs push constants.
    void dispatchHDRProcessing(
      Rc<RtxContext> ctx,
      Rc<DxvkSampler> linearSampler,
      Rc<DxvkImageView> exposureView,
      const Resources::Resource& inputColorBuffer,
      const Resources::Resource& outputColorBuffer,
      uint32_t frameIndex,
      bool autoExposureEnabled);

    // Writes `args.enableHDR = isHDREnabled()`. Called from
    // RtxContext::dispatchComposite or wherever CompositeArgs is
    // assembled (find the call site by grep during commit 5).
    void populateCompositeHDRArgs(struct CompositeArgs& args);

    // Renders the HDR CollapsingHeader's contents (sliders, combos,
    // toggles). Called from ImGUI::showRenderingSettings's
    // Post-Processing section inside a new CollapsingHeader("HDR").
    void showHDRUI();

    // Renders the HDR-specific auto-exposure UI section. Called from
    // DxvkAutoExposure::showImguiSettings near the top of the function
    // (before the standard SDR controls). Only emits UI when
    // isHDREnabled() is true.
    void showHDRAutoExposureUI();
```

Note: the forward declaration `struct CompositeArgs` is used so we don't have to pull in the shader-shared header from this file. The real struct definition lives at `src/dxvk/shaders/rtx/pass/composite/composite_args.h`.

### Step 1.5 — Add HDR binding constants to tonemapping.h

- [ ] Read `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h`. Find the cluster of `#define` binding-index constants for the existing tonemap passes (search for `TONEMAPPING_APPLY_` or `TONEMAPPING_HISTOGRAM_` to locate them). Add the HDR Processing binding constants right after the last existing tonemap binding cluster, using the next four unused integer slots. Exact slot numbers depend on what's free; increment from the last tonemap binding index.

Add this block:

```c
// HDR Processing pass bindings (fork-owned, W3)
#define HDR_PROCESSING_BLUE_NOISE_TEXTURE  <next_free_slot>
#define HDR_PROCESSING_INPUT_BUFFER        <next_free_slot_plus_1>
#define HDR_PROCESSING_OUTPUT_BUFFER       <next_free_slot_plus_2>
#define HDR_PROCESSING_EXPOSURE_INPUT      <next_free_slot_plus_3>
```

Replace `<next_free_slot>` etc. with concrete integers that don't collide with any existing binding in this header. If TONEMAPPING_APPLY_TONEMAPPING_COLOR_OUTPUT is (e.g.) slot 4, use slots 5,6,7,8.

### Step 1.6 — Register rtx_fork_hdr.cpp in meson

- [ ] Open the meson file identified in Step 1.1. In the source list that contains existing `rtx_fork_*.cpp` entries, add a new entry for `rtx_fork_hdr.cpp` immediately after `rtx_fork_tonemap.cpp` (or in alphabetical position matching the existing convention). Match the exact indentation and quoting style of the surrounding entries.

### Step 1.7 — Seed fork-touchpoints.md entries

- [ ] Open `docs/fork-touchpoints.md`. For each upstream file W3 will touch, either add a new "pending" entry or extend an existing entry. The files are:

  - `src/dxvk/rtx_render/rtx_tone_mapping.cpp` — **Hook** at top of `DxvkToneMapping::dispatch`, `fork_hooks::isHDREnabled` + `fork_hooks::dispatchHDRProcessing`. Landing in commit 4.
  - `src/dxvk/rtx_render/rtx_composite.cpp` — **Hook** wherever `CompositeArgs` is assembled, `fork_hooks::populateCompositeHDRArgs`. Landing in commit 5.
  - `src/dxvk/rtx_render/rtx_auto_exposure.cpp` — **Hook** at top of `showImguiSettings`, `fork_hooks::showHDRAutoExposureUI`; possible small inline tweak in dispatch for HDR-param resolution. Landing in commit 6.
  - `src/dxvk/imgui/dxvk_imgui.cpp` — **Inline tweak**: new `CollapsingHeader("HDR")` inside Post-Processing that calls `fork_hooks::showHDRUI()`. Landing in commit 7.
  - `src/dxvk/shaders/rtx/pass/composite/composite_args.h` — **Inline tweak**: add `uint enableHDR;` field + static_assert. Landing in commit 5.
  - `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h` — **Inline tweak**: HDR binding constants + HDRProcessingArgs struct (landing in commit 1 + commit 2).

Use the existing entry style — `**Inline tweak** at ...`, `**Hook** at ...`. Group by upstream file as the existing doc does.

### Step 1.8 — Build

- [ ] Run the build command at the top of this plan. Expected: success, no errors, no warnings.

### Step 1.9 — Commit

```bash
cd "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" && git add src/dxvk/rtx_render/rtx_fork_hdr.h src/dxvk/rtx_render/rtx_fork_hdr.cpp src/dxvk/rtx_render/rtx_fork_hooks.h src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h src/dxvk/meson.build docs/fork-touchpoints.md && git commit -m "$(cat <<'EOF'
fork(hdr): scaffold rtx_fork_hdr module + enums + binding constants

Introduces the fork-owned module that will carry all HDR output logic
for Workstream 3: HDRFormat + HDRToneMapper enums, empty RtxForkHDR
and RtxForkHDRAutoExposure class skeletons, and five hook declarations
in rtx_fork_hooks.h (isHDREnabled, dispatchHDRProcessing,
populateCompositeHDRArgs, showHDRUI, showHDRAutoExposureUI).

HDR processing shader binding constants added to tonemapping.h;
rtx_fork_hdr.cpp registered in the meson source list.

docs/fork-touchpoints.md seeded with pending entries for every
upstream file W3 will touch across commits 2-7.

No behavior change.
EOF
)"
```
Expected: commit succeeds on `unity-workstream/03-hdr`, no co-author trailer, Kim2091 as author.

---

## Commit 2 — RtxForkHDR + RtxForkHDRAutoExposure option classes

**Purpose:** Declare all 17 RtxOptions and the HDRProcessingArgs shader struct. Options are exposed but unused — no behavior change.

**Files:**
- Modify: `src/dxvk/rtx_render/rtx_fork_hdr.h`
- Modify: `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h`

### Step 2.1 — Read gmod's auto-exposure HDR defaults

- [ ] Before writing code, read gmod's `rtx_auto_exposure.h` to pull the exact defaults, ranges, and docstrings for the 4 HDR auto-exposure options. Use Read tool:
  - Path: `c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/src/dxvk/rtx_render/rtx_auto_exposure.h`
  - Look for `RTX_OPTION(...useHDRSpecificSettings...)`, `...hdrAutoExposureSpeed...`, `...hdrEvMinValue...`, `...hdrEvMaxValue...`. Record the type, default value, and docstring for each — these exact values go into RtxForkHDRAutoExposure in step 2.3.

### Step 2.2 — Add 13 RtxOptions to RtxForkHDR

- [ ] In `src/dxvk/rtx_render/rtx_fork_hdr.h`, replace the empty `class RtxForkHDR { ... };` block with this:

```cpp
  class RtxForkHDR {
    RTX_OPTION("rtx.tonemap", bool, enableHDR, false,
               "Enable HDR output mode. Requires HDR-capable display and Windows HDR mode enabled at the OS level — without Windows HDR, the image will appear washed-out because the shader emits PQ/HLG-encoded values into a standard SDR swapchain.");
    RTX_OPTION("rtx.tonemap", HDRFormat, hdrFormat, HDRFormat::PQ,
               "HDR output format. 0 = Linear (scRGB / compatibility), 1 = PQ (HDR10 / ST.2084 — most displays), 2 = HLG (Hybrid Log-Gamma — broadcast standard).");
    RTX_OPTION("rtx.tonemap", HDRToneMapper, hdrToneMapper, HDRToneMapper::None,
               "HDR tone mapping method. 0 = None (linear bypass), 1 = ACES HDR (ACES RRT+ODT adapted for HDR range preservation).");
    RTX_OPTION("rtx.tonemap", bool, hdrEnableDithering, true,
               "Enable blue-noise dithering for HDR output. Reduces banding artifacts at the cost of a small amount of monochromatic noise.");
    RTX_OPTION("rtx.tonemap", float, hdrBlueNoiseAmplitude, 20.0f,
               "HDR blue noise dithering amplitude multiplier. 1.0 = optimal, 0.0 = no dithering, >1.0 = stronger for testing. Range [1.0, 40.0].");
    RTX_OPTION("rtx.tonemap", float, hdrExposureBias, 0.0f,
               "HDR exposure adjustment in EV stops. Positive values brighten the image. Range [-3.0, 20.0] (matches gmod's UI range).");
    RTX_OPTION("rtx.tonemap", float, hdrBrightness, 1.0f,
               "HDR brightness multiplier applied after tone mapping. Higher values increase overall brightness. Range [0.1, 20.0].");
    RTX_OPTION("rtx.tonemap", float, hdrMaxLuminance, 1000.0f,
               "Maximum display brightness in nits for HDR output. Typical consumer displays: 1000-4000.");
    RTX_OPTION("rtx.tonemap", float, hdrMinLuminance, 0.01f,
               "Minimum display brightness in nits for HDR output. Typical consumer displays: 0.01-0.05.");
    RTX_OPTION("rtx.tonemap", float, hdrPaperWhiteLuminance, 100.0f,
               "Reference paper-white luminance in nits. Typical range: 80-400. 100 matches the SDR reference; higher values make the image brighter relative to the HDR envelope.");
    RTX_OPTION("rtx.tonemap", float, hdrShadows, 0.0f,
               "HDR shadows adjustment (log-luminance tri-band). Negative values darken shadows, positive values lift them. Range [-1.0, 1.0].");
    RTX_OPTION("rtx.tonemap", float, hdrMidtones, 0.0f,
               "HDR midtones adjustment (log-luminance tri-band). Range [-1.0, 1.0].");
    RTX_OPTION("rtx.tonemap", float, hdrHighlights, 0.0f,
               "HDR highlights adjustment (log-luminance tri-band). Range [-1.0, 1.0].");
  };
```

### Step 2.3 — Add 4 RtxOptions to RtxForkHDRAutoExposure

- [ ] In the same file, replace the empty `class RtxForkHDRAutoExposure { ... };` block with the 4 options using the exact defaults/ranges/docstrings read in step 2.1. Template:

```cpp
  class RtxForkHDRAutoExposure {
    RTX_OPTION("rtx.autoExposure", bool, useHDRSpecificSettings, <default_from_gmod>,
               "<docstring_from_gmod_or_adapted>");
    RTX_OPTION("rtx.autoExposure", float, hdrAutoExposureSpeed, <default_from_gmod>,
               "<docstring_from_gmod>");
    RTX_OPTION("rtx.autoExposure", float, hdrEvMinValue, <default_from_gmod>,
               "<docstring_from_gmod>");
    RTX_OPTION("rtx.autoExposure", float, hdrEvMaxValue, <default_from_gmod>,
               "<docstring_from_gmod>");
  };
```

If gmod declares these under a different class (e.g., `DxvkAutoExposure`), copy the defaults and wrap the docstrings with the same wording, adjusting only where gmod's phrasing refers to things not present in the port.

### Step 2.4 — Add HDRProcessingArgs struct to tonemapping.h

- [ ] In `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h`, right after the HDR_PROCESSING binding constants added in commit 1, add the full `HDRProcessingArgs` struct — byte-for-byte match with gmod's definition. Read gmod's version first:
  - Path: `c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang` (the struct is defined inline in the shader in gmod) OR check `c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h` if it's been moved there.

  The struct has these 15 fields (from the W3 spec):

```c
struct HDRProcessingArgs {
  uint  enableAutoExposure;
  float hdrMaxLuminance;
  float hdrMinLuminance;
  float hdrPaperWhiteLuminance;
  float exposureFactor;
  uint  frameIndex;
  uint  hdrFormat;        // 0=Linear, 1=PQ, 2=HLG
  float hdrExposureBias;
  float hdrBrightness;
  uint  hdrToneMapper;    // 0=None, 1=ACES_HDR
  uint  hdrEnableDithering;
  float hdrShadows;
  float hdrMidtones;
  float hdrHighlights;
  float hdrBlueNoiseAmplitude;
};
```

Place this struct right after the HDR_PROCESSING_* `#define` block. If tonemapping.h has existing struct definitions at its end, place HDRProcessingArgs alongside them.

### Step 2.5 — Build

- [ ] Run the build command. Expected: success, no errors, no warnings. The options are registered with the config system but nothing reads them yet.

### Step 2.6 — Commit

```bash
cd "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" && git add src/dxvk/rtx_render/rtx_fork_hdr.h src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h && git commit -m "$(cat <<'EOF'
fork(hdr): declare RtxForkHDR + RtxForkHDRAutoExposure options

RtxForkHDR: 13 options covering enable flag, output format (Linear/PQ/
HLG), tone mapper (None/ACES_HDR), dithering, exposure bias, brightness,
three luminance reference values (max/min/paper-white in nits), and
three log-luminance tri-band adjustments (shadows/midtones/highlights).

RtxForkHDRAutoExposure: 4 options for HDR-specific auto-exposure
tuning, matching gmod's defaults/ranges exactly (pulled from
rtx_auto_exposure.h at gmod's current tip).

HDRProcessingArgs shader push-constant struct added to tonemapping.h
byte-for-byte matching gmod's definition; 15 fields consumed by the
HDR processing compute shader landing in commit 3.

Options are exposed to the config system but unread — no behavior change.
EOF
)"
```

---

## Commit 3 — Port HDR shader + dispatch plumbing

**Purpose:** Port the 325-line HDR processing compute shader byte-for-byte. Register the shader class and implement the dispatch hook. Shader is compiled but not yet called from the main tonemap dispatch.

**Files:**
- Create: `src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang`
- Modify: `src/dxvk/rtx_render/rtx_fork_hdr.cpp`
- Modify: `src/dxvk/rtx_render/rtx_fork_hdr.h`
- Modify: `src/dxvk/meson.build` (shader list)

### Step 3.1 — Audit gmod's shader registration pattern

- [ ] Read gmod's `rtx_tone_mapping.cpp` around the `ApplyTonemappingShader` class registration (grep for `class ApplyTonemappingShader`). Note the `ManagedShader` + `SHADER_SOURCE` + binding layout pattern. Use it as the template for `HDRProcessingShader`.

- [ ] Read gmod's `src/dxvk/rtx_render/rtx_tone_mapping.cpp` around the `dispatchHDRProcessing` function (around line 469) to understand the exact dispatch sequence: resource bindings, push-constant write, workgroup dimensions, shader bind. This is the reference implementation for step 3.4.

### Step 3.2 — Copy HDR shader byte-for-byte

- [ ] Copy gmod's HDR shader to the port worktree:

```bash
cp "c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang"
```
Do NOT modify the shader content. Verify it matches gmod byte-for-byte:
```bash
diff "c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang"
```
Expected: no output (files identical).

### Step 3.3 — Register shader in meson

- [ ] Find the meson file that registers existing `.comp.slang` shaders in the tonemap directory. Grep:
```bash
grep -rn "tonemapping_apply_tonemapping\|tonemapping_histogram" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/" 2>/dev/null | grep meson.build
```

- [ ] In that file, add a new entry for `hdr_processing.comp.slang` alongside the existing tonemap shader entries. The shader source identifier used by `SHADER_SOURCE(...)` is conventionally derived from the filename; use `tonemapping_hdr_processing` as the identifier so it matches the filename stem. Match the exact indentation/quoting style of surrounding entries.

### Step 3.4 — Declare HDRProcessingShader in rtx_fork_hdr.h

- [ ] In `src/dxvk/rtx_render/rtx_fork_hdr.h`, after the `RtxForkHDRAutoExposure` class and before the closing `}` of namespace dxvk, add a forward declaration comment. The class itself lives in the `.cpp` file (matches the pattern used by shader classes in `rtx_tone_mapping.cpp` which are file-local). Nothing to add in the header — just confirm.

### Step 3.5 — Implement HDRProcessingShader class + hooks in rtx_fork_hdr.cpp

- [ ] Expand `src/dxvk/rtx_render/rtx_fork_hdr.cpp` to contain the HDR shader class + `dispatchHDRProcessing` hook. Full file content:

```cpp
// rtx_fork_hdr.cpp
//
// Fork-owned implementations of the HDR hooks declared in rtx_fork_hooks.h.
// See rtx_fork_hdr.h for module overview.

#include "rtx_fork_hooks.h"
#include "rtx_fork_hdr.h"
#include "rtx_imgui.h"
#include "rtx_options.h"

#include "dxvk_shader_manager.h"         // ManagedShader, SHADER_SOURCE
#include "rtx/pass/tonemap/tonemapping.h"  // HDR_PROCESSING_* binding constants, HDRProcessingArgs
#include "rtx/pass/composite/composite_args.h"  // CompositeArgs (for populateCompositeHDRArgs)

#include <rtx_shaders/tonemapping_hdr_processing.h>  // generated shader header

#include "../imgui/imgui.h"

namespace dxvk {

  // HDR Processing compute shader class. Mirrors the ApplyTonemappingShader
  // pattern in rtx_tone_mapping.cpp but lives here because it's fork-owned.
  class HDRProcessingShader : public ManagedShader {
    SHADER_SOURCE(HDRProcessingShader, VK_SHADER_STAGE_COMPUTE_BIT, tonemapping_hdr_processing)

    PUSH_CONSTANTS(HDRProcessingArgs)

    BEGIN_PARAMETER()
      TEXTURE2DARRAY(HDR_PROCESSING_BLUE_NOISE_TEXTURE)
      RW_TEXTURE2D(HDR_PROCESSING_INPUT_BUFFER)
      RW_TEXTURE2D(HDR_PROCESSING_OUTPUT_BUFFER)
      RW_TEXTURE1D_READONLY(HDR_PROCESSING_EXPOSURE_INPUT)
    END_PARAMETER()
  };

  PREWARM_SHADER_PIPELINE(HDRProcessingShader);

  namespace fork_hooks {

    void dispatchHDRProcessing(
        Rc<RtxContext> ctx,
        Rc<DxvkSampler> linearSampler,
        Rc<DxvkImageView> exposureView,
        const Resources::Resource& inputColorBuffer,
        const Resources::Resource& outputColorBuffer,
        uint32_t frameIndex,
        bool autoExposureEnabled) {

      ScopedGpuProfileZone(ctx, "HDR Processing");

      const VkExtent3D workgroups = util::computeBlockCount(
        inputColorBuffer.view->imageInfo().extent,
        VkExtent3D{ 16, 16, 1 });

      HDRProcessingArgs pushArgs = {};
      pushArgs.enableAutoExposure      = autoExposureEnabled ? 1u : 0u;
      pushArgs.hdrMaxLuminance         = RtxForkHDR::hdrMaxLuminance();
      pushArgs.hdrMinLuminance         = RtxForkHDR::hdrMinLuminance();
      pushArgs.hdrPaperWhiteLuminance  = RtxForkHDR::hdrPaperWhiteLuminance();
      pushArgs.exposureFactor          = 1.0f;
      pushArgs.frameIndex              = frameIndex;
      pushArgs.hdrFormat               = static_cast<uint32_t>(RtxForkHDR::hdrFormat());
      pushArgs.hdrExposureBias         = RtxForkHDR::hdrExposureBias();
      pushArgs.hdrBrightness           = RtxForkHDR::hdrBrightness();
      pushArgs.hdrToneMapper           = static_cast<uint32_t>(RtxForkHDR::hdrToneMapper());
      pushArgs.hdrEnableDithering      = RtxForkHDR::hdrEnableDithering() ? 1u : 0u;
      pushArgs.hdrShadows              = RtxForkHDR::hdrShadows();
      pushArgs.hdrMidtones             = RtxForkHDR::hdrMidtones();
      pushArgs.hdrHighlights           = RtxForkHDR::hdrHighlights();
      pushArgs.hdrBlueNoiseAmplitude   = RtxForkHDR::hdrBlueNoiseAmplitude();

      ctx->bindResourceView(HDR_PROCESSING_BLUE_NOISE_TEXTURE,
                            ctx->getResourceManager().getBlueNoiseTexture(ctx), nullptr);
      ctx->bindResourceView(HDR_PROCESSING_INPUT_BUFFER, inputColorBuffer.view, nullptr);
      ctx->bindResourceView(HDR_PROCESSING_OUTPUT_BUFFER, outputColorBuffer.view, nullptr);
      ctx->bindResourceView(HDR_PROCESSING_EXPOSURE_INPUT, exposureView, nullptr);
      ctx->bindResourceSampler(HDR_PROCESSING_EXPOSURE_INPUT, linearSampler);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, HDRProcessingShader::getShader());
      ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

  } // namespace fork_hooks
} // namespace dxvk
```

Notes:
- `PUSH_CONSTANTS`, `BEGIN_PARAMETER`, `TEXTURE2DARRAY`, `RW_TEXTURE2D`, `RW_TEXTURE1D_READONLY`, `END_PARAMETER`, `PREWARM_SHADER_PIPELINE`, `ScopedGpuProfileZone` are all macros from DXVK's shader/managed-shader framework — verified against ApplyTonemappingShader in gmod's rtx_tone_mapping.cpp. Confirm the exact macro names in the port's existing `rtx_tone_mapping.cpp` while writing this step; adjust if port uses different names.
- The include `<rtx_shaders/tonemapping_hdr_processing.h>` is a generated header — the build system produces it from the `.slang` file. Its name matches the shader identifier registered in meson (step 3.3).

### Step 3.6 — Build

- [ ] Run the build command. Expected: success. The slang compiler produces `tonemapping_hdr_processing.h` from the new shader; `HDRProcessingShader` compiles and registers. No warnings, no errors. The hook is callable but not yet called from any dispatch path.

If the build fails with unresolved macro errors, compare macro usage against `ApplyTonemappingShader` in the port's `rtx_tone_mapping.cpp` and adjust.

### Step 3.7 — Commit

```bash
cd "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" && git add src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang src/dxvk/rtx_render/rtx_fork_hdr.cpp src/dxvk/meson.build && git commit -m "$(cat <<'EOF'
fork(hdr): port hdr_processing.comp.slang + dispatch plumbing

hdr_processing.comp.slang copied byte-for-byte from gmod's version.
Contains PQ (HDR10 / ST.2084) and HLG (BT.2100) transfer functions,
Rec.709 -> Rec.2020 gamut conversion, ACES HDR tone mapper variant,
tri-band log-luminance shadows/midtones/highlights grading, and
blue noise dithering — all in a single compute pass.

HDRProcessingShader class registered in rtx_fork_hdr.cpp using the
fork module pattern (file-local shader class, mirrors
ApplyTonemappingShader). Bindings match the HDR_PROCESSING_* constants
added to tonemapping.h in commit 1.

fork_hooks::dispatchHDRProcessing implemented: populates 15-field
HDRProcessingArgs from RtxForkHDR options, binds blue noise texture +
input/output color buffers + exposure texture, dispatches with
16x16 workgroups.

Shader is compiled and the hook is callable, but not yet wired into
the main tonemap dispatch path — enableHDR option still has no
runtime effect. Commit 4 closes the loop.

refs gmod <copy-SHA-here-from-gmod-git-log>
EOF
)"
```

Before committing, replace `<copy-SHA-here-from-gmod-git-log>` with the actual gmod commit SHA that added `hdr_processing.comp.slang`. Find it by running in the gmod repo directory (read-only):
```bash
git -C "c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod" log --oneline -- src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang | head -1
```

---

## Commit 4 — Wire HDR branch into DxvkToneMapping::dispatch

**Purpose:** Add the `isHDREnabled` hook implementation and insert the HDR dispatch branch at the top of `DxvkToneMapping::dispatch`. After this commit, setting `rtx.tonemap.enableHDR=True` in a config file causes the HDR shader to run end-to-end.

**Files:**
- Modify: `src/dxvk/rtx_render/rtx_fork_hdr.cpp` (add isHDREnabled)
- Modify: `src/dxvk/rtx_render/rtx_tone_mapping.cpp` (insert branch)
- Modify: `docs/fork-touchpoints.md` (upgrade pending entry to landed)

### Step 4.1 — Implement isHDREnabled

- [ ] In `src/dxvk/rtx_render/rtx_fork_hdr.cpp`, inside the `fork_hooks` namespace, before `dispatchHDRProcessing`, add:

```cpp
    bool isHDREnabled() {
      return RtxForkHDR::enableHDR();
    }
```

### Step 4.2 — Locate DxvkToneMapping::dispatch body

- [ ] Find the start of `DxvkToneMapping::dispatch` body in `src/dxvk/rtx_render/rtx_tone_mapping.cpp`. Grep:
```bash
grep -n "void DxvkToneMapping::dispatch(" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/rtx_render/rtx_tone_mapping.cpp"
```
Note the line number. Read ~40 lines starting there to see the existing dispatch structure (the function signature, early setup code, and the histogram/toneCurve/apply sequence).

### Step 4.3 — Insert HDR branch hook

- [ ] In `DxvkToneMapping::dispatch`, find the first line that does substantive work — typically the `ScopedGpuProfileZone(ctx, "Tone Mapping");` or the first `if`/`dispatchXxx` call. Insert the HDR branch ABOVE that, right after any required setup (like `m_resetState |= resetHistory;`). The insertion should read:

```cpp
    // W3 (HDR) fork hook: when HDR is enabled, bypass the SDR tonemap
    // pipeline entirely and dispatch the HDR processing shader instead.
    if (fork_hooks::isHDREnabled()) {
      fork_hooks::dispatchHDRProcessing(
        ctx, linearSampler, exposureView,
        rtOutput.m_finalOutput.resource(Resources::AccessType::Read),
        rtOutput.m_finalOutput.resource(Resources::AccessType::Write),
        ctx->getDevice()->getCurrentFrameId(),
        autoExposureEnabled);
      return;
    }
```

Important:
- Match gmod's insertion position — read gmod's `rtx_tone_mapping.cpp` around its `if (enableHDR())` (line ~533) to confirm where the branch lives relative to the existing setup code. Place the port's hook at the equivalent location.
- If the port's `DxvkToneMapping::dispatch` signature differs from gmod's (different param names, extra params), adjust the hook call accordingly but keep the read/write resource pair and frameId/autoExposure pattern.

- [ ] Add `#include "rtx_fork_hooks.h"` near the top of `rtx_tone_mapping.cpp` if it isn't already included. Grep to check:
```bash
grep -n "rtx_fork_hooks" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/rtx_render/rtx_tone_mapping.cpp"
```

### Step 4.4 — Update fork-touchpoints.md

- [ ] In `docs/fork-touchpoints.md`, under `## src/dxvk/rtx_render/rtx_tone_mapping.cpp`, upgrade the pending W3 entry seeded in commit 1 to reflect the landed state. Use format matching existing "Hook at" entries. Example:

```markdown
- **Hook** at `DxvkToneMapping::dispatch` (top of function) — 10-line block. Calls `fork_hooks::isHDREnabled()`; when true, `fork_hooks::dispatchHDRProcessing(...)` is called and the function returns, bypassing the SDR pipeline. Implementation in `rtx_fork_hdr.cpp`.
  *Replaces gmod's inline `if (enableHDR()) dispatchHDRProcessing(...)` with a fork-hook indirection so the entire HDR dispatch path stays in the fork-owned module.*
```

### Step 4.5 — Build

- [ ] Run the build command. Expected: success, no errors, no warnings.

### Step 4.6 — Manual smoke test

- [ ] Without running the game yet, verify the hook compiles in by grepping the built executable for the HDR shader name:
```bash
grep -c "tonemapping_hdr_processing" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/_Comp64Release/src/d3d9/d3d9.dll" 2>/dev/null || echo "shader reference present (binary grep is expected to find or not find, just confirm build completed)"
```

### Step 4.7 — Commit

```bash
cd "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" && git add src/dxvk/rtx_render/rtx_fork_hdr.cpp src/dxvk/rtx_render/rtx_tone_mapping.cpp docs/fork-touchpoints.md && git commit -m "$(cat <<'EOF'
fork(hdr): route HDR dispatch via fork hook (enables end-to-end HDR)

Adds fork_hooks::isHDREnabled (returns RtxForkHDR::enableHDR()) and
inserts the HDR branch at the top of DxvkToneMapping::dispatch:

  if (fork_hooks::isHDREnabled()) {
    fork_hooks::dispatchHDRProcessing(...);
    return;
  }

This is the W3 milestone commit: setting rtx.tonemap.enableHDR=True
in a dxvk.conf file now causes the HDR processing shader to run
end-to-end (PQ/HLG encoding, Rec.709->Rec.2020 gamut conversion,
ACES HDR tone mapping, tri-band grading, blue noise dithering).
No UI yet -- that lands in commits 6-7.

docs/fork-touchpoints.md updated to reflect the landed hook entry.

refs gmod <copy-SHA-here>
EOF
)"
```

Replace `<copy-SHA-here>` with gmod's SHA that added the HDR branch to `DxvkToneMapping::dispatch`. Find it:
```bash
git -C "c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod" log --oneline -S "if (enableHDR())" -- src/dxvk/rtx_render/rtx_tone_mapping.cpp | head -1
```

---

## Commit 5 — Composite HDR flag wiring

**Purpose:** Add `uint enableHDR;` to `CompositeArgs`, lock struct size with static_assert, implement `populateCompositeHDRArgs`, and call it from `rtx_composite.cpp` where `compositeArgs` is assembled.

**Files:**
- Modify: `src/dxvk/shaders/rtx/pass/composite/composite_args.h`
- Modify: `src/dxvk/rtx_render/rtx_fork_hdr.cpp` (add populate hook)
- Modify: `src/dxvk/rtx_render/rtx_composite.cpp` (call hook)
- Modify: `docs/fork-touchpoints.md`

### Step 5.1 — Read composite_args.h current state

- [ ] Read `src/dxvk/shaders/rtx/pass/composite/composite_args.h` in the port worktree. Note the full struct contents — field types, field order, and the last field's offset — to compute the new struct size after adding `enableHDR`.

- [ ] Read gmod's version for reference:
  - Path: `c:/Users/mystery/Projects/dx11_remix/dxvk-remix-gmod/src/dxvk/shaders/rtx/pass/composite/composite_args.h`

  Note gmod's exact position of `uint enableHDR;` within the struct — place port's field at the same position to match gmod's struct layout byte-for-byte (important if any shader references fields by offset).

### Step 5.2 — Add enableHDR field + static_assert

- [ ] In `src/dxvk/shaders/rtx/pass/composite/composite_args.h`:
  - Add `uint enableHDR;` at the position matching gmod's field placement.
  - Add `static_assert(sizeof(CompositeArgs) == <N>, "CompositeArgs size locked by fork(hdr) — update assert if upstream changes the struct");` immediately after the struct's closing `};`.

  `<N>` is computed from: current sizeof(CompositeArgs) + 4 (one uint) + any alignment padding introduced by the new field. If you're unsure of the exact number, set an initial value, build, read the compiler's error message (which will include the actual size), then update the assert to match.

### Step 5.3 — Implement populateCompositeHDRArgs

- [ ] In `src/dxvk/rtx_render/rtx_fork_hdr.cpp`, inside the `fork_hooks` namespace, add:

```cpp
    void populateCompositeHDRArgs(CompositeArgs& args) {
      args.enableHDR = isHDREnabled() ? 1u : 0u;
    }
```

### Step 5.4 — Find the CompositeArgs assembly site in rtx_composite.cpp

- [ ] Grep:
```bash
grep -n "CompositeArgs\s\+\w\+" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/rtx_render/rtx_composite.cpp" | head -5
```
Note the line where a `CompositeArgs` local is declared (the usual pattern is `CompositeArgs compositeArgs = {};` or similar). Find the block where individual fields are assigned.

### Step 5.5 — Insert hook call

- [ ] In that block, after all SDR-relevant fields are assigned but before the `writeToBuffer`/`pushConstants` call that pushes the struct to the GPU, insert:

```cpp
      // W3 (HDR): populate enableHDR flag via fork hook so HDR-aware
      // composite shader branches read the right value.
      fork_hooks::populateCompositeHDRArgs(compositeArgs);
```

- [ ] Ensure `#include "rtx_fork_hooks.h"` is present near the top of `rtx_composite.cpp`.

### Step 5.6 — Update fork-touchpoints.md

- [ ] Update the pending entries for `rtx_composite.cpp` and `composite_args.h` to reflect landed state. Use "Hook at ..." and "Inline tweak at ..." formats.

### Step 5.7 — Build

- [ ] Run the build. If the `static_assert` fails with an actual-size message, update `<N>` in step 5.2 to the compiler-reported size and rebuild. Expected: eventual success, no errors, no warnings.

### Step 5.8 — Commit

```bash
cd "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" && git add src/dxvk/shaders/rtx/pass/composite/composite_args.h src/dxvk/rtx_render/rtx_fork_hdr.cpp src/dxvk/rtx_render/rtx_composite.cpp docs/fork-touchpoints.md && git commit -m "$(cat <<'EOF'
fork(hdr): propagate enableHDR through composite args

CompositeArgs gains one uint enableHDR field (placed to match gmod's
struct layout exactly). Struct size is locked with a static_assert so
any accidental upstream change to CompositeArgs breaks the build loudly
rather than silently drifting.

fork_hooks::populateCompositeHDRArgs writes the flag from
fork_hooks::isHDREnabled(). Called from rtx_composite.cpp where
compositeArgs is assembled; HDR-aware branches in the composite
shader now see the right value.

refs gmod <copy-SHA-here>
EOF
)"
```

Replace `<copy-SHA-here>` with the gmod SHA that added `enableHDR` to `CompositeArgs`.

---

## Commit 6 — Auto-exposure HDR integration

**Purpose:** Wire the 4 `RtxForkHDRAutoExposure` options into the auto-exposure UI + dispatch. Implement `showHDRAutoExposureUI` and the inline param-resolution helper.

**Files:**
- Modify: `src/dxvk/rtx_render/rtx_fork_hdr.cpp`
- Modify: `src/dxvk/rtx_render/rtx_auto_exposure.cpp`
- Modify: `docs/fork-touchpoints.md`

### Step 6.1 — Audit gmod's auto-exposure HDR integration

- [ ] Read gmod's `rtx_auto_exposure.cpp` fully or search for `enableHDR` / `useHDRSpecificSettings` / `hdrAutoExposureSpeed` references. Note:
  - Where the HDR-specific UI section is rendered inside `showImguiSettings` (should be near the top).
  - Where the dispatch path reads HDR-specific auto-exposure params (speed, min, max) vs SDR params — identify the branch (likely `isHDREnabled() && useHDRSpecificSettings()` → HDR params; else → SDR params).
  - The exact parameter names and types.

### Step 6.2 — Implement showHDRAutoExposureUI

- [ ] In `src/dxvk/rtx_render/rtx_fork_hdr.cpp`, inside the `fork_hooks` namespace, add:

```cpp
    void showHDRAutoExposureUI() {
      if (!isHDREnabled()) {
        return;
      }

      ImGui::Text("HDR Auto Exposure Settings");
      RemixGui::Checkbox("Use HDR-Specific Settings",
                         &RtxForkHDRAutoExposure::useHDRSpecificSettingsObject());

      if (RtxForkHDRAutoExposure::useHDRSpecificSettings()) {
        ImGui::Indent();
        RemixGui::DragFloat("HDR Adaptation Speed",
                            &RtxForkHDRAutoExposure::hdrAutoExposureSpeedObject(),
                            0.01f, 0.1f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("HDR Min (EV100)",
                            &RtxForkHDRAutoExposure::hdrEvMinValueObject(),
                            0.1f, -6.0f, 0.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("HDR Max (EV100)",
                            &RtxForkHDRAutoExposure::hdrEvMaxValueObject(),
                            0.1f, 3.0f, 12.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Unindent();
        ImGui::Separator();
      } else {
        ImGui::Text("Using standard SDR settings with expanded range for HDR");
        ImGui::Separator();
      }
    }
```

The slider ranges above match gmod's rtx_auto_exposure.cpp lines 112-114.

### Step 6.3 — Insert hook call in rtx_auto_exposure.cpp

- [ ] Find `DxvkAutoExposure::showImguiSettings` in the port's `rtx_auto_exposure.cpp`:
```bash
grep -n "void DxvkAutoExposure::showImguiSettings" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/rtx_render/rtx_auto_exposure.cpp"
```

- [ ] At the top of that function body, before the existing SDR-specific controls, insert:

```cpp
      fork_hooks::showHDRAutoExposureUI();
```

Wrap surrounding SDR controls so they only render when HDR is NOT using HDR-specific settings. Pattern from gmod:

```cpp
      if (!fork_hooks::isHDREnabled() || !RtxForkHDRAutoExposure::useHDRSpecificSettings()) {
        // existing SDR speed / min / max sliders here, unchanged
      }
```

- [ ] For the DISPATCH side: find where `autoExposureSpeed` / `evMinValue` / `evMaxValue` are read in the auto-exposure dispatch (grep for these names in rtx_auto_exposure.cpp). At each read site, wrap with the HDR-aware resolution:

```cpp
      const float effectiveSpeed =
        (fork_hooks::isHDREnabled() && RtxForkHDRAutoExposure::useHDRSpecificSettings())
          ? RtxForkHDRAutoExposure::hdrAutoExposureSpeed()
          : autoExposureSpeed();
```

Repeat for `evMinValue` and `evMaxValue`. Use the resolved value downstream.

- [ ] Ensure `#include "rtx_fork_hooks.h"` and `#include "rtx_fork_hdr.h"` are present near the top of `rtx_auto_exposure.cpp`.

### Step 6.4 — Update fork-touchpoints.md

- [ ] Update the `rtx_auto_exposure.cpp` entry to reflect landed state.

### Step 6.5 — Build

- [ ] Run the build. Expected: success, no errors, no warnings.

### Step 6.6 — Commit

```bash
cd "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" && git add src/dxvk/rtx_render/rtx_fork_hdr.cpp src/dxvk/rtx_render/rtx_auto_exposure.cpp docs/fork-touchpoints.md && git commit -m "$(cat <<'EOF'
fork(hdr): add HDR auto-exposure param resolution + UI

fork_hooks::showHDRAutoExposureUI renders the HDR-specific auto-exposure
section at the top of DxvkAutoExposure::showImguiSettings when HDR is
enabled: a Use HDR-Specific Settings checkbox, and when enabled, three
sliders (HDR Adaptation Speed, HDR Min EV100, HDR Max EV100) matching
gmod's ranges exactly.

The auto-exposure dispatch path now reads HDR-tuned params when
isHDREnabled() AND useHDRSpecificSettings() are both true; otherwise
falls back to the standard SDR params. Pattern matches gmod.

refs gmod <copy-SHA-here>
EOF
)"
```

---

## Commit 7 — Main HDR UI collapsing header

**Purpose:** Implement `showHDRUI` (the big HDR CollapsingHeader contents) and add the header to `dxvk_imgui.cpp` under Post-Processing.

**Files:**
- Modify: `src/dxvk/rtx_render/rtx_fork_hdr.cpp`
- Modify: `src/dxvk/imgui/dxvk_imgui.cpp`
- Modify: `docs/fork-touchpoints.md`

### Step 7.1 — Audit gmod's HDR UI layout

- [ ] Read gmod's `dxvk_imgui.cpp` lines 4282-4325 for the canonical HDR UI layout. Note the exact slider labels, ranges, step sizes, format strings, and ImGui calls used. This is the reference for the port's `showHDRUI`.

### Step 7.2 — Implement showHDRUI

- [ ] In `src/dxvk/rtx_render/rtx_fork_hdr.cpp`, inside the `fork_hooks` namespace, add:

```cpp
    void showHDRUI() {
      RemixGui::Checkbox("Enable HDR Output", &RtxForkHDR::enableHDRObject());

      if (!RtxForkHDR::enableHDR()) {
        return;
      }

      ImGui::Indent();

      // HDR Format
      const char* hdrFormats = "Linear (Compatibility)\0PQ/HDR10 (Most Displays)\0HLG (Broadcast)\0\0";
      RemixGui::Combo("HDR Format", &RtxForkHDR::hdrFormatObject(), hdrFormats);
      ImGui::Separator();

      // HDR Tone Mapping section
      ImGui::Text("HDR Tone Mapping");
      const char* hdrToneMappers = "None (Linear)\0ACES HDR\0\0";
      RemixGui::Combo("HDR Tone Mapper", &RtxForkHDR::hdrToneMapperObject(), hdrToneMappers);
      RemixGui::Checkbox("Enable HDR Dithering", &RtxForkHDR::hdrEnableDitheringObject());
      if (RtxForkHDR::hdrEnableDithering()) {
        ImGui::Indent();
        RemixGui::DragFloat("Blue Noise Amplitude",
                            &RtxForkHDR::hdrBlueNoiseAmplitudeObject(),
                            0.01f, 1.0f, 40.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Multiplier for blue noise dithering strength.\n1.0 = optimal for reducing banding\n0.0 = no dithering\n>1.0 = stronger dithering for testing");
        }
        ImGui::Unindent();
      }
      ImGui::Separator();

      // HDR Brightness Controls
      ImGui::Text("HDR Brightness Controls");
      RemixGui::DragFloat("HDR Exposure Bias (EV)",
                          &RtxForkHDR::hdrExposureBiasObject(),
                          0.01f, -3.0f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("HDR Brightness",
                          &RtxForkHDR::hdrBrightnessObject(),
                          0.01f, 0.1f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::Separator();

      // HDR Color Grading
      ImGui::Text("HDR Color Grading");
      RemixGui::DragFloat("HDR Shadows",
                          &RtxForkHDR::hdrShadowsObject(),
                          0.01f, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("HDR Midtones",
                          &RtxForkHDR::hdrMidtonesObject(),
                          0.01f, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("HDR Highlights",
                          &RtxForkHDR::hdrHighlightsObject(),
                          0.01f, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::Separator();

      // Luminance reference values
      RemixGui::DragFloat("HDR Max Luminance (nits)",
                          &RtxForkHDR::hdrMaxLuminanceObject(),
                          10.0f, 100.0f, 10000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("HDR Min Luminance (nits)",
                          &RtxForkHDR::hdrMinLuminanceObject(),
                          0.001f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Paper White Luminance (nits)",
                          &RtxForkHDR::hdrPaperWhiteLuminanceObject(),
                          1.0f, 80.0f, 400.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);

      ImGui::Unindent();
    }
```

Tune labels, format strings, and ranges to match gmod's values exactly (cross-reference Step 7.1 notes).

### Step 7.3 — Add CollapsingHeader to dxvk_imgui.cpp

- [ ] Find `showRenderingSettings`'s Post-Processing section in port's `dxvk_imgui.cpp`:
```bash
grep -n "CollapsingHeader.*Post-Processing\|CollapsingHeader.*Auto Exposure\|CollapsingHeader.*Bloom" "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/src/dxvk/imgui/dxvk_imgui.cpp" | head -10
```

- [ ] Inside the Post-Processing indent block, BEFORE the Tonemapping CollapsingHeader (so HDR appears above Tonemapping in the menu — matching gmod's order), add:

```cpp
      if (RemixGui::CollapsingHeader("HDR", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        fork_hooks::showHDRUI();
        ImGui::Unindent();
      }
```

- [ ] Ensure `#include "rtx_fork_hooks.h"` is near the top of `dxvk_imgui.cpp` (should already be there from W2).

### Step 7.4 — Update fork-touchpoints.md

- [ ] Update `dxvk_imgui.cpp` entry for W3 to landed state. Example:

```markdown
- **Inline tweak** at `ImGUI::showRenderingSettings` Post-Processing section — 5-line addition for the HDR CollapsingHeader that wraps `fork_hooks::showHDRUI()`. Landed in commit 7 of Workstream 3.
```

### Step 7.5 — Build

- [ ] Run the build. Expected: success, no errors, no warnings.

### Step 7.6 — Commit

```bash
cd "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" && git add src/dxvk/rtx_render/rtx_fork_hdr.cpp src/dxvk/imgui/dxvk_imgui.cpp docs/fork-touchpoints.md && git commit -m "$(cat <<'EOF'
fork(hdr): add HDR dev-menu UI section

fork_hooks::showHDRUI renders the full HDR tuning surface: Enable
toggle, format combo (Linear/PQ/HLG), tone mapper combo (None/ACES
HDR), dithering + blue noise amplitude, exposure bias + brightness,
three-band color grading (shadows/midtones/highlights), and three
luminance reference sliders (max/min/paper-white in nits).

CollapsingHeader("HDR") added to dxvk_imgui.cpp inside the Post-
Processing section, placed above Tonemapping to match gmod's menu
order. Body calls the fork hook; all HDR UI logic lives in the
fork-owned module.

All W3 UI now exposed: users can toggle HDR on, pick a format, tune
all 13 options without editing a config file. Auto-exposure HDR
section from commit 6 appears automatically when HDR is on.

refs gmod <copy-SHA-here>
EOF
)"
```

---

## Commit 8 — Runtime validation + polish

**Purpose:** Run the HDR pipeline on Kim's HDR monitor, verify every RtxOption has the expected effect, catch any fixable issues.

**Files:**
- Commit any fix-up edits that surface during validation.

### Step 8.1 — Copy DLL to Skyrim

```bash
cp "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr/_Comp64Release/src/d3d9/d3d9.dll" "<path-to-Skyrim-install>/d3d9.dll"
```
Kim has the Skyrim install path from her notes.

### Step 8.2 — Enable Windows HDR mode

- [ ] In Windows Settings → System → Display → HDR, toggle HDR ON.

### Step 8.3 — Launch Skyrim + load an outdoor save

- [ ] Launch, load an outdoor save with visible sky + varied lighting (e.g., the mountain overlook she mentioned).

### Step 8.4 — Test HDR enable toggle

- [ ] Alt+X → Rendering → Post-Processing → HDR → Enable HDR Output.
- [ ] Verify image visibly changes (should appear significantly brighter + more contrasty if HDR is wired up correctly).
- [ ] If the image looks WASHED-OUT or WRONG, check: is Windows HDR still on? Is the monitor reporting HDR-ready? Those come before shader debugging.

### Step 8.5 — Cycle HDR Format

- [ ] With HDR enabled, cycle the "HDR Format" combo:
  - Linear → should look similar to SDR (no transfer-function encoding)
  - PQ/HDR10 → should render correctly on HDR10 displays; dark areas remain deep, highlights bright
  - HLG → should render correctly on HLG-capable displays
- [ ] If PQ looks wrong but HLG is OK (or vice versa), the shader's `convertColorSpace` branch for the broken format has an issue — cross-check against gmod's `hdr_processing.comp.slang` to confirm the port matches.

### Step 8.6 — Test HDR Tone Mapper

- [ ] Cycle "HDR Tone Mapper":
  - None (Linear) → direct passthrough, no tone mapping
  - ACES HDR → should apply a subtle ACES curve, lifting midtones

### Step 8.7 — Test Tri-band color grading

- [ ] With HDR on + ACES HDR tone mapper selected:
  - Push "HDR Shadows" to +1.0 → dark areas lift noticeably, midtones largely unchanged
  - Push "HDR Shadows" to -1.0 → dark areas crush darker
  - Reset to 0.0
  - Repeat for Midtones and Highlights
  - Verify the three bands are isolated — Shadows should NOT crush highlights, etc.

### Step 8.8 — Test Luminance controls

- [ ] Exercise "HDR Max Luminance (nits)" 100 → 1000 → 4000 → 10000. Verify HDR-format-specific behavior (PQ should respect the max; Linear/HLG respond differently).
- [ ] Exercise "Paper White Luminance (nits)" 80 → 100 → 200 → 400. Verify overall brightness scales.

### Step 8.9 — Test HDR Auto Exposure integration

- [ ] Expand Auto Exposure CollapsingHeader. Verify the "HDR Auto Exposure Settings" section shows when HDR is on.
- [ ] Toggle "Use HDR-Specific Settings". Move the camera between dark interior and bright outdoor. Adjust "HDR Adaptation Speed" and verify responsiveness changes.

### Step 8.10 — W2 regression test

- [ ] Disable HDR. Verify:
  - All 6 tonemap operators (None / ACES / ACES Legacy / Hable Filmic / AgX / Lottes) still work exactly as they did after W2.
  - Global / Local / Direct modes still switch correctly.
  - Hable preset buttons still apply their curves.

### Step 8.11 — Fix anything that broke

- [ ] If any step 8.5-8.10 surfaced a bug, fix it in the fork-owned code and build-verify. Track each fix as a separate atomic edit so the final commit message can summarize.

### Step 8.12 — Commit (even if empty)

If validation was clean and no fixes were needed, still commit a validation-complete marker:

```bash
cd "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" && git commit --allow-empty -m "$(cat <<'EOF'
fork(hdr): runtime validation complete on HDR monitor

Verified on Kim's HDR-capable monitor with Windows HDR mode enabled:

- HDR enable toggle produces the expected visible transition.
- All three HDR Formats render correctly (Linear / PQ / HLG).
- Both HDR Tone Mappers render correctly (None / ACES HDR).
- Tri-band color grading (Shadows / Midtones / Highlights) bands
  are properly isolated.
- Luminance controls (max / min / paper-white) produce expected
  brightness range behavior.
- HDR Auto Exposure integration responds correctly to the
  HDR-Specific Settings toggle + tunable speed/min/max.
- W2 regression clean: all 6 tonemap operators + Global/Local/Direct
  modes work unchanged when HDR is disabled.

W3 complete.
EOF
)"
```

If any fixes landed, bundle them into this commit (or into earlier atomic fix commits followed by this summary).

---

## Post-workstream handoff

After commit 8 lands, the branch is ready for Kim to open a PR. Do NOT push or open the PR — follow the same pattern as W2: Kim pushes to `kim2091` remote and opens the PR herself when she's ready.

When she says "push", the push command is:
```bash
git -C "c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix/.worktrees/unity-workstream-03-hdr" push -u kim2091 unity-workstream/03-hdr
```

---

## Self-review checklist (for plan author)

- [x] Spec coverage: every section of the W3 spec maps to at least one task here (scaffold → commit 1; 17 options → commit 2; shader → commit 3; dispatch → commit 4; composite → commit 5; auto-exposure → commit 6; UI → commit 7; validation → commit 8).
- [x] No placeholder text except the intentionally deferred defaults-from-gmod for auto-exposure options (explicitly structured as "read at step 2.1" rather than left blank).
- [x] Type consistency: `HDRFormat`, `HDRToneMapper`, `RtxForkHDR`, `RtxForkHDRAutoExposure`, `HDRProcessingShader`, `HDRProcessingArgs`, and all hook names used consistently across tasks.
- [x] Every file-touching step names the exact file path.
- [x] Every commit ends in a concrete `git add ... && git commit` command with HEREDOC message, and no AI co-author trailer.
- [x] Gmod SHA references are placeholders to be resolved at commit time (intentional — the exact SHAs are best looked up in gmod's log fresh rather than baked into a stale plan).
