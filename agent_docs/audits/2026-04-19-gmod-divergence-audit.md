# Gmod divergence audit — 2026-04-19

**Purpose:** enumerate gmod-side work that has genuinely unity-native
origin and is not yet present in the port, so future workstreams can
be planned with confidence in scope.

**Result:** three items. One correctness hazard, one trivial log
cleanup, one capture-safety guard that complements an existing
upstream fix.

> **Note on methodology.** This audit went through two false starts
> in-session before landing correctly:
> 1. First pass used `origin/gmod-ex` as the gmod baseline because of
>    its `HEAD ->` alias. Wrong — this port tracks `origin/unity`.
> 2. Second pass listed features without checking port main / W1 /
>    W2 / W5 branches for semantic coverage, producing false positives
>    (RCAS, sleep-light, hw-skinning fix, imgui remix api wrapper all
>    appeared to be missing but are actually in place).
>
> Lessons captured in
> `~/.claude/projects/.../memory/project_gmod_audit_baseline.md` so
> future sessions start from the right baseline and verify against
> port state before listing.

---

## 1. Methodology

### 1.1 Fork points and scope

- Unity fork-point with upstream: `a8192ecd` (2025-09-19, "Merge
  `REMIX-4605-graphics-preset-fix` into main")
- Unity tip: `4ce38d1b` (2026-04-01, "fix hw skinning for unity")
- Unity commits since fork: **60** (+10 809 / −560 LOC, 114 files)
- Port main tip: upstream `17d74001` (2026-04-14)
- Port workstream tips: W1 `831afd42`, W2 `9642a6b0`, W5 `664a9ba4`
- W3 HDR: shelved 2026-04-19 per gmod HDR author's confirmation
  that the upstream implementation is broken.

### 1.2 Filters applied

1. **Shared with gmod-ex — drop.** The port is a Unity-targeted fork.
   If a feature exists on both `origin/unity` AND `origin/gmod-ex`
   (confirmed by matching distinctive-code markers on both branches),
   it came from shared upstream development, not from unity-specific
   work. Those features don't qualify as unity-native and don't belong
   in this backlog.
2. **Already in port — drop.** Every candidate was grepped with
   `git grep -l -F "<marker>"` across all four port branches (main,
   W1, W2, W5). Markers were chosen per commit from the actual diff
   to avoid substring false positives (initial pass was fooled by
   substring matches on `rcas` — 15 substring hits, zero
   word-boundary hits).
3. **Intentionally deprecated — call out, don't backlog.** Port's W2
   and W5 branches contain explicit "Deprecated feature" comments
   for the occluder shader logic and decals-on-sky logic. These were
   removed by deliberate design decision; reversing the removal is a
   design question, not a port task.
4. **Out of scope — drop.** Port-direction conflicts (Option Layers,
   Eyes/Input/Remix-Logic menu), gmod-specific infrastructure
   (branding, build scripts, CI), and gmod-game-specific shader
   defaults.

### 1.3 Verification tooling

- Marker-grep across all four port branches.
- Opus sub-agent dispatched for three deep content-diff checks: the
  sky-fixes cluster (`1711109d`), the imgui remix api wrapper
  (`23fc439c`), and the six-commit Dec-2025 API fix cluster. Sub-agent
  confirmed the imgui wrapper is byte-equivalent in W1 modulo two
  em-dash→hyphen swaps in comments, and produced the per-commit
  breakdown that Dec-2025 4-of-6 are already absorbed into W1.

---

## 2. The backlog

Three items. Ordered by recommendation.

### Item 1 — `c195b10a` `RasterGeometry::externalMesh` field (⚠️ correctness hazard)

**Source:** `origin/unity`, 2025-12-09, "fix unity remix api replacements".

**What's missing on port:** W1 ported the capturer-side hash-selection
branch that reads `externalMesh`, but the FIELD ITSELF is not on
`RasterGeometry` in W1's `rtx_types.h` (only `externalMaterial`
exists there). Consequently:
- The `submesh.externalMesh = handle` assignment in
  `registerExternalMesh` is missing.
- The `replacementDrawCall` / `submeshes[0]` refinement in
  `submitExternalDraw` is missing.

**Why it matters:** API-submitted meshes fall back to the D3D9 hash
during capture, breaking replacement-lookup parity between
capture-time and runtime. This is a latent ABI correctness issue —
captures silently produce the wrong hash for API meshes, meaning a
scene captured from a running game and then replayed cannot resolve
its API-submitted meshes to their replacement assets.

**Files touched:**
- `src/dxvk/rtx_render/rtx_types.h` (field addition on `RasterGeometry`)
- `src/dxvk/rtx_render/rtx_asset_replacer.cpp` (`registerExternalMesh` body)
- `src/dxvk/rtx_render/rtx_scene_manager.cpp` (`submitExternalDraw` refinement)

**Rebase pain:** **MEDIUM.** `rtx_scene_manager.cpp` is high-churn
upstream; conflicts likely on future rebases. The `RasterGeometry`
field addition is additive and replays cleanly even if upstream
reorders the struct — worst case is re-inserting the field in the
new ordering.

### Item 2 — `f145c380` silence two spammy logs

**Source:** `origin/unity`, 2025-12-10, "silence some logs that spam
way too often".

**What the diff does (2 LOC total):**
- `src/d3d9/d3d9_swapchain.cpp:1389` — comments out the
  `throw DxvkError("D3D9SwapChainEx: Failed to recreate swap chain")`.
- `src/dxvk/rtx_render/rtx_asset_replacer.cpp:191` — comments out
  the `Logger::info("Ignoring repeated mesh registration ...")`
  inside `registerExternalMesh`.

**Verified unity-only:** gmod-ex has `Logger::info` active at line
194 (untouched); unity has it commented out.

**Rebase pain:** **LOW.** Two-line comment-outs replay cleanly even
when surrounding code moves. **Suggestion:** consider promoting to a
log-level `RtxOption` instead of hard-commenting — cleaner long-term
and drops rebase cost to zero.

### Item 3 — `83e89dbc` capture crash guard (null-imageView safety)

**Source:** `origin/unity`, 2025-12-10, "fix capture crash issue and
update splash".

**What the safety fix does:** adds `nullptr` + `image().ptr()` guards
plus `try/catch` wrappers around texture-export calls in
`rtx_game_capturer.cpp`, for **both** the D3D9 path and the Remix
API path. Previously an unguarded export on an invalid imageView
would crash the whole capture process; with the guard it logs a
warning and skips the texture.

**The Remix API path specifically protects Unity capture flows** —
Unity games submitting textures through the Remix C API hit this
code when a user captures a scene, and transient or partially-
initialized imageView states trigger the guard. Not Source-engine-
specific; the defensive null-checking is generic.

**Complementary to existing upstream fix:** port main already has
`3207ed3b [REMIX-4657] Prevent capture crash on write-protected
export path` from pkristof. That fix addresses a different scenario
(filesystem errors on write-protected export paths). Unity's fix
addresses null-imageView / invalid-texture-state crashes. Two
independent safety nets, non-overlapping.

**Scope for port — skip the splash tweak:** the commit also includes
a 4-LOC update to `src/dxvk/imgui/dxvk_imgui_splash.cpp` that's
gmod-specific splash branding. Port only the capturer changes.

**Files to port:**
- `src/dxvk/rtx_render/rtx_game_capturer.cpp` (~+67 LOC — guards)
- `src/dxvk/rtx_render/rtx_asset_exporter.cpp` (~+16 LOC)

**Files to skip:**
- `src/dxvk/imgui/dxvk_imgui_splash.cpp` (gmod branding)

**Rebase pain:** **MEDIUM.** `rtx_game_capturer.cpp` has moderate
upstream churn, but the null-guards are isolated and additive.

---

## 3. What was filtered out

### 3.1 Shared with gmod-ex (not unity-native)

Features present on both `origin/unity` AND `origin/gmod-ex` with
matching markers — shared development, not unity-specific. Listed
for reference so future audits don't re-surface them:

- Legacy emissives (`1e37214b` on unity, `7419b58d` on gmod-ex)
- USD-material null-guard (`d4e1d1a2` — `val.IsHolding<type>`)
- rtxio mounting fix (`a82b8bca` / `4ffd946c` — "Global dedupe")
- RDNA4 indirect-traceray default (`1b0da37c` / `e2e881a5`)
- Sleep-light optimization (`f1a86eff`) — also already in port W1
- XeSS 2.1 SR — also already in port main via upstream
- USD loading overhaul (`c80f8210` / `dc92be9b` async variant)
- Occluder + decals-on-sky — on both gmod branches but intentionally
  removed from port (see §3.3)
- Alpha-channel texture-stage mask (`95a0043a`)

### 3.2 Already in port (unity-native but shipped)

Marker-verified present in the port:

- `23fc439c` imgui remix api wrapper — byte-equivalent in W1 modulo
  two em-dash→hyphen comment swaps.
- `4ce38d1b` hw skinning unity fix (`prototype.skinningData.computeHash()`
  present in W1/W2/W5 `rtx_remix_api.cpp`).
- `0f4e6abb` / `f567ad8b` / `b3b0e284` / `3bce7e55` — four of the six
  Dec-2025 API fixes cleanly absorbed into W1 with log spam trimmed.
  Sub-agent characterized this as "a careful port, not a blind
  cherry-pick".
- `1a67dadb` `applyCategory` pattern — present in W1 with 13 of 14
  categories. The missing category is `LegacyEmissive`, which pairs
  with the (filtered-out) legacy-emissives feature — if that is ever
  revisited, this one-liner ships with it.

### 3.3 Intentionally deprecated — design question, not backlog

W2 and W5 branches contain explicit comments at
`src/dxvk/shaders/rtx/algorithm/geometry_resolver.slangh`
lines 1564 / 1657 / 2314:

```
// NOTE: Deprecated feature - isOccluder property was removed from Surface struct
```

The `enableDecalsOnSky` option has the same treatment. Re-adding
either of these reverses a deliberate removal decision and is a
design question. Evidence is already in-tree; uncommenting plus
re-adding the `isOccluder` field to `Surface` would restore them
in roughly half a day if the original removal reason no longer
applies.

### 3.4 Sky fixes — out of scope for this port

`1711109d` "add sky fixes" is genuinely unity-native (marker-verified
0 hits on gmod-ex; sub-agent confirmed via `git branch --contains`
that it never cherry-picked across). However:

- It addresses vertex-explosion bugs specific to Source-engine-style
  3D skyboxes (dollhouse-with-diorama rendering).
- Unity games use cubemap-based skyboxes drawn at infinite distance,
  which don't exhibit the reprojection-misclassification scenario
  the fix protects against.
- Rebase cost is **HIGH** (+170 LOC inline in `rtx_sky.h`, +148 LOC
  inline in `rtx_context.cpp`, one of the highest-churn upstream
  files).

Defer unless the port's target games extend to Source-engine titles,
or unless a touchpoint-module refactor of `rtx_sky` makes the port
cheap to execute as a side-effect.

### 3.5 Out of scope (port-direction conflicts and gmod infra)

- **UI theme / color / UX cluster** (xoxor4d commits, July–August
  2025) — collides with the port's Eyes / Input / Remix-Logic menu
  direction.
- **`b88e1cfd` / `52c97733`** — gmod-game-specific force-baked-lighting
  shader defaults.
- **`a0878a5c`** rtx.conf priority reorder — conflicts with the
  Option Layers system.
- **Gmod splash / build scripts / CI workflow** — gmod-specific
  infrastructure.

---

## 4. Open questions

- **Q1.** Port Item 2 (`f145c380`) as a direct comment-out replay, or
  promote to a log-level `RtxOption`? Comment-out is 2 LOC and
  immediate; `RtxOption` is cleaner, drops rebase cost to zero, and
  reduces future drift.
- **Q2.** Is the port's scene-capture flow actually being exercised
  by users or developers? Item 3 (`83e89dbc`) only matters if capture
  is on the critical path. If captures aren't being used,
  deprioritize.
- **Q3.** Occluder + decals-on-sky deprecation (§3.3): reverse the
  removal, or honor it? Requires a design-level decision on *why*
  they were originally removed.

---

*Audit generated 2026-04-19. Unity tip `4ce38d1b` (2026-04-01). Port
tip `ad9443e8` on `unity-port-planning`. Gmod reference repo was
read-only throughout; no commits or pushes were made to it.*
