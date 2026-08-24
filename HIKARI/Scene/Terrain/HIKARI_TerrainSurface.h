#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Terrain/HIKARI_SurfacePreset.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/Terrain/HIKARI_TerrainGrass.h"

namespace HIKARI::TERRAIN {

    enum class TerrainBrushMode : uint8_t {
        Raise = 0,
        Lower = 1,
        Smooth = 2,
        Flatten = 3,
        Mound = 4,
        Pit = 5,
        Ridge = 6,
        Terrace = 7,
    };

    inline const char* ToString(TerrainBrushMode mode) noexcept {
        switch (mode) {
        case TerrainBrushMode::Raise: return "raise";
        case TerrainBrushMode::Lower: return "lower";
        case TerrainBrushMode::Smooth: return "smooth";
        case TerrainBrushMode::Flatten: return "flatten";
        case TerrainBrushMode::Mound: return "mound";
        case TerrainBrushMode::Pit: return "pit";
        case TerrainBrushMode::Ridge: return "ridge";
        case TerrainBrushMode::Terrace: return "terrace";
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
        if (value == "ridge") { return TerrainBrushMode::Ridge; }
        if (value == "terrace") { return TerrainBrushMode::Terrace; }
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
        float ridgeDirectionDegrees = 0.0f;
        float ridgeSharpness = 0.65f;
        float terraceStepHeight = 0.45f;
    };

    inline TerrainBrush SanitizeTerrainBrush(TerrainBrush brush) noexcept {
        brush.radius = std::clamp(brush.radius, 0.05f, 100.0f);
        brush.strength = std::clamp(brush.strength, 0.0f, 1.0f);
        brush.hardness = std::clamp(brush.hardness, 0.0f, 1.0f);
        brush.falloff = std::clamp(brush.falloff, 0.0f, 1.0f);
        brush.ridgeDirectionDegrees = std::clamp(
            std::isfinite(brush.ridgeDirectionDegrees)
                ? brush.ridgeDirectionDegrees : 0.0f,
            -180.0f,
            180.0f);
        brush.ridgeSharpness = std::clamp(
            std::isfinite(brush.ridgeSharpness)
                ? brush.ridgeSharpness : 0.65f,
            0.05f,
            1.0f);
        brush.terraceStepHeight = std::clamp(
            std::isfinite(brush.terraceStepHeight)
                ? brush.terraceStepHeight : 0.45f,
            0.05f,
            8.0f);
        return brush;
    }

    // 地表レイヤーの見た目だけを調整する authored render 用倍率。
    // geometry / coverage の所有権や値は既存設定に残す。
    struct TerrainStylizationSettings {
        float textureScale = 1.0f;
        float textureStrength = 1.0f;
        float normalStrength = 1.0f;
        float saturation = 1.0f;
        float shadowStrength = 1.0f;
        float rockSaturation = 0.30f;
        // 高密度 heightfield をそのまま flat shading すると微細な三角ノイズに
        // なるため、見た目上の大きな facet 尺度と混合率を明示的に所有する。
        float facetSizeMeters = 1.6f;
        float facetStrength = 0.0f;
    };

    inline TerrainStylizationSettings SanitizeTerrainStylizationSettings(
        TerrainStylizationSettings settings) noexcept {
        const auto clampFinite = [](
            float value,
            float fallback,
            float minimum,
            float maximum) noexcept {
            return std::clamp(
                std::isfinite(value) ? value : fallback,
                minimum,
                maximum);
        };
        settings.textureScale = clampFinite(
            settings.textureScale, 1.0f, 0.1f, 8.0f);
        settings.textureStrength = clampFinite(
            settings.textureStrength, 1.0f, 0.0f, 2.0f);
        settings.normalStrength = clampFinite(
            settings.normalStrength, 1.0f, 0.0f, 2.0f);
        settings.saturation = clampFinite(
            settings.saturation, 1.0f, 0.0f, 2.0f);
        settings.shadowStrength = clampFinite(
            settings.shadowStrength, 1.0f, 0.0f, 1.0f);
        settings.rockSaturation = clampFinite(
            settings.rockSaturation, 0.30f, 0.0f, 1.0f);
        settings.facetSizeMeters = clampFinite(
            settings.facetSizeMeters, 1.6f, 0.25f, 16.0f);
        settings.facetStrength = clampFinite(
            settings.facetStrength, 0.0f, 0.0f, 1.0f);
        return settings;
    }

    // 疎 chunk 化された固体地形。chunk はワールド XZ に整列し、
    // 筆刷が届いた場所に自動で生える。水面は独立コンポーネント。
    struct TerrainSurface {
        std::string presetId = "sand";
        GranularMaterialParams material{};
        TerrainStylizationSettings stylization{};
        DeformParams deform{};
        ShorelineParams shoreline{};
        float heightScale = 0.32f;
        // 層 index -> preset id（層 tint 解決用）。
        std::vector<std::string> layerPresetIds{};
        TerrainBrush brush{};
        // 草も authored settings だけを地形コンポーネントが所有する。
        // blade/clump の実体は BuildTerrainGrassPlacementSnapshot() が
        // deterministic に再構築し、scene に個別オブジェクトを保存しない。
        TerrainGrassSettings grass{};

        // chunk 一辺のセル数と 1 セルのワールド寸法（メートル）。
        uint32_t chunkCells = 32u;
        float chunkCellSize = 0.25f;
        TerrainChunkMap chunks{};
    };

    constexpr float kTerrainMinimumChunkCellSize = 0.01f;
    constexpr float kTerrainMaximumChunkCellSize = 1024.0f;

    inline float SanitizeTerrainChunkCellSize(float value) noexcept {
        return std::clamp(
            std::isfinite(value) ? value : 0.25f,
            kTerrainMinimumChunkCellSize,
            kTerrainMaximumChunkCellSize);
    }

    inline float TerrainChunkWorldSize(
        const TerrainSurface& surface) noexcept {
        return static_cast<float>((std::clamp)(
            surface.chunkCells,
            1u,
            kTerrainChunkMaxCells)) *
            SanitizeTerrainChunkCellSize(surface.chunkCellSize);
    }

} // namespace HIKARI::TERRAIN
