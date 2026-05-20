#pragma once

// rtx_fork_static_promotion.h — fork-owned persistent BLAS promotion tier.
// Sits above the per-frame merged BLAS pool in AccelManager::mergeInstancesIntoBlas:
// stable static world geometry is detected via three per-instance stability counters
// (geometry / transform / material) and routed into long-lived "persistent buckets"
// whose BLASes only rebuild on membership change. See
// docs/superpowers/specs/2026-05-20-static-geometry-promotion-design.md.

#include <cstdint>
#include <vector>
#include <unordered_map>
#include "../../util/util_singleton.h"
#include "../../util/util_vector.h"

namespace dxvk {
  class RtInstance;
  class AccelManager;
  class DxvkContext;
  class DxvkDevice;
  struct BlasEntry;

  namespace fork_hooks {

    // Called once per frame, after instance manager finalization and before
    // AccelManager::mergeInstancesIntoBlas runs. Updates the three stability
    // counters on every RtInstance based on per-frame deltas (transform diff,
    // BlasEntry::frameLastUpdated, BlasBucketKey). Implementation in
    // rtx_fork_static_promotion.cpp.
    void tickStabilityCounters(const std::vector<RtInstance*>& instances, uint32_t currentFrame);

    // Called from AccelManager::mergeInstancesIntoBlas for each non-skipped
    // RtInstance. If the instance is currently a member of a persistent
    // bucket, restores its TLAS instance from the persistent pool and returns
    // true (caller should skip the per-frame routing for this instance). If
    // the instance is newly eligible for promotion, adds it to a persistent
    // bucket and returns true. Returns false otherwise — the caller proceeds
    // with the existing per-frame merged/dynamic routing.
    bool tryRouteToPersistentBucket(AccelManager& mgr, RtInstance* instance, uint32_t currentFrame);

    // Called from the AccelManager full-skip fast path. Touches every
    // persistent bucket's BLAS so the GC does not collect them while the
    // scene is unchanged.
    void touchPersistentBlasesForFastSkip(AccelManager& mgr, uint32_t currentFrame);

    // Called from AccelManager::removeInstanceFromBucketCache when an
    // instance is being torn down (GC, hidden, mask=0). Demotes the instance
    // from its persistent bucket if it had been promoted.
    void onInstanceRemoved(RtInstance* instance);

    // Renders the "Static Geometry Promotion" ImGui panel inside the
    // Rendering section of the dev menu. Reads per-frame counters captured
    // during the most recent mergeInstancesIntoBlas pass.
    void showStaticPromotionPanel();

    // Writes the debug view (per-pixel "BLAS source" colorization) when the
    // active debug view enum is DebugViewIdx::BlasSource.
    void writeStaticPromotionDebugView(DxvkContext& ctx);

  } // namespace fork_hooks
} // namespace dxvk
