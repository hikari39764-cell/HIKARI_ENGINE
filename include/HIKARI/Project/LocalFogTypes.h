#pragma once

#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectLocalFogApiVersion = 1u;
inline constexpr uint32_t kGameProjectLocalFogMaximumVolumes = 32u;

// A participating-medium box, combined with global fog BEFORE integration.
// Linear RGB, metres and inverse metres. No emission or independent lighting pass.
// Global fog distance, anisotropy and lighting controls also apply to these boxes.
struct GameProjectLocalFogVolume {
    uint32_t structSize = sizeof(GameProjectLocalFogVolume);
    uint32_t apiVersion = kGameProjectLocalFogApiVersion;
    // Unit box [-0.5, 0.5]; positive scale is its full world-space side length.
    GameProjectTransform worldTransform{};
    GameProjectFloat3 color{1.0f, 1.0f, 1.0f}; // [0,1], scattering tint
    float density = 0.1f;                      // [0,10], extinction per metre
    float scatteringIntensity = 1.0f;          // [0,8], 1 is unit scattering albedo
    float edgeFalloff = 0.2f;                  // [0,1], fraction of each half extent
    float noiseStrength = 0.5f;                // [0,1], 0 is uniform density
    float noiseScale = 0.25f;                  // [0.001,100], world-space cycles/metre
    GameProjectFloat3 noiseOffset{};           // metres; advance explicitly for wind
    uint32_t reserved = 0u;
};

static_assert(sizeof(GameProjectLocalFogVolume) == 96u);
static_assert(std::is_trivially_copyable_v<GameProjectLocalFogVolume>);

} // namespace HIKARI
