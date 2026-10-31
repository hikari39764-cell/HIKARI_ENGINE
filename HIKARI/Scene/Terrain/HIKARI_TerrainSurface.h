#pragma once

#include <algorithm>
#include "Core/Math/HIKARI_ScalarMath.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Authoring/HIKARI_FieldDraw.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
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

// 保存・読み取り・検査器・説明の項目表。範囲は SanitizeTerrainBrush の丸めと同じ（flattenHeight だけは範囲なし）。
// モードと乱数の種は筆刷の道具が決めるので、検査器には出さない。
inline constexpr auto kTerrainBrushFields = [](auto&& field) {
    using S = TerrainBrush;
    using namespace JSONFIELDS;
    field("mode", &S::mode, kTerrainBrushModeNames, Label{"Mode"}, Hidden{});
    field("radius", &S::radius, Range{0.05, 100.0}, Label{"Radius"}, Help{"Brush radius in meters."});
    field("strength", &S::strength, Range{0.0, kTerrainBrushMaximumStrength}, Label{"Strength"},
          Help{"Frame-rate-independent brush rate from zero to eight."});
    field("hardness", &S::hardness, Range{0.0, 1.0}, Label{"Hardness"}, Help{"Inner brush hardness from zero to one."});
    field("flattenHeight", &S::flattenHeight, Label{"Target Height"},
          Help{"Terrain-local target used by Set Height and Shaft."});
    field("falloff", &S::falloff, Range{0.0, 1.0}, Label{"Falloff"},
          Help{"Outer soft brush falloff from zero to one."});
    field("ridgeDirectionDegrees", &S::ridgeDirectionDegrees, Range{-180.0, 180.0}, Label{"Ridge Direction"},
          Unit{"deg"}, Help{"Ridge axis orientation in local XZ degrees."});
    field("ridgeSharpness", &S::ridgeSharpness, Range{0.05, 1.0}, Label{"Ridge Sharpness"},
          Help{"Width and sharpness of the line-shaped ridge."});
    field("terraceStepHeight", &S::terraceStepHeight, Range{0.05, 8.0}, Label{"Terrace Step Height"},
          Help{"Height increment used by the terrace brush."});
    field("shaftDepth", &S::shaftDepth, Range{0.05, 256.0}, Label{"Shaft Depth"},
          Help{"Depth sampled below the shaft rim at stroke start."});
    field("shaftWallWidth", &S::shaftWallWidth, Range{0.01, 100.0}, Label{"Wall Width"},
          Help{"Width of the heightfield transition from floor to rim."});
    field("noiseAmplitude", &S::noiseAmplitude, Range{0.0, 16.0}, Label{"Noise Amplitude"},
          Help{"Height multiplier for deterministic local noise."});
    field("noiseScale", &S::noiseScale, Range{0.05, 512.0}, Label{"Noise Scale"},
          Help{"World-space feature size of deterministic local noise."});
    field("noiseSeed", &S::noiseSeed, Label{"Noise Seed"}, Hidden{});
};

inline TerrainBrush SanitizeTerrainBrush(TerrainBrush brush) noexcept {
    return JSONFIELDS::Sanitize(std::move(brush), kTerrainBrushFields);
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

// 保存・読み取り・検査器・説明の項目表。範囲は SanitizeTerrainStylizationSettings の丸めと同じ。
inline constexpr auto kTerrainStylizationFields = [](auto&& field) {
    using S = TerrainStylizationSettings;
    using namespace JSONFIELDS;
    field("textureScale", &S::textureScale, Range{0.1, 8.0}, Label{"Texture Scale"},
          Help{"Global world-space texture tiling multiplier."});
    field("textureStrength", &S::textureStrength, Range{0.0, 2.0}, Label{"Texture Strength"},
          Help{"Texture blend and contrast strength (0..2)."});
    field("normalStrength", &S::normalStrength, Range{0.0, 2.0}, Label{"Normal Strength"},
          Help{"Global normal-map multiplier."});
    field("saturation", &S::saturation, Range{0.0, 2.0}, Label{"Saturation"},
          Help{"Global non-rock layer saturation multiplier."});
    field("shadowStrength", &S::shadowStrength, Range{0.0, 1.0}, Label{"Shadow Strength"},
          Help{"Global received-shadow multiplier."});
    field("rockSaturation", &S::rockSaturation, Range{0.0, 1.0}, Label{"Rock Saturation"},
          Help{"Neutral rock layer saturation."});
    field("facetSizeMeters", &S::facetSizeMeters, Range{0.25, 16.0}, Label{"Facet Size"}, Unit{"m"},
          Help{"World-space size of the stylized terrain facets."});
    field("facetStrength", &S::facetStrength, Range{0.0, 1.0}, Label{"Facet Strength"},
          Help{"Blend from the authored smooth normal to broad facets."});
};

inline TerrainStylizationSettings SanitizeTerrainStylizationSettings(TerrainStylizationSettings settings) noexcept {
    return JSONFIELDS::Sanitize(std::move(settings), kTerrainStylizationFields);
}

// 地形の設定（疎 chunk の中身を除いた作者値）。項目表（kTerrainSurfaceFields）が保存・読み取り・検査器・説明を決める。
// 検査器はこれだけを複製して編集する（chunk の配列は複製しない）。
struct TerrainSurfaceParams {
    std::string presetId = "sand";
    GranularMaterialParams material{};
    TerrainStylizationSettings stylization{};
    DeformParams deform{};
    ShorelineParams shoreline{};
    float heightScale = 0.32f;
    // 層 index -> preset id（層 tint 解決用）。表には載せず、chunk の層と一緒に読み書きする。
    std::vector<std::string> layerPresetIds{};
    TerrainBrush brush{};
    // 草も authored settings だけを地形コンポーネントが所有する。
    // blade/clump の実体は BuildTerrainGrassPlacementSnapshot() が
    // deterministic に再構築し、scene に個別オブジェクトを保存しない。
    TerrainGrassSettings grass{};
};

inline constexpr auto kTerrainSurfaceFields = [](auto&& field) {
    using S = TerrainSurfaceParams;
    using namespace JSONFIELDS;
    field("presetId", &S::presetId, Label{"Solid Preset"}, Help{"Solid surface appearance and deformation preset."},
          Draw{FIELD_DRAW::StringChoice<5>{{AuthoringChoice{"sand", "Sand"}, AuthoringChoice{"snow", "Snow"},
                                            AuthoringChoice{"dirt", "Dirt"}, AuthoringChoice{"grass", "Grass"},
                                            AuthoringChoice{"rock", "Rock"}}}},
          Common{});
    field("heightScale", &S::heightScale, Range{0.0, 2.0}, Label{"Height Scale"},
          Help{"Overall dent depth scale in meters."}, Common{});
    field("material", &S::material, Nested{kGranularMaterialFields}, Hidden{});
    field("deform", &S::deform, Nested{kDeformFields}, Hidden{});
    field("shoreline", &S::shoreline, Nested{kShorelineFields}, Group{"Shoreline"});
    field("stylization", &S::stylization, Nested{kTerrainStylizationFields}, Group{"Stylization"});
    field("grass", &S::grass, Nested{kTerrainGrassFields}, Group{"Grass"});
    field("brush", &S::brush, Nested{kTerrainBrushFields}, Group{"Brush"});
};

// 疎 chunk 化された固体地形。chunk はワールド XZ に整列し、
// 筆刷が届いた場所に自動で生える。水面は独立コンポーネント。
struct TerrainSurface : TerrainSurfaceParams {
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
