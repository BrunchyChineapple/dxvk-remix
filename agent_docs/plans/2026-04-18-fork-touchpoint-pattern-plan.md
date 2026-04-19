# Fork Touchpoint-Pattern Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the dxvk-remix port fork's scattered inline upstream edits into fork-owned files with thin hooks, backed by a `docs/fork-touchpoints.md` audit index, reducing upstream rebase time from ~2 days to a few hours.

**Architecture:** Four-phase execution — (1) Audit: walk fork commits, categorize each upstream edit as "big block migrate" or "tiny inline index"; (2) Scaffolding: create `fork_hooks::` namespace header + fridge list skeleton + PR-template bullet; (3) Migration: one upstream file per commit, highest-LOC-first, lifting fork blocks into dedicated fork-owned files with thin hook calls; (4) Verification: do a test rebase on a throwaway worktree and measure the time reduction.

**Tech Stack:** git (worktrees, rebase), bash (audit scripting), C++ (meson build, namespaces), markdown (fridge list), GitHub PR template.

**Commit authorship:** All commits in this plan author as the user only. **No AI co-author trailers on any commit.**

---

## File Structure

### Created by this plan

- `docs/fork-touchpoints.md` — Sorted-by-upstream-file index of all fork touchpoints. Authoritative inventory, updated with every migration commit.
- `src/dxvk/rtx_render/rtx_fork_hooks.h` — Header declaring the `fork_hooks::` namespace and forward-declaring hook entry functions. Each hook's implementation lives in a dedicated fork-owned `.cpp` file.
- `scripts/audit-fork-touchpoints.sh` — Audit automation script. Walks fork commits, aggregates per-file LOC, emits a prioritized migration queue. Kept for future re-use.
- `.github/PULL_REQUEST_TEMPLATE.md` — Create if not present. Add the fridge-list-update checkbox.
- `docs/superpowers/rebase-measurement-2026-04-18.md` — Phase 4 time measurement record.
- Per-migration: `src/dxvk/rtx_render/rtx_unity_*.cpp` and/or `src/dxvk/rtx_render/rtx_fork_*.cpp` files (one per migrated subsystem of fork logic). Created during Phase 3.

### Modified by this plan

- `src/dxvk/meson.build` — source list updated as each new fork-owned `.cpp` is added.
- Various upstream files under `src/dxvk/rtx_render/` and `src/dxvk/shaders/` — touched incrementally during Phase 3, one per commit. Each modification replaces inline fork logic with a thin hook call + updates the fridge list.

### Ephemeral (not committed)

- `audit-output/fork-file-inventory.tsv`, `audit-output/upstream-file-migration-queue.tsv` — audit script outputs, used as working state during Phases 1–3. Add `audit-output/` to `.gitignore` in Task 1.3 Step 4.

---

## Phase 1: Audit

Phase 1 produces: (a) the first-draft `docs/fork-touchpoints.md`, (b) the prioritized migration queue (TSV), (c) the audit script kept in the repo. No source-code changes — read-only recon + docs creation.

### Task 1.1: Set up dedicated worktree

**Files:**
- Worktree created at: `.worktrees/unity-fork-touchpoint-refactor/`
- Branch created: `unity-fork-touchpoint-refactor`

- [ ] **Step 1: Verify current state of port repo**

Run from port repo root (`c:/Users/mystery/Projects/dxvk-unity-new/dxvk-remix`):

```bash
git fetch --all
git branch -a | grep -E "unity|workstream|main|master"
git log --oneline -10
```

Expected: see existing workstream branches. Identify the current port integration tip. Per project memory this is `unity-workstream/05-hillaire-atmosphere` at SHA `664a9ba4`, but confirm against actual repo state before proceeding.

- [ ] **Step 2: Create worktree and branch**

Substitute `<INTEGRATION_TIP_REF>` with the branch/SHA confirmed in Step 1.

```bash
git worktree add -b unity-fork-touchpoint-refactor .worktrees/unity-fork-touchpoint-refactor <INTEGRATION_TIP_REF>
```

Expected: worktree created; new branch checked out inside it.

- [ ] **Step 3: Enter the worktree**

All remaining tasks run from inside the worktree:

```bash
cd .worktrees/unity-fork-touchpoint-refactor
```

### Task 1.2: Identify upstream fork-point and remote

**Files:**
- Reference only.

- [ ] **Step 1: Confirm upstream remote exists**

```bash
git remote -v | grep -i nvidia
```

If no remote points at NVIDIA's upstream dxvk-remix, add one:

```bash
git remote add upstream https://github.com/NVIDIAGameWorks/dxvk-remix
git fetch upstream
```

- [ ] **Step 2: Confirm upstream fork-point SHA**

Per project memory, the fork branches off upstream at `a8192ecd`. Confirm:

```bash
git log --oneline a8192ecd -1
```

If that SHA isn't reachable or isn't on upstream's history, find the actual fork-point:

```bash
git merge-base unity-fork-touchpoint-refactor upstream/main
```

- [ ] **Step 3: Export fork-point SHA for script use**

```bash
export FORK_POINT=a8192ecd   # replace with the SHA confirmed in Step 2
echo "FORK_POINT=$FORK_POINT"
```

### Task 1.3: Create audit script

**Files:**
- Create: `scripts/audit-fork-touchpoints.sh`
- Modify: `.gitignore` (add `audit-output/`)

- [ ] **Step 1: Create `scripts/` directory if not present**

```bash
mkdir -p scripts
```

- [ ] **Step 2: Write the audit script**

Create `scripts/audit-fork-touchpoints.sh` with this content:

```bash
#!/usr/bin/env bash
# audit-fork-touchpoints.sh
#
# Walks fork commits on top of the upstream fork-point and aggregates
# per-file fork LOC. Emits:
#   - <out>/fork-file-inventory.tsv       : all files touched by fork,
#     with added/removed LOC and whether the file existed at FORK_POINT.
#   - <out>/upstream-file-migration-queue.tsv : subset limited to files
#     that existed at FORK_POINT (i.e. upstream files), sorted by added
#     LOC descending.
#
# Usage:
#   FORK_POINT=<sha> ./scripts/audit-fork-touchpoints.sh [output-dir]

set -euo pipefail

if [[ -z "${FORK_POINT:-}" ]]; then
  echo "ERROR: FORK_POINT environment variable must be set." >&2
  exit 1
fi

OUT_DIR="${1:-audit-output}"
mkdir -p "$OUT_DIR"

INVENTORY="$OUT_DIR/fork-file-inventory.tsv"
QUEUE="$OUT_DIR/upstream-file-migration-queue.tsv"
TMP="$OUT_DIR/.inventory.tmp"

echo -e "file\tadded\tremoved\tupstream_at_fork_point" > "$INVENTORY"

# Aggregate per-file add/remove across all fork commits.
git log --format="%H" "${FORK_POINT}..HEAD" | while read -r SHA; do
  git show --numstat --format="" "$SHA"
done | awk -F'\t' '
  NF == 3 && $1 != "-" && $2 != "-" {
    added[$3]   += $1
    removed[$3] += $2
  }
  END {
    for (f in added) printf "%s\t%d\t%d\n", f, added[f], removed[f]
  }
' | sort -t $'\t' -k2,2 -nr > "$TMP"

# Mark upstream-at-fork-point vs fork-introduced.
while IFS=$'\t' read -r file added removed; do
  if git cat-file -e "${FORK_POINT}:${file}" 2>/dev/null; then
    upstream="yes"
  else
    upstream="no"
  fi
  printf "%s\t%s\t%s\t%s\n" "$file" "$added" "$removed" "$upstream"
done < "$TMP" >> "$INVENTORY"

rm "$TMP"

# Emit upstream-only subset sorted by added desc.
{
  head -n 1 "$INVENTORY"
  tail -n +2 "$INVENTORY" | awk -F'\t' '$4 == "yes"' | sort -t $'\t' -k2,2 -nr
} > "$QUEUE"

echo "Audit output:"
echo "  $INVENTORY"
echo "  $QUEUE"
```

- [ ] **Step 3: Make script executable**

```bash
chmod +x scripts/audit-fork-touchpoints.sh
```

- [ ] **Step 4: Add `audit-output/` to `.gitignore`**

Append to `.gitignore`:

```
# Fork-maintainability audit outputs (ephemeral).
audit-output/
```

If `.gitignore` already has this pattern, skip.

### Task 1.4: Run the audit

**Files:**
- Output (ephemeral): `audit-output/fork-file-inventory.tsv`, `audit-output/upstream-file-migration-queue.tsv`

- [ ] **Step 1: Run the audit script**

```bash
FORK_POINT=$FORK_POINT ./scripts/audit-fork-touchpoints.sh audit-output
```

Expected: script prints the two output paths; both files exist and are non-empty.

- [ ] **Step 2: Inspect the top of the migration queue**

```bash
head -20 audit-output/upstream-file-migration-queue.tsv
```

Expected: TSV header followed by upstream files with the largest fork footprint first.

- [ ] **Step 3: Count files by migration category**

```bash
awk -F'\t' '
  NR > 1 && $2 >  20 {big++}
  NR > 1 && $2 >   0 && $2 <= 20 {small++}
  END {
    print "big_blocks (to migrate): " (big ? big : 0)
    print "tiny_tweaks (index only): " (small ? small : 0)
  }
' audit-output/upstream-file-migration-queue.tsv
```

Expected: output like `big_blocks (to migrate): N ; tiny_tweaks (index only): M`. N tells the implementer roughly how many migration commits Phase 3 will need.

### Task 1.5: Write first-draft `docs/fork-touchpoints.md`

**Files:**
- Create: `docs/fork-touchpoints.md`

- [ ] **Step 1: Create the fridge-list skeleton**

Create `docs/fork-touchpoints.md`:

```markdown
# Fork touchpoints

This index lists every upstream file the fork touches. It is the authoritative
inventory of fork-vs-upstream surface area, maintained as fork edits are added
or removed.

See `docs/superpowers/specs/2026-04-18-fork-touchpoint-pattern-design.md` for
the design this index supports.

## Entry types

- **Hook** — upstream file contains a one-line call into fork-owned code. The
  fork logic itself lives in the fork-owned file referenced by the entry.
- **Inline tweak** — upstream file contains a small fork-introduced change
  (typically <= 20 LOC) that was not worth lifting into its own fork file.

## Upstream files touched by the fork

<!-- Entries are sorted by upstream file path. -->
```

- [ ] **Step 2: Generate a starter section for each upstream file**

For each row in `audit-output/upstream-file-migration-queue.tsv` with `added > 0`, append a section to `docs/fork-touchpoints.md` with this shape:

```markdown
## <upstream-file-path>

**Pre-refactor fork footprint:** +<added> / -<removed> LOC (audit 2026-04-18)

**Category:** <migrate | index-only>

<!-- Populate one entry per contiguous fork-introduced block in this file. -->
- **[pending]** describe each fork touchpoint here
```

Category is `migrate` if `added > 20`, else `index-only`.

A shell helper to generate the skeleton sections:

```bash
awk -F'\t' '
  NR > 1 && $2 > 0 {
    cat = ($2 > 20) ? "migrate" : "index-only"
    printf "\n## %s\n\n", $1
    printf "**Pre-refactor fork footprint:** +%s / -%s LOC (audit 2026-04-18)\n\n", $2, $3
    printf "**Category:** %s\n\n", cat
    printf "<!-- Populate one entry per contiguous fork-introduced block in this file. -->\n"
    printf "- **[pending]** describe each fork touchpoint here\n"
  }
' audit-output/upstream-file-migration-queue.tsv >> docs/fork-touchpoints.md
```

- [ ] **Step 3: Populate each section by reading the fork diff for that file**

This is the main human effort of Phase 1. For each upstream file with a section:

```bash
TARGET=<upstream-file-path>
git log -p $FORK_POINT..HEAD -- "$TARGET"
```

Read the diff end-to-end. Identify each contiguous fork-introduced block. Replace the `- **[pending]** describe each fork touchpoint here` line with one entry per block, using one of the two entry shapes:

For migrate-category files (blocks that will get lifted in Phase 3):

```markdown
- **Block** at `<symbol>` (<position within function, e.g. "after lock acquisition">) — +<N> LOC, planned target `fork_hooks::<proposed-name>` in `rtx_unity_<subsystem>.cpp`.
  *<one-line intent: what the block does semantically>*
```

For index-only files (blocks that stay inline):

```markdown
- **Inline tweak** at `<symbol>` (~line <N>) — <L>-line <addition|modification> for <purpose>.
  *<one-line intent>*
```

- [ ] **Step 4: Validate the fridge list against the audit**

Cross-check that every upstream file in the queue has a section, and every section's file is in the queue:

```bash
# Files in the migration queue but missing from the fridge list.
awk -F'\t' 'NR > 1 && $2 > 0 {print $1}' audit-output/upstream-file-migration-queue.tsv | \
  while read f; do
    grep -qF "## $f" docs/fork-touchpoints.md || echo "MISSING in fridge list: $f"
  done

# Files in the fridge list but missing from the queue.
grep -E "^## src/" docs/fork-touchpoints.md | sed 's/^## //' | \
  while read f; do
    awk -F'\t' -v f="$f" 'NR > 1 && $1 == f {found=1} END {exit !found}' \
      audit-output/upstream-file-migration-queue.tsv || echo "MISSING in queue: $f"
  done
```

Expected: both checks print nothing.

- [ ] **Step 5: Remove any remaining `[pending]` placeholders**

```bash
grep -n '\[pending\]' docs/fork-touchpoints.md || echo "no pending entries — good"
```

Expected: `no pending entries — good`. If any `[pending]` line remains, return to Step 3 for that file.

### Task 1.6: Commit Phase 1 output

**Files:**
- Staged: `docs/fork-touchpoints.md`, `scripts/audit-fork-touchpoints.sh`, `.gitignore`

- [ ] **Step 1: Review staged changeset**

```bash
git status
git diff --stat
```

Expected: three new or modified files; no changes to source code.

- [ ] **Step 2: Commit**

```bash
git add docs/fork-touchpoints.md scripts/audit-fork-touchpoints.sh .gitignore
git commit -m "Add fork-touchpoints audit output and automation script

Phase 1 of the fork touchpoint-pattern refactor. The docs/fork-touchpoints.md
index records every upstream file the fork touches today, categorized as
'migrate' (>20 LOC; get lifted to fork-owned files in Phase 3) or
'index-only' (<=20 LOC; stay inline but tracked here).

The audit script is kept in the repo for future re-use: re-running it after
the refactor's Migration phase verifies zero upstream files remain with
untracked fork edits."
```

- [ ] **Step 3: Verify commit has no AI co-author trailer**

```bash
git log -1 --format=full
```

Expected: `Author:` line only; no `Co-Authored-By:` trailer.

---

## Phase 2: Scaffolding

Phase 2 produces: (a) `fork_hooks::` namespace header, (b) PR-template bullet, (c) fork-owned-file naming convention recorded in the fridge list. No fork logic is migrated yet.

### Task 2.1: Decide naming convention for fork-owned files

**Files:**
- Read: `audit-output/upstream-file-migration-queue.tsv`
- Modify: `docs/fork-touchpoints.md` (add a "Conventions" section)

- [ ] **Step 1: Review the migration queue groupings**

```bash
awk -F'\t' 'NR > 1 && $2 > 20 {print $1}' audit-output/upstream-file-migration-queue.tsv
```

Expected: list of upstream files that will get migrated. Observe natural groupings (Unity-specific, lighting, texture, skinning, etc.).

- [ ] **Step 2: Pick the naming convention**

Default convention (matches the spec):

- `rtx_unity_*` — logic specific to Unity-engine external geometry submission.
- `rtx_fork_*` — other fork-specific logic not tied to Unity (hardware-skinning fixes, persistent lights, texture categorization, etc.).

If the audit reveals groupings that don't fit this split, adjust — but keep the prefix scheme so fork-owned files are visually distinct from upstream files.

- [ ] **Step 3: Record the decision in `docs/fork-touchpoints.md`**

Insert this section between the top-level intro and the `## Entry types` section of `docs/fork-touchpoints.md`:

```markdown
## Conventions

### Fork-owned file naming

- Files prefixed `rtx_unity_*` contain logic specific to Unity-engine
  external geometry submission.
- Files prefixed `rtx_fork_*` contain other fork-specific logic not tied
  to Unity (hardware-skinning fixes, persistent lights, texture
  categorization, ImGui state API, etc.).
- All fork-owned files live under `src/dxvk/rtx_render/` (or the
  subsystem-appropriate equivalent directory).
- Hook functions are declared in the `fork_hooks::` namespace
  (`src/dxvk/rtx_render/rtx_fork_hooks.h`) and implemented in their
  respective fork-owned `.cpp` files.
```

### Task 2.2: Create the `fork_hooks::` namespace header

**Files:**
- Create: `src/dxvk/rtx_render/rtx_fork_hooks.h`

- [ ] **Step 1: Create the header**

Create `src/dxvk/rtx_render/rtx_fork_hooks.h`:

```cpp
#pragma once

// rtx_fork_hooks.h — declarations for the fork-owned hook functions that
// upstream files call into. Each hook's implementation lives in a dedicated
// rtx_fork_*.cpp or rtx_unity_*.cpp file, keeping upstream files' fork
// footprint to one-line call sites only.
//
// See docs/fork-touchpoints.md for the index of every hook and which
// upstream file calls it.

namespace fork_hooks {

  // Hook declarations are added here during Phase 3 migration, as each
  // upstream file's inline fork logic is lifted into a fork-owned
  // implementation file.

}
```

- [ ] **Step 2: Verify the header parses under the current compile**

Temporarily include the header from one existing translation unit. Edit `src/dxvk/rtx_render/rtx_remix_api.cpp` and add `#include "rtx_fork_hooks.h"` near the top of its existing includes. Then:

```bash
meson compile -C build-release
```

Expected: clean compile with no new warnings.

- [ ] **Step 3: Revert the temporary include**

```bash
git checkout -- src/dxvk/rtx_render/rtx_remix_api.cpp
```

### Task 2.3: Register the new header with the build system if required

**Files:**
- Possibly modify: `src/dxvk/meson.build`

- [ ] **Step 1: Locate the meson declaration for `rtx_render/` sources**

```bash
grep -n "rtx_render/" src/dxvk/meson.build | head -20
```

Expected: find the source list or `files(...)` call that picks up `rtx_render/*.cpp`.

- [ ] **Step 2: Inspect whether headers are listed explicitly**

```bash
grep -n "rtx_render/.*\.h" src/dxvk/meson.build | head -5
```

If the build file lists headers (for install targets, tooling, or code-model generation), add `'rtx_render/rtx_fork_hooks.h'` to the appropriate list in the same shape as existing entries. If headers are picked up implicitly, no change needed.

- [ ] **Step 3: Verify build is clean**

```bash
meson compile -C build-release
```

Expected: clean compile.

### Task 2.4: Add the PR-template bullet

**Files:**
- Create or modify: `.github/PULL_REQUEST_TEMPLATE.md`

- [ ] **Step 1: Check whether `.github/PULL_REQUEST_TEMPLATE.md` exists**

```bash
ls .github/PULL_REQUEST_TEMPLATE.md 2>&1 || echo "DOES_NOT_EXIST"
```

- [ ] **Step 2a: If it exists, append the bullet**

Open `.github/PULL_REQUEST_TEMPLATE.md` and append to the existing checklist section (or create a `## Checklist` section if none):

```markdown
- [ ] If this PR touches an upstream file, I updated `docs/fork-touchpoints.md`.
```

- [ ] **Step 2b: If it does not exist, create a minimal template**

```bash
mkdir -p .github
```

Create `.github/PULL_REQUEST_TEMPLATE.md`:

```markdown
## Summary

<!-- What this PR does, in 1-2 sentences. -->

## Checklist

- [ ] If this PR touches an upstream file, I updated `docs/fork-touchpoints.md`.
```

### Task 2.5: Commit Phase 2 scaffolding

**Files:**
- Staged: `src/dxvk/rtx_render/rtx_fork_hooks.h`, `.github/PULL_REQUEST_TEMPLATE.md`, `docs/fork-touchpoints.md` (Conventions section), possibly `src/dxvk/meson.build`.

- [ ] **Step 1: Stage and verify**

```bash
git add src/dxvk/rtx_render/rtx_fork_hooks.h \
        .github/PULL_REQUEST_TEMPLATE.md \
        docs/fork-touchpoints.md
git add src/dxvk/meson.build 2>/dev/null || true
git status
```

- [ ] **Step 2: Commit**

```bash
git commit -m "Add fork_hooks namespace scaffolding and PR-template reminder

Phase 2 of the fork touchpoint-pattern refactor. Creates the empty
fork_hooks:: namespace in a new header, adds a PR-template bullet
reminding contributors to keep docs/fork-touchpoints.md in sync when
editing upstream files, and records the fork-owned-file naming
convention for Phase 3 migration (rtx_unity_* for Unity-specific,
rtx_fork_* for other fork logic)."
```

- [ ] **Step 3: Verify no AI co-author trailer**

```bash
git log -1 --format=full
```

---

## Phase 3: Migration

Phase 3 is iterative. The executor applies the **migration template (Task 3.T)** once per upstream file with `added > 20` LOC in `audit-output/upstream-file-migration-queue.tsv`, working from the top of the queue (highest LOC first) downward. After all migrate-category files are done, Task 3.INDEX finalizes the remaining tiny-tweak entries.

### Task 3.T (Template): Migrate one upstream file

Apply this full template once per iteration. Each iteration produces one commit.

**Files (per iteration):**
- Modify: one target upstream file (the iteration's target), e.g. `src/dxvk/rtx_render/rtx_scene_manager.cpp`
- Create: one fork-owned file, e.g. `src/dxvk/rtx_render/rtx_unity_submit.cpp`
- Modify: `src/dxvk/rtx_render/rtx_fork_hooks.h` (add declaration for the new hook)
- Modify: `src/dxvk/meson.build` (add the new `.cpp` to the source list)
- Modify: `docs/fork-touchpoints.md` (update the target file's section from pre-refactor description to post-refactor hook entry)

- [ ] **Step 1: Identify target file and read existing fork diff**

Pick the top unmigrated entry with `added > 20` from `audit-output/upstream-file-migration-queue.tsv`. Track progress manually by striking through migrated entries or by maintaining a `migrated.log` file.

```bash
TARGET=<upstream-file-path>    # e.g. src/dxvk/rtx_render/rtx_scene_manager.cpp
git log -p "$FORK_POINT..HEAD" -- "$TARGET"
```

Read the full diff. Identify the fork-introduced block(s). If multiple independent blocks exist in one file, each becomes a separate hook call (but still one migration commit per file).

- [ ] **Step 2: Decide fork-owned file name and hook symbol**

Based on the target file's subsystem and the naming convention (from the Conventions section of `docs/fork-touchpoints.md`), choose:

- Fork-owned file path, e.g. `src/dxvk/rtx_render/rtx_unity_submit.cpp`
- Hook function symbol, e.g. `fork_hooks::onExternalDraw`
- Hook signature — match the call site's existing context (what state/data the lifted block needs access to)

Record the choices as text to feed into subsequent steps.

- [ ] **Step 3: Add the hook declaration to `rtx_fork_hooks.h` (test-first shape)**

Edit `src/dxvk/rtx_render/rtx_fork_hooks.h`. Inside `namespace fork_hooks { ... }`, add:

```cpp
  // Hook for <one-line intent>. Implementation in <fork-owned-file>.
  void <hook-symbol>(<args>);
```

Example (Unity external-draw migration):

```cpp
  // Hook for routing Unity-submitted external geometry through the fork's
  // submission pipeline. Implementation in rtx_unity_submit.cpp.
  void onExternalDraw(SceneManager& scene, const ExternalDrawCmd& cmd);
```

- [ ] **Step 4: Run the build — expected to link-fail**

```bash
meson compile -C build-release
```

Expected: **link failure** referencing the new hook symbol (because no `.cpp` yet defines it). This is the test failure — it confirms the build system is aware of the new declaration and requires a matching implementation.

If the build passes here, something is off (the header change isn't picked up). Investigate before continuing.

- [ ] **Step 5: Create the fork-owned implementation file**

Create `<fork-owned-file>` (e.g. `src/dxvk/rtx_render/rtx_unity_submit.cpp`). Paste the lifted fork block(s) from the target upstream file, wrapped in the `fork_hooks::` namespace.

Template:

```cpp
// <fork-owned-file>
//
// Fork-owned file. Contains the implementation of fork_hooks::<hook-symbol>,
// lifted from <target-upstream-file> during the 2026-04-18 fork
// touchpoint-pattern refactor.
//
// See docs/fork-touchpoints.md for the catalogue of fork hooks.

#include "rtx_fork_hooks.h"
// Add only the headers the lifted code actually uses. Removing any that
// were imported solely for the (now-absent) upstream-file context.

namespace fork_hooks {

  void <hook-symbol>(<args>) {
    // Paste the lifted fork block here verbatim. Keep types, variable
    // names, and control flow identical to the source so behavior is
    // preserved exactly.
  }

}
```

- [ ] **Step 6: Register the new file with meson**

Locate where `rtx_render/*.cpp` files are listed:

```bash
grep -n "rtx_remix_api.cpp\|rtx_scene_manager.cpp" src/dxvk/meson.build
```

Add the new file to the same list, alphabetically sorted next to its siblings:

```meson
  'rtx_render/<new-fork-owned-file-basename>',
```

- [ ] **Step 7: Replace the target upstream file's inline block with the hook call**

Edit the target upstream file:

1. Delete the lifted fork block exactly as it was identified in Step 1 — remove nothing else.
2. In its place, insert the hook call at the same location:

```cpp
fork_hooks::<hook-symbol>(<matching-args>);
```

3. Add `#include "rtx_fork_hooks.h"` to the file's includes if not already present (alphabetize with existing rtx_* includes).
4. Remove any includes that were present only to support the lifted block (they now live in the fork-owned file).

- [ ] **Step 8: Build clean**

```bash
meson compile -C build-release
```

Expected: build succeeds, no new warnings, no link errors.

- [ ] **Step 9: Runtime smoke test**

Deploy and smoke-test against Skyrim Remix:

```bash
cp build-release/src/d3d9/d3d9.dll "/d/SteamLibrary/steamapps/common/Skyrim Special Edition/d3d9.dll"
```

Launch Skyrim, load a save. Verify:
- Scene renders (no white-window regression).
- Physical sky / Hillaire atmosphere still works (Workstream 5 feature intact).
- The fork feature served by the migrated file behaves identically to pre-migration (for example, if migrating the Unity-submission path, Unity-submitted geometry still renders; if migrating skinning, skinned meshes still animate).

If any regression: revert the migration commit (`git reset --hard HEAD~1` before committing) and investigate. Do not commit a broken migration.

- [ ] **Step 10: Update `docs/fork-touchpoints.md` for the target file**

Find the section for the target file. Replace the pre-migration description with the post-migration hook entry:

```markdown
## <target-upstream-file>

**Pre-refactor footprint:** +<N> / -<M> LOC (migrated 2026-04-18)
**Post-refactor footprint:** 1 hook call + 1 #include

- **Hook** at `<containing-symbol>` (<position within function>) →
  `fork_hooks::<hook-symbol>` in `<fork-owned-file>`
  *<one-line intent>*
```

If the file had additional inline tweaks not migrated in this iteration, preserve those entries unchanged.

- [ ] **Step 11: Commit**

```bash
git add "$TARGET" \
        "<new-fork-owned-file>" \
        src/dxvk/rtx_render/rtx_fork_hooks.h \
        src/dxvk/meson.build \
        docs/fork-touchpoints.md
git status
git commit -m "Migrate <target-basename> fork block to <new-fork-owned-basename>

Part of the fork touchpoint-pattern refactor. Lifts the fork-introduced
<N>-LOC block from <target-upstream-file>::<function> into a dedicated
fork-owned file, leaving only a one-line fork_hooks::<hook-symbol> call
in the upstream file. Pure refactor — behavior identical.

Updates docs/fork-touchpoints.md with the new hook entry."
```

- [ ] **Step 12: Verify commit authorship**

```bash
git log -1 --format=full
```

Expected: user is sole author; no `Co-Authored-By:` trailer.

- [ ] **Step 13: Advance to next queue entry**

Return to Step 1 with the next unmigrated file with `added > 20` in the queue. Continue until the queue is exhausted or the plan stops at a clean boundary.

Between iterations, the tree is always in a buildable and deployable state — each migration commit is self-contained.

### Task 3.INDEX: Finalize tiny-inline entries

**Files:**
- Modify: `docs/fork-touchpoints.md`

For upstream files with `added <= 20` LOC (tiny tweaks), no code migration happens — but the fridge-list entries must be complete.

- [ ] **Step 1: For each tiny-tweak file, confirm its fridge-list entry is final**

```bash
awk -F'\t' 'NR > 1 && $2 > 0 && $2 <= 20 {print $1}' \
  audit-output/upstream-file-migration-queue.tsv | while read f; do
    if ! grep -qF "## $f" docs/fork-touchpoints.md; then
      echo "MISSING section: $f"
    elif grep -A 20 "^## $f" docs/fork-touchpoints.md | grep -q '\[pending\]'; then
      echo "UNFINISHED entry: $f"
    fi
  done
```

Expected: no output. If any file is flagged, open the fridge list and complete its section (pattern from Task 1.5 Step 3).

- [ ] **Step 2: Commit if any fridge-list entries were finalized**

```bash
git diff docs/fork-touchpoints.md
git add docs/fork-touchpoints.md
git commit -m "Complete fork-touchpoints entries for remaining inline tweaks

Adds/finalizes docs/fork-touchpoints.md entries for upstream files with
fork-introduced changes below the 20-LOC threshold. These changes stay
inline (not migrated to fork-owned files); the fridge list records
their location and intent so rebase review catches them."
```

If no diff, skip the commit.

### Task 3.VERIFY: Confirm audit script agrees with post-migration state

**Files:**
- Read-only.

- [ ] **Step 1: Re-run the audit script**

```bash
FORK_POINT=$FORK_POINT ./scripts/audit-fork-touchpoints.sh audit-output-post
```

- [ ] **Step 2: Verify no upstream file has `added > 20` without a hook entry in the fridge list**

```bash
awk -F'\t' 'NR > 1 && $2 > 20 && $4 == "yes" {print $1}' \
  audit-output-post/upstream-file-migration-queue.tsv | while read f; do
    if ! grep -A 20 "^## $f" docs/fork-touchpoints.md | grep -q '\*\*Hook\*\*'; then
      echo "STILL UNMIGRATED with >20 LOC: $f"
    fi
  done
```

Expected: no output. Any file flagged means the migration is incomplete for that file — return to Task 3.T with that file as the next target.

---

## Phase 4: Verification

Phase 4 measures the rebase-time reduction on a throwaway worktree, without disturbing the refactor branch.

### Task 4.1: Create measurement worktree

**Files:**
- Worktree: `.worktrees/rebase-measurement/`

- [ ] **Step 1: Fetch latest upstream**

```bash
cd ../..   # back to port repo root
git fetch upstream
git log --oneline upstream/main -5
```

Expected: recent upstream commits. Note the upstream tip SHA.

- [ ] **Step 2: Create throwaway worktree from refactor tip**

```bash
git worktree add .worktrees/rebase-measurement unity-fork-touchpoint-refactor
cd .worktrees/rebase-measurement
```

### Task 4.2: Attempt the rebase, measure time

**Files:**
- Produces: timestamps in `/tmp/rebase-start.txt`, `/tmp/rebase-end.txt`
- Records: observations used in Task 4.3.

- [ ] **Step 1: Note start time**

```bash
date -Iseconds | tee /tmp/rebase-start.txt
```

- [ ] **Step 2: Start the rebase against current upstream main**

```bash
git rebase upstream/main
```

Expected: git either completes cleanly or stops at merge conflicts.

- [ ] **Step 3: Resolve any conflicts**

For each conflict:

1. `git status` shows conflicting files.
2. Open `docs/fork-touchpoints.md` (this file is on the rebase-in-progress branch; open it from a text editor or via `git show HEAD:docs/fork-touchpoints.md` if needed).
3. Look up the conflicting upstream file's section. The section describes exactly what the fork added: hook call(s), inline tweak(s).
4. Resolve by keeping upstream's new code and re-applying the fork's hook-call line (or inline tweak) at the appropriate location.
5. `git add <resolved-file>`, then `git rebase --continue`.

Track how long each conflict takes (rough wall-clock minutes is fine).

- [ ] **Step 4: Note end time**

```bash
date -Iseconds | tee /tmp/rebase-end.txt
```

- [ ] **Step 5: Verify the build after rebase**

```bash
meson compile -C build-release
```

Expected: clean build.

- [ ] **Step 6: Count how many upstream files had conflicts**

```bash
git log --format="%B" HEAD~<N>..HEAD | grep -c "CONFLICT" || true
```

(Or track manually during Step 3.)

- [ ] **Step 7: Discard the measurement worktree**

```bash
cd ../..   # back to port repo root
git worktree remove .worktrees/rebase-measurement
```

### Task 4.3: Document measurements

**Files:**
- Create: `docs/superpowers/rebase-measurement-2026-04-18.md`
- Worktree context: back inside `.worktrees/unity-fork-touchpoint-refactor/`

- [ ] **Step 1: Compute elapsed time**

```bash
cd .worktrees/unity-fork-touchpoint-refactor

START=$(cat /tmp/rebase-start.txt)
END=$(cat /tmp/rebase-end.txt)
python3 -c "
from datetime import datetime
a = datetime.fromisoformat('$START')
b = datetime.fromisoformat('$END')
print(f'Elapsed: {b - a}')
"
```

Expected: elapsed time printed.

- [ ] **Step 2: Write measurement report**

Create `docs/superpowers/rebase-measurement-2026-04-18.md`:

```markdown
# Rebase time measurement — fork touchpoint-pattern refactor

**Date executed:** <YYYY-MM-DD of execution, replacing 2026-04-18 if later>

**Branch measured:** `unity-fork-touchpoint-refactor`
**Upstream tip at measurement:** `<SHA from Task 4.1 Step 1>`

## Baseline (pre-refactor)

User-reported: ~2 days for the rebase immediately preceding the
2026-04-18 debug session. That rebase surfaced the three ABI-drift bugs
documented in the `project_abi_drift_pattern` memory.

## Result (post-refactor)

- **Wall-clock time:** <elapsed from Task 4.3 Step 1>
- **Files with conflicts:** <N from Task 4.2 Step 6>
- **Average time per conflict:** <minutes>

## Success criterion (from spec)

> Phase-4 test rebase takes < 1/4 the time of the most recent pre-migration
> rebase (user-reported ~2 days → target < 12 hours).

**Result:** <MET | NOT MET | PARTIALLY MET>

<One short paragraph explaining the result and any residual pain
points observed during the rebase.>

## Followups surfaced

- <Any upstream files where conflict resolution felt heavier than
  expected; candidates for further migration in a Phase-3 continuation.>
- <Any fridge-list entries whose descriptions turned out to be too vague
  for fast conflict resolution; candidates for edit.>
```

- [ ] **Step 3: Commit the measurement**

```bash
git add docs/superpowers/rebase-measurement-2026-04-18.md
git commit -m "Record Phase 4 rebase-time measurement for fork refactor

Test-rebase the refactor branch against current upstream/main on a
throwaway worktree; compare elapsed time to user-reported pre-refactor
baseline (~2 days); record pass/fail against the spec's <12-hour
success criterion."
```

### Task 4.4: Open PR or merge the refactor branch

**Files:**
- No file changes; git operation only.

- [ ] **Step 1: Decide merge target**

Review the port's existing workstream merges to confirm the integration branch name. Past workstreams (01 API skinning, 05 Hillaire atmosphere) have merged into a shared integration branch.

- [ ] **Step 2: Push branch and open PR**

```bash
git push -u kim2091 unity-fork-touchpoint-refactor
```

Then open the PR via `gh`:

```bash
gh pr create \
  --title "Fork touchpoint-pattern refactor" \
  --body "$(cat <<'EOF'
## Summary

Migrates scattered fork edits in upstream files into fork-owned files
with thin hooks, backed by the new `docs/fork-touchpoints.md` index.

## Why

Upstream rebases took ~2 days of merge-conflict work because fork edits
were interleaved directly inside upstream files. During that rebase
chaos, silent ABI-drift bugs sneaked through (three confirmed fixes on
2026-04-18; two more still latent — see project memory files). This
refactor shrinks fork-vs-upstream surface area so rebase conflicts
drop dramatically, which also reduces the cognitive load under which
ABI drift slips through.

See:
- Spec: `docs/superpowers/specs/2026-04-18-fork-touchpoint-pattern-design.md`
- Plan: `docs/superpowers/plans/2026-04-18-fork-touchpoint-pattern-plan.md`
- Measurement: `docs/superpowers/rebase-measurement-2026-04-18.md`

Queued follow-ups (separate PRs): ABI conformance harness, plugin/port
contract doc.

## Test plan

- [x] `meson compile -C build-release` clean throughout migration.
- [x] Skyrim Remix plugin loads, renders outdoors with physical sky.
- [x] Test rebase against current `upstream/main` clean within 12 hours.
- [x] `docs/fork-touchpoints.md` has an entry for every upstream file
      touched by fork code.

## Checklist

- [x] If this PR touches an upstream file, I updated docs/fork-touchpoints.md.
EOF
)"
```

Expected: PR created successfully. Share the URL for review.

---

## Self-Review

**1. Spec coverage:**

| Spec element | Plan task(s) |
|---|---|
| In-scope: fork-owned files | 2.2 (header), 3.T Step 5 (impl files) |
| In-scope: thin hooks | 3.T Step 7 |
| In-scope: fridge list | 1.5, 3.T Step 10, 3.INDEX |
| In-scope: PR-template bullet | 2.4 |
| Rollout phase 1: Audit | 1.1–1.6 |
| Rollout phase 2: Scaffolding | 2.1–2.5 |
| Rollout phase 3: Migration | 3.T, 3.INDEX, 3.VERIFY |
| Rollout phase 4: Verification | 4.1–4.4 |
| Success criterion: rebase time dropped | 4.2, 4.3 |
| Success criterion: fork surface auditable | 1.5, 3.VERIFY |
| Success criterion: no hidden edits | 3.VERIFY |
| Success criterion: discipline holds | 2.4 (PR bullet) |

All spec elements covered.

**2. Placeholder scan:**

- No `TODO`, `TBD`, `fill in later`, or generic "handle errors" placeholders.
- Task 3.T is intentionally templated with substitutable values (`<target-upstream-file>`, `<hook-symbol>`, etc.) because the audit determines the per-file values at runtime. Substitution is explicit, not placeholder-leakage.
- Task 4.3 Step 2 "write measurement report" contains concrete `<angle-bracketed>` fields that the executor fills from Step 1 output — this is data transcription, not plan ambiguity.

**3. Type consistency:**

- `fork_hooks::onExternalDraw` is the running example throughout; signature `(SceneManager& scene, const ExternalDrawCmd& cmd)` is consistent in header (3.T Step 3), implementation (3.T Step 5), and call site (3.T Step 7).
- File naming `rtx_unity_submit.cpp` / `rtx_fork_*.cpp` is consistent across spec, conventions section (2.1 Step 3), and migration template.
- `FORK_POINT` environment variable name is consistent from Task 1.2 Step 3 through Task 3.VERIFY.

---

## Execution handoff to follow.
