/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

#include <mutex>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_types.h"
#include "rtx_common_object.h"
#include "rtx_staging.h"
#include "rtx_point_instancer_system.h"
#include "../util/util_vector.h"
#include "../util/util_matrix.h"
#include "../util/util_struct_hash.h"

namespace dxvk 
{
class DxvkContext;
class DxvkDevice;
class ResourceCache;
class CameraManager;
class OpacityMicromapManager;

// AccelManager is responsible for maintaining the acceleration structures (BLAS and TLAS)
class AccelManager : public CommonDeviceObject {
  class BlasBucket {
  public:
    std::vector<VkAccelerationStructureGeometryKHR> geometries {};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges {};
    std::vector<RtInstance*> originalInstances {};
    std::vector<uint32_t> primitiveCounts {};
    std::vector<uint32_t> instanceBillboardIndices {};  // Billboard index within an instance's billboard array
    std::vector<uint32_t> indexOffsets {};              // Index offsets within geometry
    uint8_t instanceMask = 0;
    uint32_t instanceShaderBindingTableRecordOffset = 0;
    uint32_t customIndexFlags = 0;
    VkGeometryInstanceFlagsKHR instanceFlags = 0;
    bool usesUnorderedApproximations = false;
    uint32_t reorderedSurfacesOffset = UINT32_MAX;
    bool hasOmmInstances = false;
    bool hasSssInstances = false;

    // The PooledBlas assigned to this bucket by createBlasBuffersAndInstances.
    // Stored here so the per-bucket cache can capture it after buildBlases.
    PooledBlas* assignedBlas = nullptr;
    
    // Tries to add a geometry instance to the bucket. The addition is successful if either:
    //   a) the bucket is empty,
    //   b) the instance has the same mask etc. as all other instances in the bucket.
    bool tryAddInstance(RtInstance* instance);
  };

  // Key for O(1) bucket lookup in the merged-BLAS path.
  // Two instances can share a merged BLAS bucket iff they have identical keys.
  struct BlasBucketKey {
    uint32_t instanceShaderBindingTableRecordOffset = 0;
    uint32_t customIndexFlags = 0;
    VkGeometryInstanceFlagsKHR instanceFlags = 0;
    uint8_t instanceMask = 0;
    bool usesUnorderedApproximations = false;
    bool isSubsurface = false;
    uint8_t pad = 0;

    bool operator==(const BlasBucketKey& other) const {
      return instanceMask == other.instanceMask &&
             instanceShaderBindingTableRecordOffset == other.instanceShaderBindingTableRecordOffset &&
             customIndexFlags == other.customIndexFlags &&
             instanceFlags == other.instanceFlags &&
             usesUnorderedApproximations == other.usesUnorderedApproximations &&
             isSubsurface == other.isSubsurface;
    }
  };

  struct BlasBucketKeyHash {
    size_t operator()(const BlasBucketKey& k) const {
      return static_cast<size_t>(hashStructByMemory<BlasBucketKey,
          &BlasBucketKey::instanceShaderBindingTableRecordOffset,
          &BlasBucketKey::customIndexFlags,
          &BlasBucketKey::instanceFlags,
          &BlasBucketKey::instanceMask,
          &BlasBucketKey::usesUnorderedApproximations,
          &BlasBucketKey::isSubsurface,
          &BlasBucketKey::pad>(k));
    }
  };

  struct UniqueBlasInstances {
    BlasEntry* blasEntry = nullptr;
    std::vector<RtInstance*> instances;
  };

public:
  AccelManager(AccelManager const&) = delete;
  AccelManager& operator=(AccelManager const&) = delete;

  explicit AccelManager(DxvkDevice* device);

  // Returns a GPU buffer containing the surface data for active instances
  const Rc<DxvkBuffer> getSurfaceBuffer() const { return m_surfaceBuffer; }

  const Rc<DxvkBuffer> getSurfaceMappingBuffer() const { return m_surfaceMappingBuffer; }

  const Rc<DxvkBuffer> getCurrentFramePrimitiveIDPrefixSumBuffer() const {
    return m_primitiveIDPrefixSumBuffer;
  }

  const Rc<DxvkBuffer> getLastFramePrimitiveIDPrefixSumBuffer() const {
    return m_primitiveIDPrefixSumBufferLastFrame;
  }

  const Rc<DxvkBuffer> getBillboardsBuffer() const { return m_billboardsBuffer; }

  // Clear all instances currently tracked by manager
  void clear();

  // Clean up instances which are deemed as no longer required
  void garbageCollection();

  // Prepares instance buffers for rendering by the GPU
  void prepareSceneData(Rc<DxvkContext> ctx, class DxvkBarrierSet& execBarriers, InstanceManager& instanceManager);

  // Uploads instances' surface data to the GPU. Retained surfaces refresh their
  // frame-scoped buffer indices and lightweight per-frame listener state here,
  // inside the loop that already emits them.
  void uploadSurfaceData(Rc<DxvkContext> ctx, InstanceManager& instanceManager);

  // Merges the RtInstance's into a set of BLAS. Some of the BLAS will contain multiple geometries/instances,
  // and some other BLAS will be dedicated to instances with static geometries.
  void mergeInstancesIntoBlas(Rc<DxvkContext> ctx, class DxvkBarrierSet& execBarriers,
                              const std::vector<TextureRef>& textures, const CameraManager& cameraManager, 
                              InstanceManager& instanceManager, OpacityMicromapManager* opacityMicromapManager);

  // Dispatches GPU compute culling for all PointInstancer batches recorded during
  // mergeInstancesIntoBlas. Must be called after prepareSceneData (placeholders uploaded)
  // and before buildTlas.
  void dispatchPointInstancerCulling(Rc<DxvkContext> ctx, const CameraManager& cameraManager,
                                     const Rc<DxvkBuffer>& surfaceMaterialBuffer);

  void buildTlas(Rc<DxvkContext> ctx);

  // Returns the number of live BLAS objects
  static uint32_t getBlasCount();

  uint32_t getSurfaceCount() const { return m_reorderedSurfaces.size(); }
  const std::vector<RtInstance*>& getOrderedInstances() const { return m_reorderedSurfaces; }

  // Returns true if the last mergeInstancesIntoBlas call took the fast-skip
  // path (scene generation unchanged).  When true, m_reorderedSurfaces and
  // all BLAS/surface data are identical to the previous frame, so callers
  // can skip redundant GPU uploads (e.g. surface materials).
  bool wasSceneUnchangedThisFrame() const { return m_sceneUnchangedThisFrame; }

  void removeInstanceFromBucketCache(RtInstance* instance);
  void invalidateOpacityMicromapBindings() { m_ommBindPending = true; }

  // Per-phase breakdown of mergeInstancesIntoBlas. The phase as a whole is the largest
  // remaining cost at high retained-instance counts, and it contains six separate full
  // sweeps of the instance population, so a single total cannot say which one to attack.
  //
  // The counters exist to answer a question the source alone cannot: buckets are keyed by
  // BlasBucketKey, so a scene of near-identical statics may collapse into very few buckets
  // holding thousands of instances each. Because dirty tracking is per-bucket, one churning
  // instance can drag its whole bucket back through the rebuild pipeline. Whether the cost
  // is the sweeps or that amplification decides which fix is correct.
  struct MergeBlasStats {
    // Sub-phase timings. liveSet, dirtyScan, mainLoop, restore and prefixSum are disjoint
    // and sum to slightly less than mergeBlas. uploadSurface is nested inside buildBlases,
    // which the mainLoop timer does not cover, so it is additive with the rest.
    uint64_t liveSetNs = 0;         // building the validity set of live instances
    uint64_t dirtyScanNs = 0;       // scanning cached buckets for invalidation
    uint64_t mainLoopNs = 0;        // per-instance bucket routing
    uint64_t restoreNs = 0;         // restoring surfaces from clean cached buckets
    uint64_t prefixSumNs = 0;       // rebuildPrimitivePrefixSums
    uint64_t uploadSurfaceNs = 0;   // uploadSurfaceData, including the retained finalize

    uint32_t buckets = 0;                   // cached buckets seen this frame
    uint32_t bucketsDirty = 0;              // of those, how many needed a rebuild
    uint32_t instancesInDirtyBuckets = 0;   // instances forced through the pipeline by bucket granularity
    uint32_t retainedInstancesInDirtyBuckets = 0;  // of those, how many are retained statics
    uint32_t pipelineInstances = 0;         // instances that ran the full main-loop body
    uint32_t skippedCleanInstances = 0;     // instances skipped because their bucket was clean
    uint32_t samples = 0;                   // frames that took the incremental path

    // Which predicate actually dirtied each bucket. Capping bucket size bounds the
    // amplification whatever the cause, but the residual can only be attacked once the
    // cause is known, and the cause cannot be read off the source.
    uint32_t dirtyPreInvalidated = 0;  // an instance was destroyed since the last build
    uint32_t dirtySizeMismatch = 0;    // cached instance/identity arrays disagreed
    uint32_t dirtyRemoved = 0;         // a cached instance is no longer live
    uint32_t dirtyIdentity = 0;        // pointer reused by a different allocation
    uint32_t dirtyBlasDirty = 0;       // instance marked itself dirty
    uint32_t dirtyBlasUpdated = 0;     // the shared BlasEntry was rebuilt this frame
    uint32_t dirtyKeyChanged = 0;      // bucket key no longer matches
  };
  const MergeBlasStats& getMergeBlasStats() const { return m_mergeBlasStats; }
  void resetMergeBlasStats() { m_mergeBlasStats = MergeBlasStats {}; }

  // Scene-wide primitive total from the most recent prefix-sum rebuild. Reported in
  // telemetry because the overflow diagnostic is ONCE()-gated and therefore samples a
  // single arbitrary frame, usually during streaming, which cannot answer how the count
  // varies with scene size.
  uint32_t getLastTotalPrimitiveCount() const { return m_lastTotalPrimitiveCount; }

private:
  struct SurfaceInfo {
    uint32_t surfaceMaterialIndex;
    Vector3 worldPosition;
  };

  // Persistent containers to reduce frame to frame reallocations in ::buildParticleSurfaceMapping()
  struct {
    std::vector<AccelManager::SurfaceInfo> surfaceInfoLists[2];   // Two containers for subsequent frames, ping-pong framed to frame
    uint32_t currIndex = 0;
    uint32_t prevIndex = 1;
  } buildParticleSurfaceMappingFuncState;

  // Persistent containers to reduce frame to frame reallocations in ::uploadSurfaceData()
  struct {
    std::vector<unsigned char> surfacesGPUData;
    std::vector<uint32_t> surfaceIndexMapping;
    uint32_t previousFrameSurfaceCount = 0; // Tracks last frame's surface count for mapping coverage
  } uploadSurfaceDataFuncState;

  void buildBlases(Rc<DxvkContext> ctx, DxvkBarrierSet& execBarriers,
                   const CameraManager& cameraManager, OpacityMicromapManager* opacityMicromapManager, InstanceManager& instanceManager,
                   const std::vector<TextureRef>& textures, const std::vector<RtInstance*>& instances,
                   const std::vector<std::unique_ptr<BlasBucket>>& blasBuckets, 
                   std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& blasToBuild,
                   std::vector<VkAccelerationStructureBuildRangeInfoKHR*>& blasRangesToBuild,
                   size_t& currentScratchOffset);
  
  void addBlas(RtInstance* instance, BlasEntry* blasEntry, const Matrix4* instanceToObject);
  void addPointInstancerBlas(RtInstance* rtInstance, BlasEntry* blasEntry);

  void createBlasBuffersAndInstances(Rc<DxvkContext> ctx, 
                                     const std::vector<std::unique_ptr<BlasBucket>>& blasBuckets,
                                     std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& blasToBuild,
                                     std::vector<VkAccelerationStructureBuildRangeInfoKHR*>& blasRangesToBuild,
                                     size_t& currentScratchOffset);
  template<Tlas::Type type>
  void internalBuildTlas(Rc<DxvkContext> ctx, size_t& totalScratchSize);

  void rebuildPrimitivePrefixSums(const Vector3& cameraPosition);

  void finalizeRetainedInstanceUpload(
      RtInstance& instance,
      InstanceManager& instanceManager,
      uint32_t currentFrameId);

  void buildParticleSurfaceMapping(std::vector<uint32_t>& surfaceIndexMapping);

  bool validateUpdateMode(const VkAccelerationStructureBuildGeometryInfoKHR& oldInfo, const VkAccelerationStructureBuildGeometryInfoKHR& newInfo);

  std::vector<RtInstance*> m_reorderedSurfaces;
  std::vector<uint32_t> m_reorderedSurfacesFirstIndexOffset;
  std::vector<uint32_t> m_reorderedSurfacesPrimitiveIDPrefixSum;              // Exclusive prefix sum for this frame's surface primitive count array
  std::vector<uint32_t> m_reorderedSurfacesPrimitiveIDPrefixSumLastFrame;     // Exclusive prefix sum for last frame's surface primitive count array
  std::vector<VkAccelerationStructureInstanceKHR> m_mergedInstances[Tlas::Count];
  std::vector<Rc<PooledBlas>> m_blasPool;

  // GPU-driven PointInstancer culling batches, recorded per frame in mergeInstancesIntoBlas
  std::vector<PointInstancerBatch> m_pointInstancerBatches;

  // Number of VkAccelerationStructureInstanceKHR slots reserved for PointInstancer
  // instances in each TLAS type.  These slots are NOT stored in m_mergedInstances —
  // the GPU culling shader fills them directly in the instance buffer.
  uint32_t m_pointInstancerSlotsPerType[Tlas::Count] = {};

  // --- Incremental BLAS build caching ---
  // Scene generation from InstanceManager when the BLAS was last built.
  uint64_t m_lastProcessedGeneration = UINT64_MAX;
  bool m_sceneUnchangedThisFrame = false;
  // Set when newly built OMMs need to be bound to BLASes.  Forces all buckets
  // dirty on the next incremental rebuild so tryBindOpacityMicromap runs.
  bool m_ommBindPending = false;

  // Number of non-billboard entries in m_mergedInstances per TLAS type.
  // prepareSceneData() truncates to this baseline before appending fresh billboard instances.
  uint32_t m_mergedInstancesBaselineCount[Tlas::Count] = {};

  // Tracks all dynamic BLAS references from the last full build so they can be
  // touched during the cached (skip) path to prevent GC from collecting them.
  std::vector<Rc<PooledBlas>> m_activeDynamicBlases;

  // --- Dynamics-only rebuild cached state ---
  // Per-bucket cache from the last build.  Each entry corresponds to one merged
  // BLAS bucket and stores all the data needed to restore its portion of
  // m_reorderedSurfaces and m_mergedInstances without re-running the bucket pipeline.
  struct CachedBucketState {
    // The instances that contributed geometry to this bucket (for dirty checking)
    std::vector<RtInstance*> instances;
    std::vector<uint64_t> instanceCacheIdentities;
    // Surface data for m_reorderedSurfaces
    std::vector<RtInstance*> surfaces;
    std::vector<uint32_t> indexOffsets;
    // The PooledBlas assigned to this bucket (kept alive via Rc)
    Rc<PooledBlas> assignedBlas;
    // TLAS instance template (surface offset must be adjusted each frame)
    VkAccelerationStructureInstanceKHR tlasInstance {};
    // Which TLAS type(s) this bucket was emitted to
    bool isUnordered = false;
    bool hasSssInstances = false;
    // Whether this bucket holds retained statics, recorded at build time. A dirty bucket
    // may hold dangling instance pointers, so this cannot be derived later by inspection.
    bool isRetained = false;
  };
  std::vector<CachedBucketState> m_cachedBuckets;

  // Buckets invalidated since the last build, parallel to m_cachedBuckets. An
  // instance destroyed between builds dirties only its own bucket, so the rest of
  // the cache survives. A dirty bucket's cached instance pointers may already be
  // dangling and must not be dereferenced.
  std::vector<bool> m_cachedBucketDirty;

  uint32_t m_lastTotalPrimitiveCount = 0;

  MergeBlasStats m_mergeBlasStats;

  // Cap in force when the current bucket cache was built. A live change invalidates it.
  uint32_t m_lastBucketInstanceCap = UINT32_MAX;

  // Maps a merged instance pointer to its bucket index in m_cachedBuckets.
  // Allows O(1) "is this instance in a clean bucket?" check in the main loop.
  std::unordered_map<RtInstance*, uint32_t> m_instanceBucketIndex;

  // Set of BlasEntry* that went to the dynamic path on the last full rebuild.
  // Used for quick O(1) per-instance classification on the dynamics-only path.
  std::unordered_set<BlasEntry*> m_cachedDynamicBlasEntries;

  // Scratch containers for collecting dynamic BLAS groups during mergeInstancesIntoBlas().
  // The map is lookup-only; the vector preserves first-seen emission order.
  std::vector<UniqueBlasInstances> m_uniqueDynamicBlas;
  uint32_t m_uniqueDynamicBlasCount = 0;
  std::unordered_map<BlasEntry*, uint32_t> m_uniqueDynamicBlasIndex;

  void resetUniqueDynamicBlasGroups();

  Rc<DxvkBuffer> m_vkInstanceBuffer; // Note: Holds Vulkan AS Instances, not RtInstances
  Rc<DxvkBuffer> m_surfaceBuffer;
  Rc<DxvkBuffer> m_surfaceMappingBuffer;
  Rc<DxvkBuffer> m_transformBuffer;
  Rc<DxvkBuffer> m_primitiveIDPrefixSumBuffer;
  Rc<DxvkBuffer> m_primitiveIDPrefixSumBufferLastFrame;

  int getCurrentFramePrimitiveIDPrefixSumBufferID() const;

  Rc<PooledBlas> m_intersectionBlas;
  Rc<DxvkBuffer> m_aabbBuffer;
  Rc<DxvkBuffer> m_billboardsBuffer;
  void createAndBuildIntersectionBlas(Rc<DxvkContext> ctx, class DxvkBarrierSet& execBarriers);
  
  Rc<DxvkBuffer> getScratchMemory(const size_t requiredScratchAllocSize);
  Rc<PooledBlas> createPooledBlas(size_t bufferSize, const char* name) const;

  VkDeviceSize m_scratchAlignment;
  Rc<DxvkBuffer> m_scratchBuffer;
};

}  // namespace dxvk

