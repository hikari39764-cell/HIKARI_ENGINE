#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Terrain/HIKARI_SurfacePreset.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"

namespace HIKARI::TERRAIN {

    enum class TerrainBrushMode : uint8_t {
        Raise = 0,
        Lower = 1,
        Smooth = 2,
        Flatten = 3,
        Mound = 4,
        Pit = 5,
    };

    inline const char* ToString(TerrainBrushMode mode) noexcept {
        switch (mode) {
        case TerrainBrushMode::Raise: return "raise";
        case TerrainBrushMode::Lower: return "lower";
        case TerrainBrushMode::Smooth: return "smooth";
        case TerrainBrushMode::Flatten: return "flatten";
        case TerrainBrushMode::Mound: return "mound";
        case TerrainBrushMode::Pit: return "pit";
        }
        return "raise";
    }

    inline TerrainBrushMode ParseTerrainBrushMode(
        std::string_view value,
        TerrainBrushMode fallback = TerrainBrushMode::Raise) noexcept {
        if (value == "raise") { return TerrainBrushMode::Raise; }
        if (value == "lower") { return TerrainBrushMode::Lower; }
        if (value == "smooth") { return TerrainBrushMode::Smooth; }
        if (value == "flatten") { return TerrainBrushMode::Flatten; }
        if (value == "mound") { return TerrainBrushMode::Mound; }
        if (value == "pit") { return TerrainBrushMode::Pit; }
        return fallback;
    }

    // 地形ブラシのオーサリング状態。
    struct TerrainBrush {
        TerrainBrushMode mode = TerrainBrushMode::Raise;
        float radius = 1.0f;
        float strength = 0.4f;
        float hardness = 0.5f;
        float falloff = 0.5f;
        float flattenHeight = 0.0f;
    };

    inline TerrainBrush SanitizeTerrainBrush(TerrainBrush brush) noexcept {
        brush.radius = std::clamp(brush.radius, 0.05f, 100.0f);
        brush.strength = std::clamp(brush.strength, 0.0f, 1.0f);
        brush.hardness = std::clamp(brush.hardness, 0.0f, 1.0f);
        brush.falloff = std::clamp(brush.falloff, 0.0f, 1.0f);
        return brush;
    }

    // 疎 chunk 化された固体地形。chunk はワールド XZ に整列し、
    // 筆刷が届いた場所に自動で生える。水面は独立コンポーネント。
    struct TerrainSurface {
        std::string presetId = "sand";
        GranularMaterialParams material{};
        DeformParams deform{};
        ShorelineParams shoreline{};
        float heightScale = 0.32f;
        // 層 index -> preset id（層 tint 解決用）。
        std::vector<std::string> layerPresetIds{};
        TerrainBrush brush{};

        // chunk 一辺のセル数と 1 セルのワールド寸法（メートル）。
        uint32_t chunkCells = 32u;
        float chunkCellSize = 0.25f;
        TerrainChunkMap chunks{};
    };

} // namespace HIKARI::TERRAIN
