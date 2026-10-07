#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Math/HIKARI_Math3D.h"
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
    Shaft = 8,
    Noise = 9,
};

inline constexpr TEXT::EnumName<TerrainBrushMode> kTerrainBrushModeNames[] = {
    {TerrainBrushMode::Raise, "raise"},   {TerrainBrushMode::Lower, "lower"},
    {TerrainBrushMode::Smooth, "smooth"}, {TerrainBrushMode::Flatten, "flatten"},
    {TerrainBrushMode::Mound, "mound"},   {TerrainBrushMode::Pit, "pit"},
    {TerrainBrushMode::Ridge, "ridge"},   {TerrainBrushMode::Terrace, "terrace"},
    {TerrainBrushMode::Shaft, "shaft"},   {TerrainBrushMode::Noise, "noise"},
};

inline const char* ToString(TerrainBrushMode mode) noexcept {
    return TEXT::EnumToName(kTerrainBrushModeNames, mode, "raise");
}

constexpr float kTerrainBrushMaximumStrength = 8.0f;

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
    float shaftDepth = 4.0f;
    float shaftWallWidth = 0.75f;
    float noiseAmplitude = 0.35f;
    float noiseScale = 2.0f;
    uint32_t noiseSeed = 1337u;

    bool operator==(const TerrainBrush&) const = default;
};

inline TerrainBrush SanitizeTerrainBrush(TerrainBrush brush) noexcept {
    brush.radius = std::clamp(brush.radius, 0.05f, 100.0f);
    brush.strength =
        std::clamp(std::isfinite(brush.strength) ? brush.strength : 0.4f, 0.0f, kTerrainBrushMaximumStrength);
    brush.hardness = std::clamp(brush.hardness, 0.0f, 1.0f);
    brush.falloff = std::clamp(brush.falloff, 0.0f, 1.0f);
    brush.ridgeDirectionDegrees =
        std::clamp(std::isfinite(brush.ridgeDirectionDegrees) ? brush.ridgeDirectionDegrees : 0.0f, -180.0f, 180.0f);
    brush.ridgeSharpness = std::clamp(std::isfinite(brush.ridgeSharpness) ? brush.ridgeSharpness : 0.65f, 0.05f, 1.0f);
    brush.terraceStepHeight =
        std::clamp(std::isfinite(brush.terraceStepHeight) ? brush.terraceStepHeight : 0.45f, 0.05f, 8.0f);
    brush.shaftDepth = std::clamp(std::isfinite(brush.shaftDepth) ? brush.shaftDepth : 4.0f, 0.05f, 256.0f);
    brush.shaftWallWidth =
        std::clamp(std::isfinite(brush.shaftWallWidth) ? brush.shaftWallWidth : 0.75f, 0.01f, 100.0f);
    brush.noiseAmplitude = std::clamp(std::isfinite(brush.noiseAmplitude) ? brush.noiseAmplitude : 0.35f, 0.0f, 16.0f);
    brush.noiseScale = std::clamp(std::isfinite(brush.noiseScale) ? brush.noiseScale : 2.0f, 0.05f, 512.0f);
    return brush;
}

// Two-point authoring request. Coordinates are terrain-local and the
// endpoint Y values own the target slope. It is baked into normal height
// chunks and never becomes a second runtime representation.
struct TerrainRampSpec {
    MATH::Vec3 start{};
    MATH::Vec3 end{};
    float halfWidth = 2.0f;
    float shoulderWidth = 1.0f;
    float blendAmount = 1.0f;
};

inline TerrainRampSpec SanitizeTerrainRampSpec(TerrainRampSpec spec) noexcept {
    const auto finite = [](float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; };
    spec.start = {finite(spec.start.x, 0.0f), finite(spec.start.y, 0.0f), finite(spec.start.z, 0.0f)};
    spec.end = {finite(spec.end.x, spec.start.x), finite(spec.end.y, spec.start.y), finite(spec.end.z, spec.start.z)};
    spec.halfWidth = std::clamp(finite(spec.halfWidth, 2.0f), 0.05f, 256.0f);
    spec.shoulderWidth = std::clamp(finite(spec.shoulderWidth, 1.0f), 0.0f, 256.0f);
    spec.blendAmount = std::clamp(finite(spec.blendAmount, 1.0f), 0.0f, 1.0f);
    return spec;
}

// A bounded oriented volume removes only the heightfield surface patch it
// intersects. It supports cliff/vertical-face apertures while retaining
// the existing coverage authority; it does not generate tunnel walls.
struct TerrainOpeningSpec {
    MATH::Vec3 center{};
    MATH::Vec3 normal{0.0f, 0.0f, 1.0f};
    float width = 3.0f;
    float height = 3.0f;
    float depth = 2.0f;
    float feather = 0.20f;
};

inline TerrainOpeningSpec SanitizeTerrainOpeningSpec(TerrainOpeningSpec spec) noexcept {
    const auto finite = [](float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; };
    spec.center = {finite(spec.center.x, 0.0f), finite(spec.center.y, 0.0f), finite(spec.center.z, 0.0f)};
    spec.normal = {finite(spec.normal.x, 0.0f), finite(spec.normal.y, 0.0f), finite(spec.normal.z, 1.0f)};
    const float normalLengthSquared =
        spec.normal.x * spec.normal.x + spec.normal.y * spec.normal.y + spec.normal.z * spec.normal.z;
    if (normalLengthSquared <= 1.0e-8f) {
        spec.normal = {0.0f, 0.0f, 1.0f};
    } else {
        const float inverseLength = 1.0f / std::sqrt(normalLengthSquared);
        spec.normal = spec.normal * inverseLength;
    }
    spec.width = std::clamp(finite(spec.width, 3.0f), 0.10f, 256.0f);
    spec.height = std::clamp(finite(spec.height, 3.0f), 0.10f, 256.0f);
    spec.depth = std::clamp(finite(spec.depth, 2.0f), 0.10f, 256.0f);
    const float maximumFeather = (std::min)({32.0f, spec.width * 0.5f, spec.height * 0.5f, spec.depth * 0.5f});
    spec.feather = std::clamp(finite(spec.feather, 0.20f), 0.0f, maximumFeather);
    return spec;
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

    bool operator==(const TerrainStylizationSettings&) const = default;
};

inline TerrainStylizationSettings SanitizeTerrainStylizationSettings(TerrainStylizationSettings settings) noexcept {
    const auto clampFinite = [](float value, float fallback, float minimum, float maximum) noexcept {
        return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
    };
    settings.textureScale = clampFinite(settings.textureScale, 1.0f, 0.1f, 8.0f);
    settings.textureStrength = clampFinite(settings.textureStrength, 1.0f, 0.0f, 2.0f);
    settings.normalStrength = clampFinite(settings.normalStrength, 1.0f, 0.0f, 2.0f);
    settings.saturation = clampFinite(settings.saturation, 1.0f, 0.0f, 2.0f);
    settings.shadowStrength = clampFinite(settings.shadowStrength, 1.0f, 0.0f, 1.0f);
    settings.rockSaturation = clampFinite(settings.rockSaturation, 0.30f, 0.0f, 1.0f);
    settings.facetSizeMeters = clampFinite(settings.facetSizeMeters, 1.6f, 0.25f, 16.0f);
    settings.facetStrength = clampFinite(settings.facetStrength, 0.0f, 0.0f, 1.0f);
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

// 層の表面プリセット ID。明示が無ければ層 0 は地形の presetId、他の層は未設定（空）。
inline std::string_view ResolveTerrainLayerPresetId(const std::vector<std::string>& layerPresetIds,
                                                    std::string_view basePresetId, uint32_t layer) noexcept {
    if (layer < layerPresetIds.size() && !layerPresetIds[layer].empty()) {
        return layerPresetIds[layer];
    }
    return layer == 0u ? basePresetId : std::string_view{};
}
constexpr float kTerrainMaximumChunkCellSize = 1024.0f;

inline float SanitizeTerrainChunkCellSize(float value) noexcept {
    return std::clamp(std::isfinite(value) ? value : 0.25f, kTerrainMinimumChunkCellSize, kTerrainMaximumChunkCellSize);
}

inline float TerrainChunkWorldSize(const TerrainSurface& surface) noexcept {
    return static_cast<float>((std::clamp)(surface.chunkCells, 1u, kTerrainChunkMaxCells)) *
           SanitizeTerrainChunkCellSize(surface.chunkCellSize);
}

} // namespace HIKARI::TERRAIN
