#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

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
    constexpr uint64_t kTerrainProceduralMaximumCandidateVertices =
        524288u;

    inline const char* ToString(TerrainProceduralMode mode) noexcept {
        switch (mode) {
        case TerrainProceduralMode::NoiseHills: return "noise_hills";
        case TerrainProceduralMode::IslandCoast: return "island_coast";
        case TerrainProceduralMode::Ridge: return "ridge";
        case TerrainProceduralMode::BasinCrater: return "basin_crater";
        case TerrainProceduralMode::Dunes: return "dunes";
        }
        return "noise_hills";
    }

    inline const char* ToString(TerrainProceduralBlendMode mode) noexcept {
        switch (mode) {
        case TerrainProceduralBlendMode::Add: return "add";
        case TerrainProceduralBlendMode::Replace: return "replace";
        case TerrainProceduralBlendMode::Max: return "max";
        }
        return "add";
    }

    inline TerrainProceduralMode ParseTerrainProceduralMode(
        std::string_view value,
        TerrainProceduralMode fallback =
            TerrainProceduralMode::NoiseHills) noexcept {
        if (value == "noise_hills") { return TerrainProceduralMode::NoiseHills; }
        if (value == "island_coast") { return TerrainProceduralMode::IslandCoast; }
        if (value == "ridge") { return TerrainProceduralMode::Ridge; }
        if (value == "basin_crater") { return TerrainProceduralMode::BasinCrater; }
        if (value == "dunes") { return TerrainProceduralMode::Dunes; }
        return fallback;
    }

    inline bool TryParseTerrainProceduralMode(
        std::string_view value,
        TerrainProceduralMode& outMode) noexcept {
        if (value == "noise_hills") {
            outMode = TerrainProceduralMode::NoiseHills;
            return true;
        }
        if (value == "island_coast") {
            outMode = TerrainProceduralMode::IslandCoast;
            return true;
        }
        if (value == "ridge") {
            outMode = TerrainProceduralMode::Ridge;
            return true;
        }
        if (value == "basin_crater") {
            outMode = TerrainProceduralMode::BasinCrater;
            return true;
        }
        if (value == "dunes") {
            outMode = TerrainProceduralMode::Dunes;
            return true;
        }
        return false;
    }

    inline TerrainProceduralBlendMode ParseTerrainProceduralBlendMode(
        std::string_view value,
        TerrainProceduralBlendMode fallback =
            TerrainProceduralBlendMode::Add) noexcept {
        if (value == "add") { return TerrainProceduralBlendMode::Add; }
        if (value == "replace") { return TerrainProceduralBlendMode::Replace; }
        if (value == "max") { return TerrainProceduralBlendMode::Max; }
        return fallback;
    }

    inline bool TryParseTerrainProceduralBlendMode(
        std::string_view value,
        TerrainProceduralBlendMode& outMode) noexcept {
        if (value == "add") {
            outMode = TerrainProceduralBlendMode::Add;
            return true;
        }
        if (value == "replace") {
            outMode = TerrainProceduralBlendMode::Replace;
            return true;
        }
        if (value == "max") {
            outMode = TerrainProceduralBlendMode::Max;
            return true;
        }
        return false;
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

    inline TerrainProceduralSpec SanitizeTerrainProceduralSpec(
        TerrainProceduralSpec spec) noexcept {
        const auto finiteClamp = [](
            float value,
            float fallback,
            float minimum,
            float maximum) noexcept {
            return std::clamp(
                std::isfinite(value) ? value : fallback,
                minimum,
                maximum);
        };
        const uint8_t modeValue = static_cast<uint8_t>(spec.mode);
        if (modeValue > static_cast<uint8_t>(TerrainProceduralMode::Dunes)) {
            spec.mode = TerrainProceduralMode::NoiseHills;
        }
        const uint8_t blendValue = static_cast<uint8_t>(spec.blendMode);
        if (blendValue > static_cast<uint8_t>(TerrainProceduralBlendMode::Max)) {
            spec.blendMode = TerrainProceduralBlendMode::Add;
        }
        spec.centerX = finiteClamp(spec.centerX, 0.0f, -8192.0f, 8192.0f);
        spec.centerZ = finiteClamp(spec.centerZ, 0.0f, -8192.0f, 8192.0f);
        spec.extentX = finiteClamp(spec.extentX, 16.0f, 0.5f, 2048.0f);
        spec.extentZ = finiteClamp(spec.extentZ, 16.0f, 0.5f, 2048.0f);
        spec.amplitude = finiteClamp(spec.amplitude, 1.0f, -64.0f, 64.0f);
        spec.baseHeight = finiteClamp(spec.baseHeight, 0.0f, -64.0f, 64.0f);
        spec.scale = finiteClamp(spec.scale, 8.0f, 0.25f, 512.0f);
        spec.edgeFalloff = finiteClamp(spec.edgeFalloff, 0.18f, 0.01f, 1.0f);
        spec.blendAmount = finiteClamp(spec.blendAmount, 1.0f, 0.0f, 1.0f);
        spec.directionDegrees = finiteClamp(
            spec.directionDegrees, 0.0f, -180.0f, 180.0f);
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
    float EvaluateTerrainProceduralMask(
        const TerrainProceduralSpec& spec,
        float localX,
        float localZ) noexcept;
    float EvaluateTerrainProceduralHeight(
        const TerrainProceduralSpec& spec,
        float localX,
        float localZ) noexcept;
    // World-anchored deterministic noise shared by procedural generation and
    // the local Noise brush. It deliberately excludes footprint/mask logic.
    float EvaluateTerrainAuthoringNoise(
        float localX,
        float localZ,
        uint64_t seed,
        float scale,
        uint32_t octaves = 4u) noexcept;
    TerrainProceduralPreview PreviewTerrainProcedural(
        const TerrainSurface& surface,
        const TerrainProceduralSpec& spec) noexcept;

} // namespace HIKARI::TERRAIN
