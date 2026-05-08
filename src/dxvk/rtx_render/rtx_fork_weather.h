#pragma once

// rtx_fork_weather.h — fork-owned weather preset declarations.
// Defines 312 RTX_OPTIONs (12 presets × 26 fields) under the
// rtx.weather.preset.<presetName> namespace.
//
// Usage: invoke DECLARE_ALL_WEATHER_PRESETS() inside the RtxOptions struct body
// (see rtx_options.h). The macro expands all 12 preset declarations inline.
//
// The WEATHER_PRESET_FIELD_LIST(X) X-macro is also preserved here for use by
// Task 2 (WeatherSnapshot struct member generation). It expands to one
// X(type, name, default) call per field.

#include "rtx_option.h"
#include "../../util/util_vector.h"

// ---------------------------------------------------------------------------
// Field-list X-macro — 26 entries, one per weather field.
// Consumed by DECLARE_WEATHER_PRESET below and also available for Task 2
// (WeatherSnapshot struct member declaration).
// Expands to: X(type, name, defaultValue)
// ---------------------------------------------------------------------------
#define WEATHER_PRESET_FIELD_LIST(X)                                                               \
  /* Cloud (19) */                                                                                 \
  X(float,   cloudDensity,                              1.0f)                                      \
  X(float,   cloudCoverageMean,                         0.5f)                                      \
  X(float,   cloudCoverageSpread,                       0.2f)                                      \
  X(float,   cloudCoverageNoiseScale,                   0.0033f)                                   \
  X(float,   cloudTypeMean,                             0.5f)                                      \
  X(float,   cloudTypeSpread,                           0.2f)                                      \
  X(float,   cloudTypeNoiseScale,                       0.0034f)                                   \
  X(float,   cloudAnvilBias,                            0.3f)                                      \
  X(float,   cloudWindShearStrength,                    0.5f)                                      \
  X(Vector3, cloudColor,                                Vector3(0.89f, 0.92f, 1.0f))               \
  X(float,   cloudWindSpeed,                            0.02f)                                     \
  X(float,   cloudWindDirection,                        45.0f)                                     \
  X(float,   cloudShadowStrength,                       0.0f)                                      \
  X(float,   cloudAnisotropy,                           0.6f)                                      \
  X(float,   cloudThickness,                            3.05f)                                     \
  X(float,   cloudDetailWeight,                         1.0f)                                      \
  X(Vector3, cloudShadowTint,                           Vector3(0.55f, 0.65f, 0.85f))              \
  X(float,   cloudShadowTintStrength,                   1.0f)                                      \
  X(float,   cloudSunsetWarmth,                         0.95f)                                     \
  /* Atmosphere (3) */                                                                             \
  X(float,   airDensity,                                1.0f)                                      \
  X(float,   aerosolDensity,                            1.0f)                                      \
  X(Vector3, sunIlluminance,                            Vector3(20.0f, 20.0f, 20.0f))              \
  /* Sky/moon mood (3) */                                                                          \
  X(float,   nightSkyBrightness,                        0.008f)                                    \
  X(float,   moonNeeStrength,                           1.0f)                                      \
  X(float,   moonAtmosphericCouplingStrength,           1.0f)                                      \
  /* Volumetric (4) — volumetricAnisotropy avoids clash with cloudAnisotropy */                   \
  X(Vector3, transmittanceColor,                        Vector3(0.999f, 0.999f, 0.999f))           \
  X(float,   transmittanceMeasurementDistanceMeters,    200.0f)                                    \
  X(Vector3, singleScatteringAlbedo,                    Vector3(0.999f, 0.999f, 0.999f))           \
  X(float,   volumetricAnisotropy,                      0.0f)

// ---------------------------------------------------------------------------
// Per-field RTX_OPTION generator — invoked once per field inside DECLARE_WEATHER_PRESET.
// Produces: RTX_OPTION("rtx.weather.preset.<N>", type, N_fieldName, default, doc)
// ---------------------------------------------------------------------------
#define WEATHER_PRESET_FIELD_DECL_(N, type, fieldName, defaultVal)                                 \
  RTX_OPTION("rtx.weather.preset." #N, type, N##_##fieldName, defaultVal,                         \
             "Weather preset '" #N "' value for " #fieldName ". Override per-game in user.conf.")

// ---------------------------------------------------------------------------
// Single-preset macro — expands all 26 RTX_OPTION declarations for preset N.
// Must be invoked inside a class body (RTX_OPTION declares inline static members).
// ---------------------------------------------------------------------------
#define DECLARE_WEATHER_PRESET(N)                                                                  \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudDensity,                           1.0f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudCoverageMean,                      0.5f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudCoverageSpread,                    0.2f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudCoverageNoiseScale,                0.0033f);        \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudTypeMean,                          0.5f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudTypeSpread,                        0.2f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudTypeNoiseScale,                    0.0034f);        \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudAnvilBias,                         0.3f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudWindShearStrength,                 0.5f);           \
  WEATHER_PRESET_FIELD_DECL_(N, Vector3, cloudColor,                             Vector3(0.89f, 0.92f, 1.0f)); \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudWindSpeed,                         0.02f);          \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudWindDirection,                     45.0f);          \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudShadowStrength,                    0.0f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudAnisotropy,                        0.6f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudThickness,                         3.05f);          \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudDetailWeight,                      1.0f);           \
  WEATHER_PRESET_FIELD_DECL_(N, Vector3, cloudShadowTint,                        Vector3(0.55f, 0.65f, 0.85f)); \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudShadowTintStrength,                1.0f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   cloudSunsetWarmth,                      0.95f);          \
  WEATHER_PRESET_FIELD_DECL_(N, float,   airDensity,                             1.0f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   aerosolDensity,                         1.0f);           \
  WEATHER_PRESET_FIELD_DECL_(N, Vector3, sunIlluminance,                         Vector3(20.0f, 20.0f, 20.0f)); \
  WEATHER_PRESET_FIELD_DECL_(N, float,   nightSkyBrightness,                     0.008f);         \
  WEATHER_PRESET_FIELD_DECL_(N, float,   moonNeeStrength,                        1.0f);           \
  WEATHER_PRESET_FIELD_DECL_(N, float,   moonAtmosphericCouplingStrength,        1.0f);           \
  WEATHER_PRESET_FIELD_DECL_(N, Vector3, transmittanceColor,                     Vector3(0.999f, 0.999f, 0.999f)); \
  WEATHER_PRESET_FIELD_DECL_(N, float,   transmittanceMeasurementDistanceMeters, 200.0f);         \
  WEATHER_PRESET_FIELD_DECL_(N, Vector3, singleScatteringAlbedo,                 Vector3(0.999f, 0.999f, 0.999f)); \
  WEATHER_PRESET_FIELD_DECL_(N, float,   volumetricAnisotropy,                   0.0f)

// ---------------------------------------------------------------------------
// Umbrella macro — invoke inside RtxOptions struct body to declare all 312
// RTX_OPTIONs (12 presets × 26 fields).
// ---------------------------------------------------------------------------
#define DECLARE_ALL_WEATHER_PRESETS()   \
  DECLARE_WEATHER_PRESET(clear);        \
  DECLARE_WEATHER_PRESET(partlyCloudy); \
  DECLARE_WEATHER_PRESET(overcast);     \
  DECLARE_WEATHER_PRESET(hazy);         \
  DECLARE_WEATHER_PRESET(foggy);        \
  DECLARE_WEATHER_PRESET(drizzle);      \
  DECLARE_WEATHER_PRESET(rainstorm);    \
  DECLARE_WEATHER_PRESET(thunderstorm); \
  DECLARE_WEATHER_PRESET(snow);         \
  DECLARE_WEATHER_PRESET(blizzard);     \
  DECLARE_WEATHER_PRESET(sandstorm);    \
  DECLARE_WEATHER_PRESET(smoggy)
