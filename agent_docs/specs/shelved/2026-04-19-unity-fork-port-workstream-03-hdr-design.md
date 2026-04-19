# Unity-fork port — Workstream 3: HDR output

## Context

Workstream 3 of the unity-fork port ports gmod's HDR output feature. Workstream 2 (tonemap operators) has merged; W3 builds a parallel HDR output path that *replaces* the SDR tonemap dispatch when HDR is enabled.

The feature introduces:
- 17 RtxOptions for HDR tuning (13 core output options + 4 auto-exposure-specific)
- A fork-owned HDR processing compute shader with PQ (HDR10 / ST.2084) and HLG (BT.2100) transfer functions, Rec.709 → Rec.2020 gamut conversion, an ACES HDR tone mapper variant, tri-band log-luminance shadows/midtones/highlights grading, and blue noise dithering
- A new `CollapsingHeader` in the dev menu under Post-Processing
- HDR-aware branches in composite, auto-exposure, and global tonemap dispatch

**HDR entanglement with W2:** strictly additive. When HDR is off, the SDR operator pipeline from W2 runs unchanged. When HDR is on, the SDR pipeline (histogram + tone curve + apply-tonemapping) is replaced by a single HDR processing dispatch. The two paths do not interact; no W2 file is modified.

**Windows HDR dependency:** gmod's HDR implementation does NOT perform Vulkan HDR handshake (`VK_EXT_swapchain_colorspace` / `VK_EXT_hdr_metadata`). It emits PQ/HLG-encoded values into a standard SDR swapchain and relies on Windows HDR mode being enabled at the OS level to interpret them correctly. Port matches this exactly — documented as a known limitation; addressing it would be a future W3b-style scope expansion.

## Goal

Port gmod's HDR feature into the unity-fork port, shaped to the fork-touchpoint pattern from day one. Everything HDR-specific lives in a new fork-owned module (`rtx_fork_hdr.h/cpp`) and a new fork-owned shader (`hdr_processing.comp.slang`). Upstream files receive thin hook calls and small indexed inline tweaks, all catalogued in `docs/fork-touchpoints.md`.

## In scope

- **`RtxForkHDR` class** — 13 HDR RtxOptions (enable, format, tone mapper, dither, blue noise amplitude, exposure bias, brightness, 3 luminance values, 3 color grading sliders).
- **`RtxForkHDRAutoExposure` class** — 4 HDR-specific auto-exposure RtxOptions.
- **Two enums** — `HDRFormat` (Linear / PQ / HLG) and `HDRToneMapper` (None / ACES_HDR).
- **Fork-owned shader** `hdr_processing.comp.slang` — ported byte-for-byte from gmod. Encodes PQ and HLG transfer functions, performs Rec.709 → Rec.2020 gamut conversion, applies ACES HDR tone mapping, tri-band shadows/midtones/highlights grading, and blue noise dithering in a single compute pass.
- **HDR dispatch replacement** — one hook at the top of `DxvkToneMapping::dispatch()` that routes to the HDR shader when `isHDREnabled()` returns true.
- **Composite integration** — `enableHDR` flag propagated to `CompositeArgs` via a fork hook.
- **Auto-exposure integration** — HDR-specific auto-exposure parameters exposed; when `useHDRSpecificSettings` is true, the auto-exposure pass reads HDR-tuned range/speed values.
- **Dev-menu UI** — full HDR collapsing header under Post-Processing; HDR-specific auto-exposure section inside the Auto Exposure header.
- **Fork-touchpoint fridge-list entries** for every upstream file touched, in the same commit as the touch.

## Out of scope

- **Proper Vulkan HDR handshake** (`VK_EXT_swapchain_colorspace` / `VK_EXT_hdr_metadata`). Gmod's implementation does not negotiate HDR swapchain format; neither does ours. Deferred to a future workstream (W3b) if prioritized.
- **HDR support for the Local tonemapper path.** Gmod's HDR branch lives only on `DxvkToneMapping::dispatch` — `DxvkLocalToneMapping::dispatch` has no HDR branch. When `TonemappingMode::Local` is selected with HDR enabled, the HDR pipeline does NOT run. Matching gmod exactly.
- **Auto-detection of Windows HDR mode.** The feature relies on the user enabling Windows HDR separately; no detection, no warning.
- **Upstream PRs** — fork stays a fork.

## ABI / config contract

- 17 new RtxOptions added under `rtx.tonemap.*` and `rtx.autoExposure.*` namespaces. All default to SDR-equivalent behavior (`enableHDR = false`; 16 other options inert unless HDR is on).
- No existing RtxOption removals.
- `CompositeArgs` shader push-constant struct gains one `uint enableHDR;` field. Struct size change — locked with a `static_assert(sizeof(CompositeArgs) == N)` in the same commit.
- `HDRProcessingArgs` is a new shader struct; no conflict with existing structs.
- `remixapi_Interface` and all `remixapi_*` struct layouts **untouched**. No plugin ABI risk.

## Architecture

### Fork-owned files

| File | Contents |
|---|---|
| `src/dxvk/rtx_render/rtx_fork_hdr.h` | `HDRFormat` + `HDRToneMapper` enums. `RtxForkHDR` class (13 `RTX_OPTION`s). `RtxForkHDRAutoExposure` class (4 `RTX_OPTION`s). `HDRProcessingShader` class declaration. Hook function declarations. |
| `src/dxvk/rtx_render/rtx_fork_hdr.cpp` | `HDRProcessingShader` class registration. All hook implementations. HDR UI layout for main header and auto-exposure section. |
| `src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang` | Ported byte-for-byte from gmod. PQ/HLG transfer functions, Rec.709→Rec.2020 gamut, ACES HDR, tri-band grading, blue noise dithering. |

### Hook surface (additions to `rtx_fork_hooks.h`, `fork_hooks::` namespace)

| Hook | Call site (upstream) | Role |
|---|---|---|
| `isHDREnabled()` | `DxvkToneMapping::dispatch`, `RtxContext`, `dxvk_imgui.cpp`, `rtx_auto_exposure.cpp` | Returns `RtxForkHDR::enableHDR()`. Central query. |
| `dispatchHDRProcessing(ctx, sampler, exposureView, input, output, frameIdx, autoExpEnabled)` | `DxvkToneMapping::dispatch` (HDR branch) | Populates `HDRProcessingArgs` from `RtxForkHDR` options, binds resources, dispatches the HDR processing compute shader. |
| `populateCompositeHDRArgs(CompositeArgs&)` | `RtxContext::dispatchComposite` | Writes `args.enableHDR = isHDREnabled()`. |
| `showHDRUI()` | `ImGUI::showRenderingSettings` Post-Processing section | Renders the HDR `CollapsingHeader` contents (sliders, combos, toggles). |
| `showHDRAutoExposureUI()` | `DxvkAutoExposure::showImguiSettings` | Renders the HDR-specific auto-exposure UI section (only when HDR is enabled). |

### Upstream touchpoints (indexed in `docs/fork-touchpoints.md`)

| Upstream file | Touch shape | Approx size |
|---|---|---|
| `src/dxvk/rtx_render/rtx_tone_mapping.cpp` | Inline `if (fork_hooks::isHDREnabled()) { fork_hooks::dispatchHDRProcessing(...); return; }` branch at the top of `DxvkToneMapping::dispatch()`. | ~4 lines |
| `src/dxvk/rtx_render/rtx_composite.cpp` | Hook call `fork_hooks::populateCompositeHDRArgs(compositeArgs)` where `compositeArgs` is assembled. | ~2 lines |
| `src/dxvk/rtx_render/rtx_auto_exposure.cpp` | Hook call `fork_hooks::showHDRAutoExposureUI()` at the top of `showImguiSettings` + small inline reads for HDR-aware param selection in dispatch. | ~4-6 lines |
| `src/dxvk/imgui/dxvk_imgui.cpp` | New `CollapsingHeader("HDR")` inside Post-Processing section that calls `fork_hooks::showHDRUI()`. | ~4 lines |
| `src/dxvk/shaders/rtx/pass/composite/composite_args.h` | Add `uint enableHDR;` field + `static_assert(sizeof(CompositeArgs) == N)` in same commit. | ~2 lines |
| `src/dxvk/shaders/rtx/pass/tonemap/tonemapping.h` | Add `HDR_PROCESSING_*` binding index constants (4) + `HDRProcessingArgs` struct definition. | ~25 lines |
| `src/dxvk/meson.build` | Register `rtx_fork_hdr.cpp` + `hdr_processing.comp.slang`. | ~4 lines |

Total upstream footprint across ~7 files: **~45 lines of inline tweaks + 5 hook call sites**. Fork-owned code (the ~325-line shader + the new `.h/.cpp` module + HDR UI implementation) lives entirely outside upstream files.

## Components

### `RtxForkHDR` — 13 RtxOptions

| Option | Type | Default | Key |
|---|---|---|---|
| `enableHDR` | bool | `false` | `rtx.tonemap.enableHDR` |
| `hdrFormat` | `HDRFormat` | `PQ` | `rtx.tonemap.hdrFormat` |
| `hdrToneMapper` | `HDRToneMapper` | `None` | `rtx.tonemap.hdrToneMapper` |
| `hdrEnableDithering` | bool | `true` | `rtx.tonemap.hdrEnableDithering` |
| `hdrBlueNoiseAmplitude` | float | `20.0` | `rtx.tonemap.hdrBlueNoiseAmplitude` |
| `hdrExposureBias` | float | `0.0` | `rtx.tonemap.hdrExposureBias` |
| `hdrBrightness` | float | `1.0` | `rtx.tonemap.hdrBrightness` |
| `hdrMaxLuminance` | float | `1000.0` | `rtx.tonemap.hdrMaxLuminance` |
| `hdrMinLuminance` | float | `0.01` | `rtx.tonemap.hdrMinLuminance` |
| `hdrPaperWhiteLuminance` | float | `100.0` | `rtx.tonemap.hdrPaperWhiteLuminance` |
| `hdrShadows` | float | `0.0` | `rtx.tonemap.hdrShadows` |
| `hdrMidtones` | float | `0.0` | `rtx.tonemap.hdrMidtones` |
| `hdrHighlights` | float | `0.0` | `rtx.tonemap.hdrHighlights` |

### `RtxForkHDRAutoExposure` — 4 RtxOptions

| Option | Type | Default | Key |
|---|---|---|---|
| `useHDRSpecificSettings` | bool | `true` | `rtx.autoExposure.useHDRSpecificSettings` |
| `hdrAutoExposureSpeed` | float | `3.0` | `rtx.autoExposure.hdrAutoExposureSpeed` |
| `hdrEvMinValue` | float | `-2.0` | `rtx.autoExposure.hdrEvMinValue` |
| `hdrEvMaxValue` | float | `8.0` | `rtx.autoExposure.hdrEvMaxValue` |

*Defaults confirmed against gmod's `rtx_auto_exposure.h` during commit 2 implementation. Note: `useHDRSpecificSettings` defaults to `true` (not `false`) — meaning when HDR is enabled, HDR-specific auto-exposure params are active by default. This matches gmod's behavior.*

### Enums

```cpp
enum class HDRFormat      : uint32_t { Linear = 0, PQ = 1, HLG = 2 };
enum class HDRToneMapper  : uint32_t { None   = 0, ACES_HDR = 1 };
```

### `HDRProcessingShader` class

`ManagedShader`, `VK_SHADER_STAGE_COMPUTE_BIT`, shader source `tonemapping_hdr_processing`. Bindings as defined in `tonemapping.h` (blue noise texture, input/output color buffers, exposure input). Mirrors the structure of `ApplyTonemappingShader` in `rtx_tone_mapping.cpp`. Lives in `rtx_fork_hdr.cpp`.

### Shader-shared header additions

- **`tonemapping.h`** gains four new binding index constants — `HDR_PROCESSING_BLUE_NOISE_TEXTURE`, `HDR_PROCESSING_INPUT_BUFFER`, `HDR_PROCESSING_OUTPUT_BUFFER`, `HDR_PROCESSING_EXPOSURE_INPUT` — and the `HDRProcessingArgs` struct (15 fields matching gmod's definition byte-for-byte).
- **`composite_args.h`** gains one new field: `uint enableHDR;`. Locked with a `static_assert` on struct size.

### UI layout (main HDR header — inside Post-Processing)

```
▼ HDR
  ☐ Enable HDR Output
  [if enabled:]
    HDR Format: [Linear (Compatibility) | PQ/HDR10 (Most Displays) | HLG (Broadcast)]
    ────────────
    HDR Tone Mapping
      HDR Tone Mapper: [None (Linear) | ACES HDR]
      ☐ Enable HDR Dithering
        Blue Noise Amplitude: [slider, 1.0–40.0, "%.2f"]
    ────────────
    HDR Brightness Controls
      HDR Exposure Bias (EV): [slider, -3.0–20.0, "%.2f"]
      HDR Brightness:         [slider,  0.1–20.0, "%.2f"]
    ────────────
    HDR Color Grading
      HDR Shadows:    [slider, -1.0–1.0, "%.2f"]
      HDR Midtones:   [slider, -1.0–1.0, "%.2f"]
      HDR Highlights: [slider, -1.0–1.0, "%.2f"]
    ────────────
    HDR Max Luminance (nits):    [slider,  100–10000, "%.0f"]
    HDR Min Luminance (nits):    [slider,  0.0–1.0,   "%.3f"]
    Paper White Luminance (nits):[slider,  80–400,    "%.0f"]
```

### UI layout (auto-exposure HDR section — inside Auto Exposure header, prepended)

```
[if HDR enabled:]
  HDR Auto Exposure Settings
  ☐ Use HDR-Specific Settings
  [if useHDRSpecificSettings:]
    HDR Adaptation Speed:  [slider, 0.1–20.0, "%.2f"]
    HDR Min (EV100):       [slider, -6.0–0.0, "%.1f"]
    HDR Max (EV100):       [slider,  3.0–12.0,"%.1f"]
  ────────────
[always:]
  Average Mode: [Mean | Median]
  Adaptation Speed / Min / Max — only shown when NOT using HDR-specific
```

## Data flow

### HDR enabled path (per frame)

```
RtxOptions (RtxForkHDR::*)
  → populate HDRProcessingArgs (inside fork_hooks::dispatchHDRProcessing)
  → push constants
  → hdr_processing.comp.slang
    ├─ hdrToneMapping()              — exposure + ACES HDR (or bypass)
    ├─ exposureBias + brightness multiplier
    ├─ applyShadowMidtoneHighlight() — tri-band EV grading
    ├─ convertColorSpace()           — Rec.709→Rec.2020 + PQ or HLG encoding
    └─ ditherToHDR()                 — blue noise, post-encoding
  → Output color buffer (PQ/HLG-encoded values in SDR swapchain format)
  → Windows HDR mode interprets encoded values as HDR
```

### SDR path (when `enableHDR` is false)

Unchanged from W2: `dispatchHistogram` → `dispatchToneCurve` → `dispatchApplyToneMapping` with the operator dispatcher (Hable/AgX/Lottes/ACES/ACESLegacy/None) we built yesterday.

### Composite integration

`enableHDR` flag flows from `RtxForkHDR` → `fork_hooks::populateCompositeHDRArgs` → `CompositeArgs.enableHDR` → composite shader. The composite shader uses this flag for minor output-range adjustments; the shader-shared header change carries the field, composite shader logic lives in upstream files.

### Auto-exposure integration

`RtxForkHDRAutoExposure::useHDRSpecificSettings` — when true AND HDR is enabled, the auto-exposure pass reads `hdrAutoExposureSpeed` / `hdrEvMinValue` / `hdrEvMaxValue` instead of the standard SDR values. UI displays the HDR-specific section when HDR is enabled. Port's `rtx_auto_exposure.cpp` will gain a small inline helper (or fork hook) that resolves the right parameter based on `fork_hooks::isHDREnabled() && RtxForkHDRAutoExposure::useHDRSpecificSettings()`. Exact helper vs inline decision made during commit 6 based on what reads cleanest in the call site.

## Error handling

- **HDR without Windows HDR mode enabled:** image appears washed-out (PQ/HLG values displayed as if they were sRGB). Known footgun inherited from gmod; no detection, no warning. Documented in the `enableHDR` RtxOption docstring and in the HDR `CollapsingHeader` description text.
- **Shader compilation failure:** caught by DXVK's shader pipeline; surfaces at first HDR dispatch as a shader-missing log entry. No silent failure.
- **Struct size drift on `CompositeArgs`:** caught at compile time by `static_assert(sizeof(CompositeArgs) == N)`. Added in the same commit that adds the field; any upstream change to `CompositeArgs` breaks the build loudly rather than silently drifting.
- **HDR + Local tonemap mode combination:** HDR pipeline does not run (gmod parity). Behavior documented in the HDR section's description text.
- **Invalid `HDRFormat` enum value:** falls through to linear/identity in `convertColorSpace` switch (gmod behavior).
- **HDR enabled but no exposure data yet (first frames):** the shader's exposure factor path falls back to neutral (1.0); matches gmod's behavior on first-frame bring-up.

## Testing

Each commit MUST pass build-green verification before landing (no errors, no warnings). Runtime validation on Kim's HDR-capable monitor occurs at commit 8 (the validation-and-polish commit):

1. **Build verification per commit** — `PerformBuild -BuildFlavour release -BuildSubDir _Comp64Release -Backend ninja -EnableTracy false` returns exit 0.
2. **Shader compile verification** — slangc compiles `hdr_processing.comp.slang` without warnings or errors.
3. **Runtime validation at commit 8:**
   - Enable Windows HDR mode at the OS level.
   - Copy `_Comp64Release/src/d3d9/d3d9.dll` to Skyrim.
   - Launch, load an outdoor save.
   - Dev menu → Rendering → Post-Processing → HDR → Enable HDR Output.
   - Cycle HDR Format: verify Linear, PQ (HDR10), HLG each produce visually valid output.
   - Cycle HDR Tone Mapper: None vs ACES HDR — verify visible difference.
   - Exercise shadows/midtones/highlights sliders — verify each affects only its target luminance band (tri-band isolation).
   - Exercise PaperWhite / MaxLuminance sliders — verify brightness range behavior.
   - Toggle `useHDRSpecificSettings` + HDR-tuned auto-exposure speed/range — verify exposure adaptation behavior changes.
4. **W2 regression verification** — disable HDR, verify all 6 tonemap operators (None / ACES / ACES Legacy / Hable Filmic / AgX / Lottes) still work exactly as after W2.
5. **Tonemapping Mode regression** — verify Global / Local / Direct still switch correctly with HDR disabled.

## Code-migration approach

Eight commits on branch `unity-workstream/03-hdr`, each independently buildable and verified.

### Commit 1 — Scaffold HDR module

```
fork(hdr): scaffold rtx_fork_hdr module + enums + binding constants
```

- Create `src/dxvk/rtx_render/rtx_fork_hdr.h` and `.cpp` with `HDRFormat` + `HDRToneMapper` enums and empty class skeletons.
- Add the 5 new hook declarations to `rtx_fork_hooks.h` with doc comments matching existing style.
- Add `HDR_PROCESSING_*` binding constants to `tonemapping.h`.
- Register `rtx_fork_hdr.cpp` in `meson.build`.
- Seed `docs/fork-touchpoints.md` entries for the upstream files commits 2–8 will touch, each marked as "pending — landing in commit N."
- No upstream code changes. Building the port at this point produces zero behavior change.

### Commit 2 — RtxForkHDR + RtxForkHDRAutoExposure option classes

```
fork(hdr): add RtxForkHDR + RtxForkHDRAutoExposure options (refs gmod <SHA>)
```

- Implement all 13 `RTX_OPTION` declarations in `RtxForkHDR` with gmod defaults + docstrings (ranges included in docstring text).
- Implement all 4 `RTX_OPTION` declarations in `RtxForkHDRAutoExposure` — defaults read from gmod's `rtx_auto_exposure.h`.
- Add `HDRProcessingArgs` struct definition to `tonemapping.h` (byte-for-byte from gmod).
- No behavior change — options are exposed but unused.

### Commit 3 — Port HDR shader + dispatch plumbing

```
fork(hdr): port hdr_processing.comp.slang + dispatch plumbing (refs gmod <SHA>)
```

- Create `src/dxvk/shaders/rtx/pass/tonemap/hdr_processing.comp.slang`, copied byte-for-byte from gmod's version.
- Register shader in `meson.build`.
- Add `HDRProcessingShader` class registration in `rtx_fork_hdr.cpp` (mirror of `ApplyTonemappingShader`).
- Implement `fork_hooks::dispatchHDRProcessing` — populates `HDRProcessingArgs` from `RtxForkHDR` options, binds resources, dispatches the HDR processing compute shader.
- Shader is compiled but not yet wired into the main dispatch path. `enableHDR` option still has no runtime effect.

### Commit 4 — Wire HDR branch into `DxvkToneMapping::dispatch`

```
fork(hdr): route HDR dispatch via fork hook (refs gmod <SHA>)
```

- Implement `fork_hooks::isHDREnabled` (returns `RtxForkHDR::enableHDR()`).
- Add `if (fork_hooks::isHDREnabled()) { fork_hooks::dispatchHDRProcessing(...); return; }` at the top of `DxvkToneMapping::dispatch`.
- Fridge-list entry on `rtx_tone_mapping.cpp`.
- **Milestone commit:** HDR functionally working from config. Setting `rtx.tonemap.enableHDR=True` in a config file causes the HDR shader to run end-to-end. No UI yet.

### Commit 5 — Composite HDR flag wiring

```
fork(hdr): propagate enableHDR through composite args (refs gmod <SHA>)
```

- Add `uint enableHDR;` field to `CompositeArgs` in `composite_args.h`.
- Add `static_assert(sizeof(CompositeArgs) == N)` in the same commit.
- Implement `fork_hooks::populateCompositeHDRArgs`.
- Call it from `rtx_composite.cpp` where other `compositeArgs.*` fields are assembled.
- Fridge-list entries on `rtx_composite.cpp` and `composite_args.h`.

### Commit 6 — Auto-exposure HDR integration

```
fork(hdr): add HDR auto-exposure param resolution + UI (refs gmod <SHA>)
```

- Audit gmod's `rtx_auto_exposure.h` and `rtx_auto_exposure.cpp` — read defaults for the 4 HDR options, identify exact dispatch-side param reads.
- Implement `fork_hooks::showHDRAutoExposureUI` (the HDR-specific UI section).
- Inline hook call in `rtx_auto_exposure.cpp::showImguiSettings` — call `fork_hooks::showHDRAutoExposureUI()` at the top of the HDR-aware section.
- Inline small reads in the auto-exposure dispatch path (if gmod reads HDR params there) — match gmod's pattern.
- Fridge-list entry on `rtx_auto_exposure.cpp`.

### Commit 7 — Main HDR UI collapsing header

```
fork(hdr): add HDR dev-menu UI section (refs gmod <SHA>)
```

- Implement `fork_hooks::showHDRUI` — renders the UI layout documented under Components > UI layout.
- Add `CollapsingHeader("HDR")` in `dxvk_imgui.cpp` inside Post-Processing; the body calls `fork_hooks::showHDRUI()`.
- Fridge-list entry on `dxvk_imgui.cpp`.

### Commit 8 — Runtime validation + polish

```
fork(hdr): runtime validation fixes on HDR monitor (refs gmod <SHA>)
```

- Perform the full runtime validation checklist on Kim's HDR monitor (see Testing section).
- Commit any fixable issues surfaced during validation.
- May be a no-op commit if validation is clean.

## Open questions / known unknowns

- Exact defaults for the 4 auto-exposure HDR RtxOptions — pulled from gmod's source during commit 6 rather than fabricated.
- Whether `rtx_auto_exposure.cpp` dispatch needs inline HDR-param-resolution or can be factored into a single `fork_hooks::resolveAutoExposureParams(...)` returning a tuple. Decided during commit 6 based on what reads cleanest.
- Whether gmod's composite shader has HDR-aware logic beyond reading `enableHDR` — audited during commit 5; any additional inline tweaks added there.
- Whether gmod's HDR path has first-frame bring-up issues in scenarios port hits (e.g., the exposure buffer being uninitialized on first dispatch). Surfaces during commit 8 validation; fixes land in commit 8 if needed.
