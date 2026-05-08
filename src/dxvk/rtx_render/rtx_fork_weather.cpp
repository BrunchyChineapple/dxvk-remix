// src/dxvk/rtx_render/rtx_fork_weather.cpp
//
// Fork-owned file. Full implementation of WeatherBlender: the per-frame lerp
// pipeline that blends 29 weather params (cloud, atmosphere, sky/moon mood,
// volumetric fog) between named presets over a plugin-specified duration.
//
// Reads:
//   __weather.target       — name of the active target preset (string)
//   __weather.blend_seconds — blend duration override (float string, default 1.0)
//
// Writes:
//   Derived layer of each underlying RTX_OPTION via setImmediately()
//   __weather.current, __weather.previous, __weather.blend_progress (GameStateStore)
//
// Dormant when __weather.target is absent or unknown — zero upstream
// behavioural change.
//
// Task 3 wires update() into the per-frame render loop via
// fork_hooks::updateWeatherBlender(RtxContext&, float).
// Task 4 implements the full ImGui surface in showImguiSettings().
// Task 7 handles the upstream touchpoint update (rtx_fork_hooks.h comment).

#include "rtx_fork_weather.h"
#include "rtx_fork_hooks.h"
#include "rtx_fork_game_state.h"
#include "rtx_options.h"
#include "rtx_global_volumetrics.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Anonymous-namespace helpers
// ---------------------------------------------------------------------------
namespace dxvk { namespace fork_weather { namespace {

  // --- Math helpers ---

  float saturate(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
  }

  float lerp(float a, float b, float t) {
    return a + (b - a) * t;
  }

  Vector3 lerpV3(const Vector3& a, const Vector3& b, float t) {
    return Vector3(
      lerp(a.x, b.x, t),
      lerp(a.y, b.y, t),
      lerp(a.z, b.z, t)
    );
  }

  // Shortest-path angular interpolation (degrees).
  // 350° → 10°: delta = fmod(10-350+540, 360)-180 = fmod(200,360)-180 = 200-180 = 20°.
  float lerpAngleDeg(float a, float b, float t) {
    float delta = std::fmod((b - a + 540.0f), 360.0f) - 180.0f;
    return a + delta * t;
  }

  // --- GameStateStore wrappers ---

  float readFloatFromGameStateStore(const std::string& key, float defaultValue) {
    std::string raw;
    if (!fork_game_state::GameStateStore::get().tryGet(key, raw)) {
      return defaultValue;
    }
    try {
      return std::stof(raw);
    } catch (...) {
      return defaultValue;
    }
  }

  std::string readStringFromGameStateStore(const std::string& key) {
    std::string out;
    fork_game_state::GameStateStore::get().tryGet(key, out);
    return out;
  }

  void writeToGameStateStore(const std::string& key, std::string value) {
    fork_game_state::GameStateStore::get().set(key, std::move(value));
  }

  // --- Preset name validation ---

  bool isKnownPresetName(const std::string& name) {
    return name == "clear"
        || name == "partlyCloudy"
        || name == "overcast"
        || name == "hazy"
        || name == "foggy"
        || name == "drizzle"
        || name == "rainstorm"
        || name == "thunderstorm"
        || name == "snow"
        || name == "blizzard"
        || name == "sandstorm"
        || name == "smoggy";
  }

  // ---------------------------------------------------------------------------
  // readPresetValues — dispatch by name to the appropriate per-preset getters.
  //
  // Returns false when the preset name is unknown (caller treats blender as
  // dormant). Each branch reads all 29 fields from RtxOptions::<preset>_<field>.
  //
  // Field order matches WEATHER_PRESET_FIELD_LIST exactly (cloud 19, atm 3,
  // sky/moon 3, volumetric 4).
  // ---------------------------------------------------------------------------
  bool readPresetValues(const std::string& name, WeatherSnapshot& out) {
    if (name == "clear") {
      // Cloud (19)
      out.cloudDensity               = RtxOptions::clear_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::clear_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::clear_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::clear_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::clear_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::clear_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::clear_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::clear_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::clear_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::clear_cloudColor();
      out.cloudWindSpeed             = RtxOptions::clear_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::clear_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::clear_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::clear_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::clear_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::clear_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::clear_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::clear_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::clear_cloudSunsetWarmth();
      // Atmosphere (3)
      out.airDensity                 = RtxOptions::clear_airDensity();
      out.aerosolDensity             = RtxOptions::clear_aerosolDensity();
      out.sunIlluminance             = RtxOptions::clear_sunIlluminance();
      // Sky/moon mood (3)
      out.nightSkyBrightness         = RtxOptions::clear_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::clear_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::clear_moonAtmosphericCouplingStrength();
      // Volumetric (4)
      out.transmittanceColor                = RtxOptions::clear_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::clear_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::clear_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::clear_volumetricAnisotropy();
    } else if (name == "partlyCloudy") {
      out.cloudDensity               = RtxOptions::partlyCloudy_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::partlyCloudy_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::partlyCloudy_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::partlyCloudy_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::partlyCloudy_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::partlyCloudy_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::partlyCloudy_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::partlyCloudy_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::partlyCloudy_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::partlyCloudy_cloudColor();
      out.cloudWindSpeed             = RtxOptions::partlyCloudy_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::partlyCloudy_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::partlyCloudy_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::partlyCloudy_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::partlyCloudy_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::partlyCloudy_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::partlyCloudy_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::partlyCloudy_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::partlyCloudy_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::partlyCloudy_airDensity();
      out.aerosolDensity             = RtxOptions::partlyCloudy_aerosolDensity();
      out.sunIlluminance             = RtxOptions::partlyCloudy_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::partlyCloudy_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::partlyCloudy_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::partlyCloudy_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::partlyCloudy_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::partlyCloudy_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::partlyCloudy_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::partlyCloudy_volumetricAnisotropy();
    } else if (name == "overcast") {
      out.cloudDensity               = RtxOptions::overcast_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::overcast_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::overcast_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::overcast_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::overcast_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::overcast_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::overcast_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::overcast_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::overcast_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::overcast_cloudColor();
      out.cloudWindSpeed             = RtxOptions::overcast_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::overcast_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::overcast_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::overcast_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::overcast_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::overcast_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::overcast_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::overcast_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::overcast_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::overcast_airDensity();
      out.aerosolDensity             = RtxOptions::overcast_aerosolDensity();
      out.sunIlluminance             = RtxOptions::overcast_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::overcast_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::overcast_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::overcast_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::overcast_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::overcast_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::overcast_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::overcast_volumetricAnisotropy();
    } else if (name == "hazy") {
      out.cloudDensity               = RtxOptions::hazy_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::hazy_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::hazy_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::hazy_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::hazy_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::hazy_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::hazy_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::hazy_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::hazy_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::hazy_cloudColor();
      out.cloudWindSpeed             = RtxOptions::hazy_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::hazy_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::hazy_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::hazy_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::hazy_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::hazy_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::hazy_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::hazy_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::hazy_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::hazy_airDensity();
      out.aerosolDensity             = RtxOptions::hazy_aerosolDensity();
      out.sunIlluminance             = RtxOptions::hazy_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::hazy_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::hazy_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::hazy_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::hazy_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::hazy_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::hazy_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::hazy_volumetricAnisotropy();
    } else if (name == "foggy") {
      out.cloudDensity               = RtxOptions::foggy_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::foggy_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::foggy_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::foggy_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::foggy_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::foggy_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::foggy_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::foggy_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::foggy_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::foggy_cloudColor();
      out.cloudWindSpeed             = RtxOptions::foggy_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::foggy_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::foggy_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::foggy_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::foggy_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::foggy_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::foggy_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::foggy_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::foggy_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::foggy_airDensity();
      out.aerosolDensity             = RtxOptions::foggy_aerosolDensity();
      out.sunIlluminance             = RtxOptions::foggy_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::foggy_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::foggy_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::foggy_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::foggy_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::foggy_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::foggy_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::foggy_volumetricAnisotropy();
    } else if (name == "drizzle") {
      out.cloudDensity               = RtxOptions::drizzle_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::drizzle_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::drizzle_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::drizzle_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::drizzle_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::drizzle_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::drizzle_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::drizzle_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::drizzle_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::drizzle_cloudColor();
      out.cloudWindSpeed             = RtxOptions::drizzle_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::drizzle_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::drizzle_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::drizzle_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::drizzle_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::drizzle_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::drizzle_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::drizzle_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::drizzle_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::drizzle_airDensity();
      out.aerosolDensity             = RtxOptions::drizzle_aerosolDensity();
      out.sunIlluminance             = RtxOptions::drizzle_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::drizzle_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::drizzle_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::drizzle_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::drizzle_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::drizzle_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::drizzle_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::drizzle_volumetricAnisotropy();
    } else if (name == "rainstorm") {
      out.cloudDensity               = RtxOptions::rainstorm_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::rainstorm_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::rainstorm_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::rainstorm_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::rainstorm_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::rainstorm_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::rainstorm_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::rainstorm_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::rainstorm_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::rainstorm_cloudColor();
      out.cloudWindSpeed             = RtxOptions::rainstorm_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::rainstorm_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::rainstorm_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::rainstorm_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::rainstorm_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::rainstorm_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::rainstorm_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::rainstorm_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::rainstorm_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::rainstorm_airDensity();
      out.aerosolDensity             = RtxOptions::rainstorm_aerosolDensity();
      out.sunIlluminance             = RtxOptions::rainstorm_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::rainstorm_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::rainstorm_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::rainstorm_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::rainstorm_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::rainstorm_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::rainstorm_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::rainstorm_volumetricAnisotropy();
    } else if (name == "thunderstorm") {
      out.cloudDensity               = RtxOptions::thunderstorm_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::thunderstorm_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::thunderstorm_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::thunderstorm_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::thunderstorm_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::thunderstorm_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::thunderstorm_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::thunderstorm_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::thunderstorm_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::thunderstorm_cloudColor();
      out.cloudWindSpeed             = RtxOptions::thunderstorm_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::thunderstorm_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::thunderstorm_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::thunderstorm_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::thunderstorm_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::thunderstorm_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::thunderstorm_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::thunderstorm_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::thunderstorm_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::thunderstorm_airDensity();
      out.aerosolDensity             = RtxOptions::thunderstorm_aerosolDensity();
      out.sunIlluminance             = RtxOptions::thunderstorm_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::thunderstorm_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::thunderstorm_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::thunderstorm_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::thunderstorm_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::thunderstorm_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::thunderstorm_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::thunderstorm_volumetricAnisotropy();
    } else if (name == "snow") {
      out.cloudDensity               = RtxOptions::snow_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::snow_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::snow_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::snow_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::snow_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::snow_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::snow_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::snow_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::snow_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::snow_cloudColor();
      out.cloudWindSpeed             = RtxOptions::snow_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::snow_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::snow_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::snow_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::snow_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::snow_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::snow_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::snow_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::snow_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::snow_airDensity();
      out.aerosolDensity             = RtxOptions::snow_aerosolDensity();
      out.sunIlluminance             = RtxOptions::snow_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::snow_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::snow_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::snow_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::snow_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::snow_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::snow_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::snow_volumetricAnisotropy();
    } else if (name == "blizzard") {
      out.cloudDensity               = RtxOptions::blizzard_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::blizzard_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::blizzard_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::blizzard_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::blizzard_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::blizzard_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::blizzard_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::blizzard_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::blizzard_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::blizzard_cloudColor();
      out.cloudWindSpeed             = RtxOptions::blizzard_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::blizzard_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::blizzard_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::blizzard_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::blizzard_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::blizzard_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::blizzard_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::blizzard_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::blizzard_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::blizzard_airDensity();
      out.aerosolDensity             = RtxOptions::blizzard_aerosolDensity();
      out.sunIlluminance             = RtxOptions::blizzard_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::blizzard_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::blizzard_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::blizzard_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::blizzard_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::blizzard_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::blizzard_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::blizzard_volumetricAnisotropy();
    } else if (name == "sandstorm") {
      out.cloudDensity               = RtxOptions::sandstorm_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::sandstorm_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::sandstorm_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::sandstorm_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::sandstorm_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::sandstorm_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::sandstorm_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::sandstorm_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::sandstorm_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::sandstorm_cloudColor();
      out.cloudWindSpeed             = RtxOptions::sandstorm_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::sandstorm_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::sandstorm_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::sandstorm_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::sandstorm_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::sandstorm_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::sandstorm_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::sandstorm_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::sandstorm_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::sandstorm_airDensity();
      out.aerosolDensity             = RtxOptions::sandstorm_aerosolDensity();
      out.sunIlluminance             = RtxOptions::sandstorm_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::sandstorm_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::sandstorm_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::sandstorm_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::sandstorm_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::sandstorm_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::sandstorm_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::sandstorm_volumetricAnisotropy();
    } else if (name == "smoggy") {
      out.cloudDensity               = RtxOptions::smoggy_cloudDensity();
      out.cloudCoverageMean          = RtxOptions::smoggy_cloudCoverageMean();
      out.cloudCoverageSpread        = RtxOptions::smoggy_cloudCoverageSpread();
      out.cloudCoverageNoiseScale    = RtxOptions::smoggy_cloudCoverageNoiseScale();
      out.cloudTypeMean              = RtxOptions::smoggy_cloudTypeMean();
      out.cloudTypeSpread            = RtxOptions::smoggy_cloudTypeSpread();
      out.cloudTypeNoiseScale        = RtxOptions::smoggy_cloudTypeNoiseScale();
      out.cloudAnvilBias             = RtxOptions::smoggy_cloudAnvilBias();
      out.cloudWindShearStrength     = RtxOptions::smoggy_cloudWindShearStrength();
      out.cloudColor                 = RtxOptions::smoggy_cloudColor();
      out.cloudWindSpeed             = RtxOptions::smoggy_cloudWindSpeed();
      out.cloudWindDirection         = RtxOptions::smoggy_cloudWindDirection();
      out.cloudShadowStrength        = RtxOptions::smoggy_cloudShadowStrength();
      out.cloudAnisotropy            = RtxOptions::smoggy_cloudAnisotropy();
      out.cloudThickness             = RtxOptions::smoggy_cloudThickness();
      out.cloudDetailWeight          = RtxOptions::smoggy_cloudDetailWeight();
      out.cloudShadowTint            = RtxOptions::smoggy_cloudShadowTint();
      out.cloudShadowTintStrength    = RtxOptions::smoggy_cloudShadowTintStrength();
      out.cloudSunsetWarmth          = RtxOptions::smoggy_cloudSunsetWarmth();
      out.airDensity                 = RtxOptions::smoggy_airDensity();
      out.aerosolDensity             = RtxOptions::smoggy_aerosolDensity();
      out.sunIlluminance             = RtxOptions::smoggy_sunIlluminance();
      out.nightSkyBrightness         = RtxOptions::smoggy_nightSkyBrightness();
      out.moonNeeStrength            = RtxOptions::smoggy_moonNeeStrength();
      out.moonAtmosphericCouplingStrength = RtxOptions::smoggy_moonAtmosphericCouplingStrength();
      out.transmittanceColor                = RtxOptions::smoggy_transmittanceColor();
      out.transmittanceMeasurementDistanceMeters = RtxOptions::smoggy_transmittanceMeasurementDistanceMeters();
      out.singleScatteringAlbedo            = RtxOptions::smoggy_singleScatteringAlbedo();
      out.volumetricAnisotropy              = RtxOptions::smoggy_volumetricAnisotropy();
    } else {
      return false;  // Unknown preset name.
    }
    return true;
  }

  // ---------------------------------------------------------------------------
  // snapshotRenderer — reads current live renderer RTX_OPTION values.
  //
  // FIELD ORDER matches WEATHER_PRESET_FIELD_LIST exactly (same 3 sites).
  // Cloud fields: RtxOptions::xxx()
  // Atmosphere fields: RtxOptions::xxx()
  // Volumetric fields: RtxGlobalVolumetrics::xxx()
  //   (anisotropy option is named 'anisotropy' in the class; the snapshot
  //    field is named 'volumetricAnisotropy' to avoid clash with cloudAnisotropy)
  // ---------------------------------------------------------------------------
  WeatherSnapshot snapshotRenderer() {
    WeatherSnapshot s;
    // Cloud (19)
    s.cloudDensity               = RtxOptions::cloudDensity();
    s.cloudCoverageMean          = RtxOptions::cloudCoverageMean();
    s.cloudCoverageSpread        = RtxOptions::cloudCoverageSpread();
    s.cloudCoverageNoiseScale    = RtxOptions::cloudCoverageNoiseScale();
    s.cloudTypeMean              = RtxOptions::cloudTypeMean();
    s.cloudTypeSpread            = RtxOptions::cloudTypeSpread();
    s.cloudTypeNoiseScale        = RtxOptions::cloudTypeNoiseScale();
    s.cloudAnvilBias             = RtxOptions::cloudAnvilBias();
    s.cloudWindShearStrength     = RtxOptions::cloudWindShearStrength();
    s.cloudColor                 = RtxOptions::cloudColor();
    s.cloudWindSpeed             = RtxOptions::cloudWindSpeed();
    s.cloudWindDirection         = RtxOptions::cloudWindDirection();
    s.cloudShadowStrength        = RtxOptions::cloudShadowStrength();
    s.cloudAnisotropy            = RtxOptions::cloudAnisotropy();
    s.cloudThickness             = RtxOptions::cloudThickness();
    s.cloudDetailWeight          = RtxOptions::cloudDetailWeight();
    s.cloudShadowTint            = RtxOptions::cloudShadowTint();
    s.cloudShadowTintStrength    = RtxOptions::cloudShadowTintStrength();
    s.cloudSunsetWarmth          = RtxOptions::cloudSunsetWarmth();
    // Atmosphere (3)
    s.airDensity                 = RtxOptions::airDensity();
    s.aerosolDensity             = RtxOptions::aerosolDensity();
    s.sunIlluminance             = RtxOptions::sunIlluminance();
    // Sky/moon mood (3)
    s.nightSkyBrightness         = RtxOptions::nightSkyBrightness();
    s.moonNeeStrength            = RtxOptions::moonNeeStrength();
    s.moonAtmosphericCouplingStrength = RtxOptions::moonAtmosphericCouplingStrength();
    // Volumetric (4) — class is RtxGlobalVolumetrics
    s.transmittanceColor                     = RtxGlobalVolumetrics::transmittanceColor();
    s.transmittanceMeasurementDistanceMeters = RtxGlobalVolumetrics::transmittanceMeasurementDistanceMeters();
    s.singleScatteringAlbedo                 = RtxGlobalVolumetrics::singleScatteringAlbedo();
    s.volumetricAnisotropy                   = RtxGlobalVolumetrics::anisotropy();
    return s;
  }

  // ---------------------------------------------------------------------------
  // writeBlendedToDerivedLayer — writes each field of interp to the Derived
  // layer of its underlying RTX_OPTION via setImmediately().
  //
  // FIELD ORDER matches WEATHER_PRESET_FIELD_LIST exactly (same 3 sites).
  // ---------------------------------------------------------------------------
  void writeBlendedToDerivedLayer(const WeatherSnapshot& interp) {
    // Cloud (19)
    RtxOptions::cloudDensityObject().setImmediately(interp.cloudDensity);
    RtxOptions::cloudCoverageMeanObject().setImmediately(interp.cloudCoverageMean);
    RtxOptions::cloudCoverageSpreadObject().setImmediately(interp.cloudCoverageSpread);
    RtxOptions::cloudCoverageNoiseScaleObject().setImmediately(interp.cloudCoverageNoiseScale);
    RtxOptions::cloudTypeMeanObject().setImmediately(interp.cloudTypeMean);
    RtxOptions::cloudTypeSpreadObject().setImmediately(interp.cloudTypeSpread);
    RtxOptions::cloudTypeNoiseScaleObject().setImmediately(interp.cloudTypeNoiseScale);
    RtxOptions::cloudAnvilBiasObject().setImmediately(interp.cloudAnvilBias);
    RtxOptions::cloudWindShearStrengthObject().setImmediately(interp.cloudWindShearStrength);
    RtxOptions::cloudColorObject().setImmediately(interp.cloudColor);
    RtxOptions::cloudWindSpeedObject().setImmediately(interp.cloudWindSpeed);
    RtxOptions::cloudWindDirectionObject().setImmediately(interp.cloudWindDirection);
    RtxOptions::cloudShadowStrengthObject().setImmediately(interp.cloudShadowStrength);
    RtxOptions::cloudAnisotropyObject().setImmediately(interp.cloudAnisotropy);
    RtxOptions::cloudThicknessObject().setImmediately(interp.cloudThickness);
    RtxOptions::cloudDetailWeightObject().setImmediately(interp.cloudDetailWeight);
    RtxOptions::cloudShadowTintObject().setImmediately(interp.cloudShadowTint);
    RtxOptions::cloudShadowTintStrengthObject().setImmediately(interp.cloudShadowTintStrength);
    RtxOptions::cloudSunsetWarmthObject().setImmediately(interp.cloudSunsetWarmth);
    // Atmosphere (3)
    RtxOptions::airDensityObject().setImmediately(interp.airDensity);
    RtxOptions::aerosolDensityObject().setImmediately(interp.aerosolDensity);
    RtxOptions::sunIlluminanceObject().setImmediately(interp.sunIlluminance);
    // Sky/moon mood (3)
    RtxOptions::nightSkyBrightnessObject().setImmediately(interp.nightSkyBrightness);
    RtxOptions::moonNeeStrengthObject().setImmediately(interp.moonNeeStrength);
    RtxOptions::moonAtmosphericCouplingStrengthObject().setImmediately(interp.moonAtmosphericCouplingStrength);
    // Volumetric (4) — class is RtxGlobalVolumetrics
    RtxGlobalVolumetrics::transmittanceColorObject().setImmediately(interp.transmittanceColor);
    RtxGlobalVolumetrics::transmittanceMeasurementDistanceMetersObject().setImmediately(interp.transmittanceMeasurementDistanceMeters);
    RtxGlobalVolumetrics::singleScatteringAlbedoObject().setImmediately(interp.singleScatteringAlbedo);
    RtxGlobalVolumetrics::anisotropyObject().setImmediately(interp.volumetricAnisotropy);
  }

} } }  // namespace dxvk::fork_weather::(anonymous)


// ---------------------------------------------------------------------------
// WeatherBlender member implementations
// ---------------------------------------------------------------------------
namespace dxvk { namespace fork_weather {

  // ---------------------------------------------------------------------------
  // update — per-frame entry point.
  //
  // Lifecycle:
  //  1. Advance clock.
  //  2. If paused: return (manual ImGui edits persist).
  //  3. Read __weather.target. If absent/empty/unknown: clear state, return.
  //  4. If target changed (new or retarget):
  //     a. First activation: snapshot current renderer state.
  //     b. Mid-blend retarget: compute current t, lerp from prev toward old
  //        target, store result as new previous snapshot.
  //     c. Update m_targetPresetName, read m_blendDurationSec, reset start.
  //  5. Compute t = clamp((now - start) / dur, 0, 1).
  //  6. applyBlendedValues(t).
  //  7. publishStateToGameStateStore(t).
  // ---------------------------------------------------------------------------
  void WeatherBlender::update(float deltaTimeSeconds) {
    m_currentTimeSec += deltaTimeSeconds;

    if (m_paused) {
      return;
    }

    // Step 3: read and validate target preset.
    std::string newTarget = readStringFromGameStateStore("__weather.target");
    if (newTarget.empty()) {
      m_targetPresetName.clear();
      m_previousPresetName.clear();
      return;
    }
    if (!isKnownPresetName(newTarget)) {
      m_targetPresetName.clear();
      m_previousPresetName.clear();
      return;
    }

    // Step 4: handle retarget or first activation.
    if (newTarget != m_targetPresetName) {
      if (m_targetPresetName.empty()) {
        // First activation: snapshot current live renderer state.
        m_previousSnapshot    = snapshotCurrentValues();
        m_previousPresetName  = "(initial)";
      } else {
        // Mid-blend retarget: capture the partially-blended state.
        float currentT = saturate(
          (m_currentTimeSec - m_blendStartTimeSec) / std::max(0.001f, m_blendDurationSec));

        // Read old target values once.
        WeatherSnapshot oldTargetValues;
        readPresetValues(m_targetPresetName, oldTargetValues);

        // Build retarget snapshot by lerping prev toward the old target at currentT.
        WeatherSnapshot retarget;
        retarget.cloudDensity            = lerp(m_previousSnapshot.cloudDensity,            oldTargetValues.cloudDensity,            currentT);
        retarget.cloudCoverageMean       = lerp(m_previousSnapshot.cloudCoverageMean,       oldTargetValues.cloudCoverageMean,       currentT);
        retarget.cloudCoverageSpread     = lerp(m_previousSnapshot.cloudCoverageSpread,     oldTargetValues.cloudCoverageSpread,     currentT);
        retarget.cloudCoverageNoiseScale = lerp(m_previousSnapshot.cloudCoverageNoiseScale, oldTargetValues.cloudCoverageNoiseScale, currentT);
        retarget.cloudTypeMean           = lerp(m_previousSnapshot.cloudTypeMean,           oldTargetValues.cloudTypeMean,           currentT);
        retarget.cloudTypeSpread         = lerp(m_previousSnapshot.cloudTypeSpread,         oldTargetValues.cloudTypeSpread,         currentT);
        retarget.cloudTypeNoiseScale     = lerp(m_previousSnapshot.cloudTypeNoiseScale,     oldTargetValues.cloudTypeNoiseScale,     currentT);
        retarget.cloudAnvilBias          = lerp(m_previousSnapshot.cloudAnvilBias,          oldTargetValues.cloudAnvilBias,          currentT);
        retarget.cloudWindShearStrength  = lerp(m_previousSnapshot.cloudWindShearStrength,  oldTargetValues.cloudWindShearStrength,  currentT);
        retarget.cloudColor              = lerpV3(m_previousSnapshot.cloudColor,             oldTargetValues.cloudColor,              currentT);
        retarget.cloudWindSpeed          = lerp(m_previousSnapshot.cloudWindSpeed,          oldTargetValues.cloudWindSpeed,          currentT);
        retarget.cloudWindDirection      = lerpAngleDeg(m_previousSnapshot.cloudWindDirection, oldTargetValues.cloudWindDirection,   currentT);
        retarget.cloudShadowStrength     = lerp(m_previousSnapshot.cloudShadowStrength,     oldTargetValues.cloudShadowStrength,     currentT);
        retarget.cloudAnisotropy         = lerp(m_previousSnapshot.cloudAnisotropy,         oldTargetValues.cloudAnisotropy,         currentT);
        retarget.cloudThickness          = lerp(m_previousSnapshot.cloudThickness,          oldTargetValues.cloudThickness,          currentT);
        retarget.cloudDetailWeight       = lerp(m_previousSnapshot.cloudDetailWeight,       oldTargetValues.cloudDetailWeight,       currentT);
        retarget.cloudShadowTint         = lerpV3(m_previousSnapshot.cloudShadowTint,        oldTargetValues.cloudShadowTint,         currentT);
        retarget.cloudShadowTintStrength = lerp(m_previousSnapshot.cloudShadowTintStrength, oldTargetValues.cloudShadowTintStrength, currentT);
        retarget.cloudSunsetWarmth       = lerp(m_previousSnapshot.cloudSunsetWarmth,       oldTargetValues.cloudSunsetWarmth,       currentT);
        retarget.airDensity              = lerp(m_previousSnapshot.airDensity,              oldTargetValues.airDensity,              currentT);
        retarget.aerosolDensity          = lerp(m_previousSnapshot.aerosolDensity,          oldTargetValues.aerosolDensity,          currentT);
        retarget.sunIlluminance          = lerpV3(m_previousSnapshot.sunIlluminance,         oldTargetValues.sunIlluminance,          currentT);
        retarget.nightSkyBrightness      = lerp(m_previousSnapshot.nightSkyBrightness,      oldTargetValues.nightSkyBrightness,      currentT);
        retarget.moonNeeStrength         = lerp(m_previousSnapshot.moonNeeStrength,         oldTargetValues.moonNeeStrength,         currentT);
        retarget.moonAtmosphericCouplingStrength = lerp(m_previousSnapshot.moonAtmosphericCouplingStrength, oldTargetValues.moonAtmosphericCouplingStrength, currentT);
        retarget.transmittanceColor                    = lerpV3(m_previousSnapshot.transmittanceColor, oldTargetValues.transmittanceColor, currentT);
        retarget.transmittanceMeasurementDistanceMeters = lerp(m_previousSnapshot.transmittanceMeasurementDistanceMeters, oldTargetValues.transmittanceMeasurementDistanceMeters, currentT);
        retarget.singleScatteringAlbedo                = lerpV3(m_previousSnapshot.singleScatteringAlbedo, oldTargetValues.singleScatteringAlbedo, currentT);
        retarget.volumetricAnisotropy                  = lerp(m_previousSnapshot.volumetricAnisotropy, oldTargetValues.volumetricAnisotropy, currentT);

        m_previousSnapshot   = retarget;
        m_previousPresetName = m_targetPresetName;
      }

      m_targetPresetName   = newTarget;
      m_blendDurationSec   = std::max(0.001f, readFloatFromGameStateStore("__weather.blend_seconds", 1.0f));
      m_blendStartTimeSec  = m_currentTimeSec;
    }

    // Step 5: compute interpolation parameter.
    float t = saturate((m_currentTimeSec - m_blendStartTimeSec) / m_blendDurationSec);

    // Step 6 + 7.
    applyBlendedValues(t);
    publishStateToGameStateStore(t);
  }

  // ---------------------------------------------------------------------------
  // showImguiSettings — placeholder for Task 4.
  // ---------------------------------------------------------------------------
  void WeatherBlender::showImguiSettings() {
    ImGui::TextDisabled("(Weather UI lands in Task 4)");
  }

  // ---------------------------------------------------------------------------
  // snapshotCurrentValues — delegates to the free helper.
  // ---------------------------------------------------------------------------
  WeatherSnapshot WeatherBlender::snapshotCurrentValues() const {
    return snapshotRenderer();
  }

  // ---------------------------------------------------------------------------
  // readTargetPresetValues — delegates to the free helper.
  // ---------------------------------------------------------------------------
  WeatherSnapshot WeatherBlender::readTargetPresetValues(const std::string& presetName) const {
    WeatherSnapshot s;
    readPresetValues(presetName, s);
    return s;
  }

  // ---------------------------------------------------------------------------
  // applyBlendedValues — lerp prev snapshot toward target at t, write to
  // Derived layer.
  //
  // FIELD ORDER matches WEATHER_PRESET_FIELD_LIST exactly (same 3 sites).
  // cloudWindDirection uses lerpAngleDeg (shortest-path angular).
  // All other floats use lerp; all Vector3s use lerpV3.
  // ---------------------------------------------------------------------------
  void WeatherBlender::applyBlendedValues(float t) {
    WeatherSnapshot targetValues;
    if (!readPresetValues(m_targetPresetName, targetValues)) {
      return;
    }

    WeatherSnapshot interp;
    // Cloud (19)
    interp.cloudDensity            = lerp(m_previousSnapshot.cloudDensity,            targetValues.cloudDensity,            t);
    interp.cloudCoverageMean       = lerp(m_previousSnapshot.cloudCoverageMean,       targetValues.cloudCoverageMean,       t);
    interp.cloudCoverageSpread     = lerp(m_previousSnapshot.cloudCoverageSpread,     targetValues.cloudCoverageSpread,     t);
    interp.cloudCoverageNoiseScale = lerp(m_previousSnapshot.cloudCoverageNoiseScale, targetValues.cloudCoverageNoiseScale, t);
    interp.cloudTypeMean           = lerp(m_previousSnapshot.cloudTypeMean,           targetValues.cloudTypeMean,           t);
    interp.cloudTypeSpread         = lerp(m_previousSnapshot.cloudTypeSpread,         targetValues.cloudTypeSpread,         t);
    interp.cloudTypeNoiseScale     = lerp(m_previousSnapshot.cloudTypeNoiseScale,     targetValues.cloudTypeNoiseScale,     t);
    interp.cloudAnvilBias          = lerp(m_previousSnapshot.cloudAnvilBias,          targetValues.cloudAnvilBias,          t);
    interp.cloudWindShearStrength  = lerp(m_previousSnapshot.cloudWindShearStrength,  targetValues.cloudWindShearStrength,  t);
    interp.cloudColor              = lerpV3(m_previousSnapshot.cloudColor,            targetValues.cloudColor,              t);
    interp.cloudWindSpeed          = lerp(m_previousSnapshot.cloudWindSpeed,          targetValues.cloudWindSpeed,          t);
    interp.cloudWindDirection      = lerpAngleDeg(m_previousSnapshot.cloudWindDirection, targetValues.cloudWindDirection,   t);  // angular wrap
    interp.cloudShadowStrength     = lerp(m_previousSnapshot.cloudShadowStrength,     targetValues.cloudShadowStrength,     t);
    interp.cloudAnisotropy         = lerp(m_previousSnapshot.cloudAnisotropy,         targetValues.cloudAnisotropy,         t);
    interp.cloudThickness          = lerp(m_previousSnapshot.cloudThickness,          targetValues.cloudThickness,          t);
    interp.cloudDetailWeight       = lerp(m_previousSnapshot.cloudDetailWeight,       targetValues.cloudDetailWeight,       t);
    interp.cloudShadowTint         = lerpV3(m_previousSnapshot.cloudShadowTint,       targetValues.cloudShadowTint,         t);
    interp.cloudShadowTintStrength = lerp(m_previousSnapshot.cloudShadowTintStrength, targetValues.cloudShadowTintStrength, t);
    interp.cloudSunsetWarmth       = lerp(m_previousSnapshot.cloudSunsetWarmth,       targetValues.cloudSunsetWarmth,       t);
    // Atmosphere (3)
    interp.airDensity              = lerp(m_previousSnapshot.airDensity,              targetValues.airDensity,              t);
    interp.aerosolDensity          = lerp(m_previousSnapshot.aerosolDensity,          targetValues.aerosolDensity,          t);
    interp.sunIlluminance          = lerpV3(m_previousSnapshot.sunIlluminance,        targetValues.sunIlluminance,          t);
    // Sky/moon mood (3)
    interp.nightSkyBrightness      = lerp(m_previousSnapshot.nightSkyBrightness,      targetValues.nightSkyBrightness,      t);
    interp.moonNeeStrength         = lerp(m_previousSnapshot.moonNeeStrength,         targetValues.moonNeeStrength,         t);
    interp.moonAtmosphericCouplingStrength = lerp(m_previousSnapshot.moonAtmosphericCouplingStrength, targetValues.moonAtmosphericCouplingStrength, t);
    // Volumetric (4)
    interp.transmittanceColor                    = lerpV3(m_previousSnapshot.transmittanceColor, targetValues.transmittanceColor, t);
    interp.transmittanceMeasurementDistanceMeters = lerp(m_previousSnapshot.transmittanceMeasurementDistanceMeters, targetValues.transmittanceMeasurementDistanceMeters, t);
    interp.singleScatteringAlbedo                = lerpV3(m_previousSnapshot.singleScatteringAlbedo, targetValues.singleScatteringAlbedo, t);
    interp.volumetricAnisotropy                  = lerp(m_previousSnapshot.volumetricAnisotropy, targetValues.volumetricAnisotropy, t);

    writeBlendedToDerivedLayer(interp);
  }

  // ---------------------------------------------------------------------------
  // publishStateToGameStateStore — writes blend progress state.
  // ---------------------------------------------------------------------------
  void WeatherBlender::publishStateToGameStateStore(float t) const {
    writeToGameStateStore("__weather.current",
      (t > 0.5f) ? m_targetPresetName : m_previousPresetName);
    writeToGameStateStore("__weather.previous", m_previousPresetName);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", t);
    writeToGameStateStore("__weather.blend_progress", buf);
  }

} }  // namespace dxvk::fork_weather


// ---------------------------------------------------------------------------
// fork_hooks stub bodies
// ---------------------------------------------------------------------------
namespace dxvk { namespace fork_hooks {

  // Per-frame weather preset blender update. Reads __weather.target and
  // __weather.blend_seconds from the GameStateStore and writes blended weather
  // params to the Derived layer of their underlying RTX_OPTIONs. Dormant when
  // no target is set — zero behavioural change vs upstream.
  //
  // Real implementation lands in Task 3 (wires WeatherBlender into RtxContext
  // per-frame and resolves m_weatherBlender). For now, both args are unused.
  void updateWeatherBlender(class RtxContext& ctx, float deltaTimeSeconds) {
    (void)ctx;
    (void)deltaTimeSeconds;
  }

  // Renders the weather preset UI inside the existing atmosphere ImGui tree.
  // Real UI implementation lands in Task 4.
  // For now this is a no-op so the linker is satisfied when Task 4 wires the
  // call site in showAtmosphereUI.
  void showWeatherUI() {
    // No-op stub. Task 4 replaces this body with the full ImGui surface.
  }

} }  // namespace dxvk::fork_hooks
