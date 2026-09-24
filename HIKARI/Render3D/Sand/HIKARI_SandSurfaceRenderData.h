#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::SAND {

constexpr std::size_t kSandSurfaceMaxGpuDataRecords = 1024u;
constexpr std::size_t kSandSurfaceMaxHeightCells = 1024u * 1024u;
constexpr std::size_t kSandSurfaceLayerStyleFloatCount = 20u;

// 1 terrain paint layer の GPU-neutral な見た目。96-byte surface record は
// 増やさず、既存 float pool の layer payload に 5 float4 で格納する。
struct SandSurfaceLayerRenderStyle {
    MATH::Vec3 lightTint{0.95f, 0.84f, 0.60f};
    float textureStrength = 0.52f;
    MATH::Vec3 darkTint{0.62f, 0.47f, 0.29f};
    float normalStrength = 0.72f;
    // Built-in 2x2 atlas の content rect (scale.xy, bias.xy)。
    MATH::Vec4 atlasScaleBias{0.4921875f, 0.4921875f, 0.00390625f, 0.00390625f};
    // Pure Sand compatibility: approximately the former 0.01 uv scale
    // on the default 0.25 m terrain cell (one 25 m world-space tile).
    float tilesPerMeter = 1.0f / 25.0f;
    float roughness = 0.92f;
    float saturation = 1.14f;
    uint32_t materialKind = 0u;
    float shadowStrength = 1.0f;
    // sand/snow の ripple・sparkle・deformation shading だけを残す gate。
    float granularStrength = 1.0f;
    // 既存 5-float4 payload の末尾 2 scalar を使う。96-byte surface
    // record を拡張せず、広い低ポリ面の法線表現を全 pass へ渡す。
    float facetSizeMeters = 1.6f;
    float facetStrength = 0.0f;
};

// Scene から Render3D へ渡す narrow, GPU-neutral な入力。
// Scene は GPU スロット配置や DirectX パッキングを所有しない。
struct SandSurfaceRenderData {
    // 高さ・coverage・layer weight が共有する正方形グリッド解像度。
    // GPU record と水面 bathymetry も同じ単一値を読む。
    uint32_t gridResolution = 64u;
    MATH::Vec2 extents{20.0f, 20.0f};
    float heightScale = 0.32f;
    float rimStrength = 0.28f;

    MATH::Vec3 lightTint{0.95f, 0.84f, 0.60f};
    float sparkleStrength = 0.35f;
    MATH::Vec3 darkTint{0.62f, 0.47f, 0.29f};
    float rippleAmount = 0.45f;
    float pitDarkening = 0.32f;

    uint32_t toonBandCount = 3u;
    float toonBandHardness = 0.14f;
    float wetness = 0.0f;
    // 境界を沈ませる深さ（メートル）。GPU record の styleWet.w に乗せる。
    float edgeDepth = 1.5f;

    // 層ウェイト。layer-major で、各層は (gridResolution+1)^2 個。
    // 既定は 1 層（既存の単一砂地）で、このとき既存着色と完全一致する。
    uint32_t layerCount = 1u;
    std::vector<float> layerWeights{};
    std::vector<float> coverage{};

    // 層ごとの atlas / tint / normal / toon style。空の場合は従来の
    // sand tint へ縮退する。設定時は layerCount 個必要。
    std::vector<SandSurfaceLayerRenderStyle> layerStyles{};
};

bool SandSurfaceRenderDataEqual(const SandSurfaceRenderData& lhs, const SandSurfaceRenderData& rhs) noexcept;

} // namespace HIKARI::RENDER3D::SAND
