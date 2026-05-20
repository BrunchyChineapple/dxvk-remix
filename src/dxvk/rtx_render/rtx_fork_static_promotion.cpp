#include "rtx_fork_static_promotion.h"
#include "rtx_options.h"
#include "rtx_instance_manager.h"
#include "rtx_accel_manager.h"
#include "rtx_context.h"
#include "rtx_types.h"
#include "rtx/pass/instance_definitions.h"
#include "../imgui/imgui.h"

#include <cmath>
#include <cstdint>

namespace dxvk {

  // RTX_OPTIONs for the persistent static promotion tier.
  // All declared here (not in rtx_options.h) so the fork footprint on the
  // upstream options file stays at zero LOC. Wrapped in a fork-owned holder
  // class because the RTX_OPTION macro expands to a class-body member
  // declaration (public: inline static RtxOption<T>); it cannot live at
  // namespace scope. Mirrors the RtxForkGlobalTonemap / RtxForkHableFilmic
  // pattern used by rtx_fork_tonemap.h. Discovered by the auto-doc
  // generator via the standard RTX_OPTION macro.
  class RtxForkStaticPromotion {
    RTX_OPTION("rtx", bool, enableStaticGeometryPromotion, false,
      "Master switch for the persistent static geometry promotion tier. When enabled, "
      "stable static world geometry is detected and routed into long-lived merged BLASes "
      "that only rebuild on membership change. Reduces TLAS instance count and per-frame "
      "BLAS rebuild cost for stable scenes.");

    RTX_OPTION("rtx", uint32_t, staticGeometryPromotionFrames, 8,
      "Stability threshold (K). An instance must observe identical geometry / transform / "
      "material for K consecutive frames before it is promoted into a persistent bucket. "
      "Higher = more conservative (fewer false promotes); lower = faster ramp-up after "
      "cell transitions.");

    RTX_OPTION("rtx", uint32_t, maxPrimsInPersistentBLAS, 262144,
      "Maximum primitive count per persistent merged BLAS. Larger than maxPrimsInMergedBLAS "
      "because the persistent rebuild cost is amortized across many frames.");

    RTX_OPTION("rtx", uint32_t, persistentBlasMemoryBudgetMB, 256,
      "LRU eviction trigger. When total persistent BLAS memory exceeds this budget, the "
      "least-recently-rendered bucket is demoted wholesale.");

    RTX_OPTION("rtx", bool, enableSameFrameContentDedup, false,
      "Layer 2: when DrawCallCache::get would allocate a fresh BlasEntry because the only "
      "topological match was already touched this frame, fall through to a cross-bucket "
      "VertexDataHash + material + bones search and share the existing entry on match. "
      "Reduces duplicate BlasEntries for games that submit the same content twice per frame.");

    RTX_OPTION("rtx", float, staticPromotionTransformEpsilon, 1.0e-4f,
      "Per-element transform stability tolerance. Element-wise abs-diff above this threshold "
      "resets transformStableFrames. Default 1e-4 yields sub-pixel positional error if a "
      "within-epsilon drift is absorbed by the persistent BLAS.");
  };

  namespace {
    // Element-wise abs-diff comparison of two VkTransformMatrixKHR matrices under
    // the user-configured epsilon. Returns true if every element is within
    // tolerance, false otherwise.
    bool transformWithinEpsilon(const VkTransformMatrixKHR& a, const VkTransformMatrixKHR& b, float eps) {
      for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
          if (std::abs(a.matrix[row][col] - b.matrix[row][col]) > eps) {
            return false;
          }
        }
      }
      return true;
    }

    // Computes a stable hash of the bucket-key-relevant fields of an RtInstance.
    // Mirrors BlasBucketKey but reads directly from the live VkInstance and the
    // two boolean flags the bucket key tracks.
    uint64_t hashBucketKeyFields(const VkAccelerationStructureInstanceKHR& vk, bool isSubsurface, bool isUnordered) {
      struct Packed {
        uint32_t sbtOffset;
        uint32_t customIndexFlags;
        uint32_t instanceFlags;
        uint8_t  mask;
        bool     isUnordered;
        bool     isSubsurface;
        uint8_t  pad;
      } p {};
      p.sbtOffset = vk.instanceShaderBindingTableRecordOffset;
      p.customIndexFlags = vk.instanceCustomIndex & ~uint32_t(CUSTOM_INDEX_SURFACE_MASK);
      p.instanceFlags = vk.flags;
      p.mask = static_cast<uint8_t>(vk.mask);
      p.isUnordered = isUnordered;
      p.isSubsurface = isSubsurface;
      return XXH3_64bits(&p, sizeof(p));
    }
  } // namespace

  namespace fork_hooks {

    void tickStabilityCounters(const std::vector<RtInstance*>& instances, uint32_t currentFrame) {
      if (!RtxForkStaticPromotion::enableStaticGeometryPromotion()) {
        return;
      }
      const float eps = RtxForkStaticPromotion::staticPromotionTransformEpsilon();

      for (RtInstance* inst : instances) {
        if (!inst) continue;

        // Geometry: bumps when the linked BlasEntry's content was NOT updated this frame.
        BlasEntry* blas = inst->getBlas();
        const bool geometryChanged = blas && (blas->frameLastUpdated == currentFrame);
        if (geometryChanged) {
          inst->m_geometryStableFrames = 0;
        } else {
          ++inst->m_geometryStableFrames;
        }

        // Transform: element-wise compare against last frame.
        const VkTransformMatrixKHR& cur = inst->getVkInstance().transform;
        if (transformWithinEpsilon(cur, inst->m_prevFrameTransform, eps)) {
          ++inst->m_transformStableFrames;
        } else {
          inst->m_transformStableFrames = 0;
        }
        inst->m_prevFrameTransform = cur;

        // Material/SBT/mask: compare hashed bucket key.
        const uint64_t curHash = hashBucketKeyFields(
          inst->getVkInstance(),
          inst->isSubsurface(),
          inst->usesUnorderedApproximations());
        if (curHash == inst->m_prevFrameBucketKeyHash) {
          ++inst->m_materialStableFrames;
        } else {
          inst->m_materialStableFrames = 0;
        }
        inst->m_prevFrameBucketKeyHash = curHash;
      }
    }

    bool tryRouteToPersistentBucket(AccelManager& /*mgr*/, RtInstance* /*instance*/, uint32_t /*currentFrame*/) {
      return false;
    }

    void touchPersistentBlasesForFastSkip(AccelManager& /*mgr*/, uint32_t /*currentFrame*/) {}

    void onInstanceRemoved(RtInstance* /*instance*/) {}

    void showStaticPromotionPanel() {}

    void writeStaticPromotionDebugView(DxvkContext& /*ctx*/) {}

  } // namespace fork_hooks
} // namespace dxvk
