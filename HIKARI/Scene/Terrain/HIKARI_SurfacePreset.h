#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"

namespace HIKARI::TERRAIN {

enum class SurfaceLayerMaterialKind : uint8_t {
    Sand = 0,
    Snow = 1,
    Dirt = 2,
    Grass = 3,
    Rock = 4,
};

enum class DeformBehaviorId : uint8_t {
    SphereCap,
    SoftSnow,
    None,
};

struct GranularMaterialParams {
    MATH::Vec3 lightTint{0.95f, 0.84f, 0.60f};
    MATH::Vec3 darkTint{0.62f, 0.47f, 0.29f};
    float sparkleStrength = 0.35f;
    float rippleAmount = 0.45f;
    float pitDarkening = 0.32f;
    uint32_t toonBandCount = 3u;
    float toonBandHardness = 0.14f;
};

// 層ごとの着色 tint。ペイント時にこの色で混合される。
// 既定値は従来シェーダーにハードコードされていた snow/dirt と同一で、
// これにより組み込み層の見た目は一切変わらない。
struct SurfaceLayerTint {
    MATH::Vec3 light{1.0f, 1.0f, 1.0f};
    MATH::Vec3 dark{1.0f, 1.0f, 1.0f};
};

// Built-in terrain atlas の 1 layer を解決する描画スタイル。
// atlasTile は EngineContent/Terrain/README.md の固定 2x2 layout。
// texture density は world-space metersPerTile が所有し、coverage uv1 を
// タイルしてはならない。
struct SurfaceLayerMaterialParams {
    SurfaceLayerMaterialKind kind = SurfaceLayerMaterialKind::Sand;
    uint32_t atlasTile = 0u;
    float metersPerTile = 2.8f;
    float textureStrength = 0.52f;
    float normalStrength = 0.72f;
    float roughness = 0.92f;
    float saturation = 1.10f;
    float shadowStrength = 1.0f;
};

struct DeformParams {
    DeformBehaviorId behavior = DeformBehaviorId::SphereCap;
    float maxDepth = 0.30f;
    float stampRadius = 0.58f;
    float rimStrength = 0.32f;
    // runtime response は authored terrain より細かい疎格子で保持する。
    // 2 倍なら既定 0.25 m cell が 0.125 m になり、小さな足跡も潰れない。
    uint32_t responseResolutionScale = 2u;
    // 接触は変形の gate にだけ使い、深さはアート調整可能な profile で決める。
    // 物理 solver の侵入量や質量が 0 に近くても一定の見える痕跡を作れる。
    float restDepthScale = 0.36f;
    float minimumDepth = 0.055f;
    float settleStartRatio = 0.72f;
    float settleSeconds = 0.18f;
    float impactDepthScale = 0.03f;
    float rimWidthScale = 0.42f;
    // elastic は緩やかに戻り、plastic は長時間痕跡として残る。
    // diffusion は elastic/loose sand にだけ作用させる。
    float refillPerSecond = 0.008f;
    float diffusionPerSecond = 0.04f;
    float plasticity = 0.93f;
    float plasticRefillPerSecond = 0.0003f;
    float recoveryDelaySeconds = 8.0f;

    bool operator==(const DeformParams&) const = default;
};

struct ShorelineParams {
    float edgeFalloff = 4.0f;
    float edgeDepth = 1.5f;
    float wetness = 0.0f;

    bool operator==(const ShorelineParams&) const = default;
};

// 保存・読み取り・説明の項目表。材質と変形は地表の preset が決める値で、TerrainSurface の項目表では検査器に出さない。
inline constexpr auto kGranularMaterialFields = [](auto&& field) {
    using S = GranularMaterialParams;
    using namespace JSONFIELDS;
    field("lightTint", &S::lightTint, Color{});
    field("darkTint", &S::darkTint, Color{});
    field("sparkleStrength", &S::sparkleStrength);
    field("rippleAmount", &S::rippleAmount);
    field("pitDarkening", &S::pitDarkening);
    field("toonBandCount", &S::toonBandCount);
    field("toonBandHardness", &S::toonBandHardness);
};

// behavior は名前の表を持たず、列挙の番号のまま保存する。
inline constexpr auto kDeformFields = [](auto&& field) {
    using S = DeformParams;
    using namespace JSONFIELDS;
    field("behavior", &S::behavior);
    field("maxDepth", &S::maxDepth);
    field("stampRadius", &S::stampRadius);
    field("rimStrength", &S::rimStrength);
    field("responseResolutionScale", &S::responseResolutionScale);
    field("restDepthScale", &S::restDepthScale);
    field("minimumDepth", &S::minimumDepth);
    field("settleStartRatio", &S::settleStartRatio);
    field("settleSeconds", &S::settleSeconds);
    field("impactDepthScale", &S::impactDepthScale);
    field("rimWidthScale", &S::rimWidthScale);
    field("refillPerSecond", &S::refillPerSecond);
    field("diffusionPerSecond", &S::diffusionPerSecond);
    field("plasticity", &S::plasticity);
    field("plasticRefillPerSecond", &S::plasticRefillPerSecond);
    field("recoveryDelaySeconds", &S::recoveryDelaySeconds);
};

inline constexpr auto kShorelineFields = [](auto&& field) {
    using S = ShorelineParams;
    using namespace JSONFIELDS;
    field("edgeFalloff", &S::edgeFalloff, Range{0.0, 40.0}, Label{"Edge Falloff"},
          Help{"Shoreline transition width in meters."});
    field("edgeDepth", &S::edgeDepth, Range{0.0, 20.0}, Label{"Edge Depth"}, Help{"Shoreline sink depth in meters."});
    field("wetness", &S::wetness, Range{0.0, 1.0}, Label{"Wetness"},
          Help{"Shoreline wet-surface strength (0 disables)."});
};

// 実心地表の preset。砂 / 雪 / 泥 / 草 / 岩はこの構造を差し替える。
struct SurfacePreset {
    std::string id{};
    GranularMaterialParams material{};
    DeformParams deform{};
    ShorelineParams shoreline{};
    SurfaceLayerTint tint{}; // この層をペイントしたときの色
    SurfaceLayerMaterialParams layerMaterial{};
};

inline SurfacePreset MakeSandPreset() {
    SurfacePreset preset{};
    preset.id = "sand";
    preset.material = {};
    preset.deform.behavior = DeformBehaviorId::SphereCap;
    preset.shoreline = {};
    // 基底層 tint はマテリアルの tint と同一にする。シェーダーは
    // 全層を層 tint で一様に混合するため、単層時に w0=1 で既存の
    // p.lightTint/p.darkTint と一致し、見た目が変わらない。
    preset.tint.light = preset.material.lightTint;
    preset.tint.dark = preset.material.darkTint;
    preset.layerMaterial = {};
    // Preserve the pre-layer Sand baseline: the old terrain uv0 used a
    // 0.01 material scale over the default 0.25 m terrain cell, which is
    // approximately one 25 m world-space tile. Other built-in layers
    // intentionally keep their newer, denser world-space scales.
    preset.layerMaterial.metersPerTile = 25.0f;
    preset.layerMaterial.textureStrength = 0.52f;
    preset.layerMaterial.normalStrength = 0.72f;
    preset.layerMaterial.saturation = 1.14f;
    return preset;
}

inline SurfacePreset MakeSnowPreset() {
    SurfacePreset preset = MakeSandPreset();
    preset.id = "snow";
    preset.material.lightTint = {1.0f, 1.0f, 1.0f};
    preset.material.darkTint = {0.68f, 0.78f, 0.90f};
    preset.deform.behavior = DeformBehaviorId::SoftSnow;
    preset.deform.maxDepth = 0.45f;
    preset.deform.restDepthScale = 0.38f;
    preset.deform.minimumDepth = 0.05f;
    preset.deform.settleStartRatio = 0.40f;
    preset.deform.settleSeconds = 0.18f;
    preset.deform.rimWidthScale = 0.40f;
    preset.deform.refillPerSecond = 0.004f;
    preset.deform.diffusionPerSecond = 0.025f;
    preset.deform.plasticity = 0.97f;
    preset.deform.plasticRefillPerSecond = 0.00015f;
    preset.deform.recoveryDelaySeconds = 12.0f;
    preset.tint.light = {1.0f, 1.0f, 1.0f};
    preset.tint.dark = {0.68f, 0.78f, 0.90f};
    preset.layerMaterial.kind = SurfaceLayerMaterialKind::Snow;
    preset.layerMaterial.atlasTile = 0u;
    preset.layerMaterial.metersPerTile = 2.8f;
    preset.layerMaterial.textureStrength = 0.30f;
    preset.layerMaterial.normalStrength = 0.42f;
    preset.layerMaterial.roughness = 0.96f;
    preset.layerMaterial.saturation = 0.18f;
    return preset;
}

inline SurfacePreset MakeDirtPreset() {
    SurfacePreset preset = MakeSandPreset();
    preset.id = "dirt";
    preset.material.lightTint = {0.52f, 0.38f, 0.24f};
    preset.material.darkTint = {0.28f, 0.20f, 0.12f};
    preset.material.sparkleStrength = 0.0f;
    preset.deform.behavior = DeformBehaviorId::None;
    preset.tint.light = {0.52f, 0.38f, 0.24f};
    preset.tint.dark = {0.28f, 0.20f, 0.12f};
    preset.layerMaterial.kind = SurfaceLayerMaterialKind::Dirt;
    preset.layerMaterial.atlasTile = 1u;
    preset.layerMaterial.metersPerTile = 1.4f;
    preset.layerMaterial.textureStrength = 0.72f;
    preset.layerMaterial.normalStrength = 0.58f;
    preset.layerMaterial.roughness = 0.95f;
    preset.layerMaterial.saturation = 0.78f;
    return preset;
}

inline SurfacePreset MakeGrassPreset() {
    SurfacePreset preset = MakeSandPreset();
    preset.id = "grass";
    preset.material.lightTint = {0.68f, 0.90f, 0.50f};
    preset.material.darkTint = {0.18f, 0.32f, 0.12f};
    preset.material.sparkleStrength = 0.0f;
    preset.material.rippleAmount = 0.0f;
    preset.deform.behavior = DeformBehaviorId::None;
    preset.tint.light = preset.material.lightTint;
    preset.tint.dark = preset.material.darkTint;
    preset.layerMaterial.kind = SurfaceLayerMaterialKind::Grass;
    preset.layerMaterial.atlasTile = 2u;
    preset.layerMaterial.metersPerTile = 1.4f;
    preset.layerMaterial.textureStrength = 0.68f;
    preset.layerMaterial.normalStrength = 0.48f;
    preset.layerMaterial.roughness = 0.92f;
    preset.layerMaterial.saturation = 0.82f;
    return preset;
}

inline SurfacePreset MakeRockPreset() {
    SurfacePreset preset = MakeSandPreset();
    preset.id = "rock";
    preset.material.lightTint = {0.86f, 0.86f, 0.86f};
    preset.material.darkTint = {0.30f, 0.30f, 0.30f};
    preset.material.sparkleStrength = 0.0f;
    preset.material.rippleAmount = 0.0f;
    preset.deform.behavior = DeformBehaviorId::None;
    preset.tint.light = preset.material.lightTint;
    preset.tint.dark = preset.material.darkTint;
    preset.layerMaterial.kind = SurfaceLayerMaterialKind::Rock;
    preset.layerMaterial.atlasTile = 3u;
    preset.layerMaterial.metersPerTile = 1.4f;
    preset.layerMaterial.textureStrength = 0.76f;
    preset.layerMaterial.normalStrength = 0.74f;
    preset.layerMaterial.roughness = 0.88f;
    // ユーザー指定の中性 grey。元の brown/green 成分を残しすぎない。
    preset.layerMaterial.saturation = 0.30f;
    return preset;
}

// ビルトイン preset のフォールバック解決。registry が主経路で、
// これは registry 未初期化時の安全網として残す。
inline std::optional<SurfacePreset> ResolveSurfacePreset(std::string_view id) {
    if (id == "sand") {
        return MakeSandPreset();
    }
    if (id == "snow") {
        return MakeSnowPreset();
    }
    if (id == "dirt") {
        return MakeDirtPreset();
    }
    if (id == "grass") {
        return MakeGrassPreset();
    }
    if (id == "rock") {
        return MakeRockPreset();
    }
    return std::nullopt;
}
} // namespace HIKARI::TERRAIN
