# Fork maintainability — touchpoint-pattern refactor

## Context

The dxvk-remix port fork (`c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix`) carries ~60 commits on top of upstream fork-point `a8192ecd`, adding Unity-external-geometry submission, hardware-skinning fixes, batched mesh creation, texture categorization, persistent lights, ImGui state API, and more. Fork changes are currently interleaved directly inside upstream files, which produces two classes of pain observed during the 2026-04-18 debug session:

1. **Rebase pain.** Rebasing against upstream NVIDIA takes ~2 days because fork edits are scattered across upstream files. Every upstream release surfaces merge conflicts in files that hold mixed fork+upstream logic.
2. **ABI-drift bugs surface during that rebase chaos.** Bit-renumberings, vtable-slot shifts, and struct-layout changes sneak through the merge because cognitive load is saturated by upstream-churn merging. The three confirmed bugs fixed 2026-04-18 (see `project_abi_drift_pattern` memory) and two still-latent bugs (see `project_latent_particle_abi`, `project_latent_bit24_semantic` memories) are all of this class.

This spec addresses (1) directly. Addressing (1) is also expected to reduce (2) as a second-order effect — with less cognitive load burned on merge conflicts, silent ABI shifts are easier to spot during rebase review. The ABI-drift class of bugs is addressed directly by a follow-up spec (a compile-time ABI conformance harness, see "What comes next").

## Goal

Reduce rebase time against upstream from ~2 days to a few hours, and make the fork's surface area fully auditable, by:

1. Lifting substantive fork-specific logic out of upstream files into fork-owned files, with thin hooks in upstream calling into fork namespaces.
2. Maintaining a `docs/fork-touchpoints.md` index that lists every upstream file the fork touches, with what the touch is and what fork file/symbol it points to.
3. Adding a lightweight PR-template reminder to keep new fork contributions conforming to the pattern.

## In scope

- **Fork-owned files**: substantive fork logic migrates into dedicated files named with a clear prefix (e.g. `rtx_fork_*.cpp`, `rtx_unity_*.cpp` — exact prefix chosen during the scaffolding phase) so files are unambiguously identifiable as fork-owned vs upstream. Fork-owned files are exempt from upstream rebase conflicts by construction — upstream never touches them.
- **Thin hooks in upstream**: where fork logic must execute inside an upstream function, the in-upstream footprint shrinks to a namespaced one-line call (e.g. `fork_hooks::onExternalDraw(*this, cmd);`). The full logic lives in the fork-owned file.
- **Fridge list (`docs/fork-touchpoints.md`)**: a markdown index, sorted by upstream file path, listing every upstream file the fork touches, what the touch is (hook vs inline tweak), what fork file/symbol it points to (for hooks), and what the tweak's semantic intent is (for inline tweaks). Updated in the same commit as any fork edit to an upstream file.
- **Tiny inline tweaks retained, but indexed**: edits too small to merit their own fork file (single-line changes, typo fixes, 2–3-line additions in a function the fork logically shares with upstream) stay inline, but MUST have a fridge-list entry.
- **PR-template bullet**: `.github/PULL_REQUEST_TEMPLATE.md` gets a checklist item reminding contributors to update `fork-touchpoints.md` whenever a PR touches an upstream file. Opt-in discipline; no CI enforcement in this spec.

## Out of scope

- **ABI conformance harness**: the compile-time test binary that `static_assert`s vtable slot offsets, enum bit values, struct layouts, and `sType` values against the plugin's `remix_c.h`. Queued as the next spec after this one lands.
- **Plugin/port contract document**: codifying plugin-side constraints (e.g. "`SetConfigVariable` MUST NOT be called before plugin's `g_initialized` flips"). Queued third.
- **CI enforcement of the PR-template bullet**: deferred. Start with convention + review discipline; add a CI script if discipline slips.
- **Behavior changes**: this is a pure refactor. No fork feature gains, loses, or changes behavior.
- **Upstream PRs**: no attempt to upstream these changes back to NVIDIA. The fork remains a fork.
- **Shader-file fork-content migration**: shader-level fork changes (e.g. fork-added `.slang` content) follow the same principles but may require different mechanics than C++. If the audit identifies shader files that need similar treatment, shader migration becomes a follow-up spec.
- **Reorganization of already-fork-owned files**: this spec does not re-organize files that are already fork-owned (e.g. `rtx_remix_api.cpp`). It only moves fork logic OUT of upstream files into (new or existing) fork-owned files.

## Approach

### The three pieces

**Piece 1 — Fork-owned files.**

A file is "fork-owned" iff every line in it is fork-introduced. Fork-owned files:

- Are named with a clear prefix (`rtx_fork_*` or `rtx_unity_*`; exact convention chosen during scaffolding phase, informed by audit output).
- Live under `src/dxvk/rtx_render/` (or the appropriate upstream-equivalent folder for non-render code).
- Define their hook functions inside a `fork_hooks::` namespace (or a subsystem-specific namespace).
- Are exempt from rebase conflicts by construction — upstream never touches them.

Example sketch:

```cpp
// src/dxvk/rtx_render/rtx_unity_submit.cpp — fork-owned
namespace fork_hooks {
  void onExternalDraw(SceneManager& scene, const ExternalDrawCmd& cmd) {
    // All Unity-submission logic lifted from rtx_scene_manager.cpp lives here.
    ...
  }
}
```

**Piece 2 — Thin hooks in upstream.**

Where upstream code must call fork logic, the in-upstream footprint is a namespaced one-line call:

```cpp
// src/dxvk/rtx_render/rtx_scene_manager.cpp — upstream file
void SceneManager::submitDraw(const DrawCmd& cmd) {
  fork_hooks::onExternalDraw(*this, cmd);  // ← touchpoint
  // ... rest of NVIDIA's original function, untouched ...
}
```

The upstream file's rebase-conflict surface on this function shrinks from N lines of inline fork logic to 1 line. When upstream rewrites the function, conflict resolution becomes trivial: re-insert the one hook line at the correct location inside the new function body.

**Piece 3 — Fridge list (`docs/fork-touchpoints.md`).**

Markdown index, sorted by upstream file path, with two entry types per file section:

- **Hook entries**: `Hook at <symbol> (<position>) → <fork_namespace::symbol> in <fork_file>` + one-line intent comment.
- **Inline-tweak entries**: `Inline tweak at <symbol> (~line N) — <N-line> addition for <purpose>` + one-line intent comment.

Example:

```markdown
## src/dxvk/rtx_render/rtx_scene_manager.cpp

- **Hook** at `SceneManager::submitDraw` (top) → `fork_hooks::onExternalDraw` in `rtx_unity_submit.cpp`
  *Routes externally-submitted Unity geometry through the fork API path.*

- **Inline tweak** at `SceneManager::updateLights` (~line 340) — 2-line addition for `LEGACY_EMISSIVE` category bit mapping.
  *Too small to merit its own file; tracked here so rebase spots it.*
```

Every edit to an upstream file MUST have a fridge-list entry. Migration commits (moving inline fork logic into fork-owned files) update the fridge list in the same commit.

### Optional guardrail — PR-template bullet

Add one bullet to `.github/PULL_REQUEST_TEMPLATE.md`:

```markdown
- [ ] If this PR touches an upstream file, I updated `docs/fork-touchpoints.md`.
```

Low-effort, review-visible. No CI enforcement in this spec. If convention slips over time, a follow-up task can add a CI check that greps each PR's diff for upstream files and verifies each has a matching fridge-list entry.

## Rollout

Four phases. Each phase has a clear output and stop-point. No phase is a big-bang cutover.

| Phase | Activity | Output | Behavior change |
|---|---|---|---|
| **1. Audit** | Walk every fork commit on top of fork-point `a8192ecd`. For each file touched in each commit, categorize the edit as *"big block → migrate"* or *"tiny inline → index-only"*. | First draft of `docs/fork-touchpoints.md`, plus a prioritized migration queue (upstream files sorted by fork-footprint size descending) | None — read-only recon |
| **2. Scaffolding** | Create the `fork_hooks::` namespace header; commit the audit-generated `docs/fork-touchpoints.md`; add the PR-template bullet; pick final fork-file naming prefix | Empty scaffolding ready to receive migrations | None — plumbing only |
| **3. Migration** | One upstream file at a time, highest-footprint first: lift big fork blocks into a fork-owned file (new if needed), replace the inline logic with a thin hook call, update `fork-touchpoints.md`. Test after each file. | Each commit migrates exactly one file; self-contained and revertible | Pure refactor — behavior identical |
| **4. Verification** | Once migration is "done enough," execute a test rebase against current upstream main. Measure time spent on merge conflicts and compare to prior rebase duration. | Measurement numbers documented in the plan's closeout section | None — measurement only |

### Sequencing principles

- **One file per commit during migration.** Each migration commit is minimal, revertible, and reviewable in isolation.
- **Highest-footprint upstream files first.** Maximize felt-win per unit of effort; keep momentum high.
- **Stop points at every phase boundary.** The work is pausable at any phase boundary without leaving the tree in a broken state.
- **Audit output guides migration order.** The audit produces a prioritized queue; migration follows that queue.

### What counts as "done enough" for phase 4

Migration is "done enough" when:

- All upstream files with a fork-footprint > ~20 LOC have been migrated to hook-based form.
- All remaining upstream edits (small tweaks < ~20 LOC) are indexed in `fork-touchpoints.md`.
- No fork-introduced code remains in an upstream file without either a hook-migration plan or a fridge-list entry.

## Validation

### Success criteria

| Signal | Measured by |
|---|---|
| Rebase time dropped | Phase-4 test rebase takes < 1/4 the time of the most recent pre-migration rebase (user-reported ~2 days → target < 12 hours) |
| Fork surface fully auditable | `docs/fork-touchpoints.md` contains an entry for every upstream file the fork touches; audit phase output reviewed against the fridge list |
| No hidden edits | Informal check at audit-phase completion: `git diff <upstream-fork-point>` against upstream files shows nothing that isn't referenced in `fork-touchpoints.md` |
| Discipline holds | PR-template bullet present; first 3 PRs after plan lands either follow the pattern or are corrected in review |

### Regression checks (during migration phase 3)

Each per-file migration commit must:

- Build clean (`meson compile -C build-release`).
- Run the Skyrim Remix plugin without behavior regressions — spot-check rendering in an outdoor Skyrim cell after each migration.
- Preserve existing Workstream 1 (API + HW skinning) and Workstream 5 (Hillaire atmosphere) functionality.

## Git layout

- **Spec branch**: `unity-port-planning` (this document lives here, alongside existing workstream specs/plans).
- **Execution branch**: chosen during plan-writing — likely a new branch `unity-fork-touchpoint-refactor` off the current port integration tip.
- **Worktree**: likely a new `.worktrees/unity-fork-touchpoint-refactor/` during execution (see `using-git-worktrees` skill).
- **Merge target**: the port's unity integration branch, same target as existing workstreams.
- **Commit authorship**: user only. No AI co-author trailers on any commit in this work, per user's durable rule.

## Known risks

1. **Audit mis-categorization** — an edit is marked "tiny inline" during audit when it should have been "big block migrate" (or vice versa). *Mitigation:* the categorization is not load-bearing in phase 3. If an "inline" edit turns out to warrant migration during rebase experience, it can be re-categorized and migrated in a follow-up commit. The fridge list is designed to accommodate both shapes.

2. **Fork-owned file naming convention churn** — picking a naming prefix (`rtx_fork_*` vs `rtx_unity_*`) that later proves suboptimal. *Mitigation:* decide during scaffolding phase after audit output reveals the natural groupings; convention is a rename-refactor away at any time.

3. **Hook-call overhead** — introducing one-line hook calls may add marginal indirection. *Mitigation:* all hooks are direct function calls into fork-owned code; no virtual dispatch, no dynamic lookup. Overhead is zero or near-zero after inlining. Inner-loop hot paths (per-ray, per-pixel) get extra scrutiny during migration to confirm no regression.

4. **Upstream rewrites the function where a hook lives** — the hook call vanishes if upstream deletes or renames the containing function. *Mitigation:* this is exactly the case the fridge list catches. During rebase, a missing hook surfaces as "the function the hook was in doesn't exist in new upstream"; the fridge list documents what the hook was doing so the resolver decides where the hook lives in the new upstream structure.

5. **Shader-file fork content** — some fork changes live in `.slang` files (e.g. gbuffer shaders, surface material headers). The hook-in-upstream pattern may not translate directly to shader-side fork content. *Mitigation:* audit phase flags shader files separately; if meaningful shader-side migration is warranted, it becomes a follow-up spec rather than inflating this one.

6. **Large-scale refactor drag** — 60+ commits worth of fork logic may take longer to migrate than the 2-day rebase pain it replaces. *Mitigation:* sequencing principle #2 (highest-footprint first) ensures early-stage migrations produce the largest felt wins. User can stop at any phase boundary; partial migration still reduces rebase pain proportionally to what's migrated.

7. **Conflation with active workstreams** — the port has several workstream branches in flight (`unity-workstream/01-api-skinning`, `unity-workstream/05-hillaire-atmosphere`). Migrating fork logic in one branch while other workstreams edit the same files creates merge conflicts between the workstreams. *Mitigation:* execute the refactor on a dedicated branch off the current integration tip; coordinate with workstream merges so the refactor lands either before or after each workstream's merge window, not mid-flight.

## What comes next

After this spec's plan ships and migration is verified (phase 4 measurement confirms rebase-time reduction):

1. **ABI conformance harness** (user's original leverage-point #1). Build a small test binary in the port repo that compiles against a reference copy of the plugin's `remix_c.h` (from `c:/Users/mystery/Projects/dx11_remix/Skyrim/Skyrim_Remix/extern/remix/remix_c.h`) and uses `static_assert` to verify:
   - vtable slot offsets in `remixapi_Interface`
   - enum bit values (e.g. `REMIXAPI_INSTANCE_CATEGORY_BIT_LEGACY_EMISSIVE == (1u << 24)`)
   - struct layouts and sizes for all `remixapi_*EXT` structs
   - `sType` enum values

   Catches the silent ABI-drift class of bugs at compile time. Made easier to build by this spec: the harness can target the C API boundary cleanly, which by then lives in known fork-owned files + the public header rather than interleaved with upstream code.

2. **Plugin/port contract document** (user's original leverage-point #2). Living `docs/plugin-port-contract.md` codifying plugin-side assumptions such as:
   - "Plugin MUST NOT call `SetConfigVariable` before its `g_initialized` flag flips." (See plugin commit `e8dee5a` for the canonical gating pattern.)
   - "Options registered via `RTX_OPTION` inline statics are available at DLL-load time."
   - Thread-safety guarantees per API function.
   - Init-order contract.

   Lowest urgency of the three leverage points; most valuable when plugin-side devs need a clear reference for correct API usage.
