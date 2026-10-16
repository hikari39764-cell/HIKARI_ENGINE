#pragma once

#include <algorithm>
#include "Core/Math/HIKARI_ScalarMath.h"
#include <cmath>
#include <cstdint>
#include <string_view>

#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"

namespace HIKARI::TERRAIN {

// Deterministic editor-only heightfield authoring profiles. The profile is
// a session value, not serialized scene data; ApplyProceduralTerrain bakes
// its result into the existing TerrainSurface chunks.
enum class TerrainProceduralMode : uint8_t {
    NoiseHills = 0,
    IslandCoast = 1,
    Ridge = 2,
    BasinCrater = 3,
    Dunes = 4,
};

enum class TerrainProceduralBlendMode : uint8_t {
    Add = 0,
    Replace = 1,
    Max = 2,
};

// Procedural bakes serialize complete touched chunks into a reversible
// document delta. Keep a hard main-thread admission budget so a single
// editor click cannot allocate hundreds of millions of samples or an
// unbounded JSON history entry. Larger worlds are authored in tiles.
constexpr uint64_t kTerrainProceduralMaximumCandidateVertices = 524288u;

inline constexpr TEXT::EnumName<TerrainProceduralMode> kTerrainProceduralModeNames[] = {
    {TerrainProceduralMode::NoiseHills, "noise_hills"},
    {TerrainProceduralMode::IslandCoast, "island_coast"},
    {TerrainProceduralMode::Ridge, "ridge"},
    {TerrainProceduralMode::BasinCrater, "basin_crater"},
    {TerrainProceduralMode::Dunes, "dunes"},
};

inline constexpr TEXT::EnumName<TerrainProceduralBlendMode> kTerrainProceduralBlendModeNames[] = {
    {TerrainProceduralBlendMode::Add, "add"},
    {TerrainProceduralBlendMode::Replace, "replace"},
    {TerrainProceduralBlendMode::Max, "max"},
};

inline const char* ToString(TerrainProceduralMode mode) noexcept {
    return TEXT::EnumToName(kTerrainProceduralModeNames, mode, "noise_hills");
}

inline const char* ToString(TerrainProceduralBlendMode mode) noexcept {
    return TEXT::EnumToName(kTerrainProceduralBlendModeNames, mode, "add");
}

struct TerrainProceduralSpec {
    TerrainProceduralMode mode = TerrainProceduralMode::NoiseHills;
    TerrainProceduralBlendMode blendMode = TerrainProceduralBlendMode::Add;
    uint64_t seed = 1337u;
    float centerX = 0.0f;
    float centerZ = 0.0f;
    float extentX = 16.0f;
    float extentZ = 16.0f;
    float amplitude = 1.0f;
    float baseHeight = 0.0f;
    float scale = 8.0f;
    float edgeFalloff = 0.18f;
    float blendAmount = 1.0f;
    float directionDegrees = 0.0f;
    uint32_t octaves = 4u;
};

inline TerrainProceduralSpec SanitizeTerrainProceduralSpec(TerrainProceduralSpec spec) noexcept {
    const uint8_t modeValue = static_cast<uint8_t>(spec.mode);
    if (modeValue > static_cast<uint8_t>(TerrainProceduralMode::Dunes)) {
        spec.mode = TerrainProceduralMode::NoiseHills;
    }
    const uint8_t blendValue = static_cast<uint8_t>(spec.blendMode);
    if (blendValue > static_cast<uint8_t>(TerrainProceduralBlendMode::Max)) {
        spec.blendMode = TerrainProceduralBlendMode::Add;
    }
    spec.centerX = MATH::ClampFinite(spec.centerX, 0.0f, -8192.0f, 8192.0f);
    spec.centerZ = MATH::ClampFinite(spec.centerZ, 0.0f, -8192.0f, 8192.0f);
    spec.extentX = MATH::ClampFinite(spec.extentX, 16.0f, 0.5f, 2048.0f);
    spec.extentZ = MATH::ClampFinite(spec.extentZ, 16.0f, 0.5f, 2048.0f);
    spec.amplitude = MATH::ClampFinite(spec.amplitude, 1.0f, -64.0f, 64.0f);
    spec.baseHeight = MATH::ClampFinite(spec.baseHeight, 0.0f, -64.0f, 64.0f);
    spec.scale = MATH::ClampFinite(spec.scale, 8.0f, 0.25f, 512.0f);
    spec.edgeFalloff = MATH::ClampFinite(spec.edgeFalloff, 0.18f, 0.01f, 1.0f);
    spec.blendAmount = MATH::ClampFinite(spec.blendAmount, 1.0f, 0.0f, 1.0f);
    spec.directionDegrees = MATH::ClampFinite(spec.directionDegrees, 0.0f, -180.0f, 180.0f);
    spec.octaves = std::clamp(spec.octaves, 1u, 8u);
    return spec;
}

struct TerrainProceduralPreview {
    bool valid = false;
    bool budgetExceeded = false;
    // These are generator candidates inside the authored footprint.
    // Seam normalization may include existing neighbour chunks in the
    // final reversible delta; the admission budget already reserves that
    // one-chunk closure even though the UI does not present it as newly
    // generated terrain.
    uint32_t affectedChunks = 0u;
    uint32_t newChunks = 0u;
    uint64_t candidateVertices = 0u;
    uint64_t affectedVertices = 0u;
    float minimumGeneratedHeight = 0.0f;
    float maximumGeneratedHeight = 0.0f;
};

// Pure deterministic evaluators shared by the editor preview, the bake
// path, and focused contract tests. Coordinates are terrain-local X/Z.
float EvaluateTerrainProceduralMask(const TerrainProceduralSpec& spec, float localX, float localZ) noexcept;
float EvaluateTerrainProceduralHeight(const TerrainProceduralSpec& spec, float localX, float localZ) noexcept;
// World-anchored deterministic noise shared by procedural generation and
// the local Noise brush. It deliberately excludes footprint/mask logic.
float EvaluateTerrainAuthoringNoise(float localX, float localZ, uint64_t seed, float scale,
                                    uint32_t octaves = 4u) noexcept;
TerrainProceduralPreview PreviewTerrainProcedural(const TerrainSurface& surface,
                                                  const TerrainProceduralSpec& spec) noexcept;

} // namespace HIKARI::TERRAIN
