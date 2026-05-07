/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

/*
 * Cloud-noise bake (fork) -- runs once at atmosphere init.
 *
 * Fills the 256-cubed R8 cloudNoise3D texture with a 3D Perlin gradient FBM
 * stack: 4-octave base + 3-octave detail. Coverage, anvil pow, cloud-type
 * profile, and wind are per-frame at sampling time, NOT baked.
 *
 * Frequency control via pre-scaling worldPosKm into fbmNoise3D -- the FBM's
 * third argument is `seed`, not `baseFreq`. Detail uses seed != 0 for
 * decorrelation from base.
 *
 * Dispatch: 32x32x32 thread groups, 8x8x8 threads each -> 256-cubed voxels.
 *
 * One-shot at init -- see RtxAtmosphere::dispatchCloudNoise3DBake (Task 5).
 */

#include "rtx/pass/atmosphere/atmosphere_common.slangh"

layout(binding = 0)
ConstantBuffer<AtmosphereArgs> atmosphereArgs;

layout(binding = 1, r8)
RWTexture3D<float> cloudNoise3DOutput;

// Map a voxel index to its world-space sample position in km.
vec3 voxelToWorldKm(uint3 voxel, float tileKm) {
  // Center samples on voxel centers (+0.5), scale to [0, tileKm).
  return (vec3(voxel) + 0.5f) * (tileKm / 256.0f);
}

[shader("compute")]
[numthreads(8, 8, 8)]
void main(uint3 threadId : SV_DispatchThreadID) {
  AtmosphereArgs args = atmosphereArgs;

  // Bounds check -- dispatch covers exactly 256-cubed; defensive guard is cheap.
  if (any(threadId >= uint3(256u, 256u, 256u))) {
    return;
  }

  vec3 worldPosKm = voxelToWorldKm(threadId, args.cloudNoiseTileKm);

  // Base FBM: 4 octaves at frequency 90 (frequency via pre-scale of worldPosKm).
  // Detail FBM: 3 octaves at frequency 360 (4x base) with seed offset 17.3 to
  // decorrelate from the base lattice. Both return approximately [-1, 1]
  // (gradient noise's natural range).
  float base   = fbmNoise3D(worldPosKm * 90.0f,  4, 0.0f);
  float detail = fbmNoise3D(worldPosKm * 360.0f, 3, 17.3f);

  // Combine: detail at half-amplitude, then saturate-clamp to [0, 1] for R8.
  // The clip is intentional -- gradient noise's negative half maps to "no
  // cloud" voxels, which produces a sparse cloud-like distribution.
  float density = saturate(base + 0.5f * detail);

  cloudNoise3DOutput[threadId] = density;
}
