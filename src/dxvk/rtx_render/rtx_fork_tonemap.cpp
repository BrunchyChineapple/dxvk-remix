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
