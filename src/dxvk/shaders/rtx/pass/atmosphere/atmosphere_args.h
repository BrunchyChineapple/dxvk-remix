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
#pragma once

#include "rtx/utility/shader_types.h"

// Atmosphere parameters for Hillaire physically-based atmospheric scattering
struct AtmosphereArgs {
  vec3 sunDirection;
  float planetRadius;  // in km
  
  vec3 sunIlluminance;
  float atmosphereThickness;  // in km
  
  vec3 rayleighScattering;
  float mieAnisotropy;  // Henyey-Greenstein phase function g parameter [-1, 1]
  
  vec3 mieScattering;
  float sunRayBrightness;  // Multiplier for direct sun ray brightness
  
  // Ozone absorption (important for realistic sunset colors per Hillaire paper Section 3.4)
  vec3 ozoneAbsorption;  // Absorption coefficients (km^-1)
  float ozoneLayerAltitude;  // Peak altitude of ozone layer (km)
  
  uint transmittanceLutWidth;
  uint transmittanceLutHeight;
  uint multiscatteringLutSize;
  uint skyViewLutWidth;
  
  uint skyViewLutHeight;
  float ozoneLayerWidth;  // Width of ozone layer (km)
  float viewAltitude;     // Camera altitude offset (km)
  uint pad2;
  
  // Derived parameters (computed on CPU)
  float atmosphereRadius;  // planetRadius + atmosphereThickness
  float rayleighScaleHeight;  // exponential density falloff for Rayleigh (km)
  float mieScaleHeight;  // exponential density falloff for Mie (km)
  float sunAngularRadius; // Sun angular radius in radians

  // Night sky parameters
  float starBrightness;     // Overall star brightness multiplier
  float starDensity;        // Controls how many stars are visible (threshold)
  float starTwinkleSpeed;   // Speed of star twinkling animation
  float nightSkyBrightness; // Airglow / ambient night sky brightness

  vec3 nightSkyColor;       // Base color of the night sky (airglow tint)
  float timeSeconds;        // Elapsed time for twinkling animation

  // Moon parameters (Secunda)
  vec3 moonDirection;       // Direction to moon in Y-up space (normalized)
  float moonAngularRadius;  // Moon angular radius in radians
  
  vec3 moonColor;           // Base moon color/albedo
  float moonBrightness;     // Overall moon brightness multiplier
  
  float moonPhase;          // Moon phase [0, 1]: 0=new, 0.25=first quarter, 0.5=full, 0.75=last quarter
  float moonEnabled;        // 1.0 if moon should be rendered, 0.0 otherwise
  float pad3;
  float pad4;
};
