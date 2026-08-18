#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::SAND {

    constexpr std::size_t kSandSurfaceMaxGpuDataRecords = 1024u;
    constexpr std::size_t kSandSurfaceMaxHeightCells = 1024u * 1024u;

    // Scene から Render3D へ渡す narrow, GPU-neutral な入力。
    // Scene は GPU スロット配置や DirectX パッキングを所有しない。
    struct SandSurfaceRenderData {
        // 高さ・coverage・layer weight が共有する正方形グリッド解像度。
        // GPU record と水面 bathymetry も同じ単一値を読む。
        uint32_t gridResolution = 64u;
        MATH::Vec2 extents{ 20.0f, 20.0f };
        float heightScale = 0.32f;
        float rimStrength = 0.28f;

        MATH::Vec3 lightTint{ 0.95f, 0.84f, 0.60f };
        float sparkleStrength = 0.35f;
        MATH::Vec3 darkTint{ 0.62f, 0.47f, 0.29f };
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

        // 層ごとの着色 tint。空の場合は従来通りのハードコード色
        // (sand=material tint, snow=白, dirt=茶) に C++ 側で展開され、
        // 組み込み層の見た目は変わらない。設定時は layerCount 個必要。
        std::vector<MATH::Vec3> layerLightTints{};
        std::vector<MATH::Vec3> layerDarkTints{};
    };

    bool SandSurfaceRenderDataEqual(
        const SandSurfaceRenderData& lhs,
        const SandSurfaceRenderData& rhs) noexcept;

} // namespace HIKARI::RENDER3D::SAND
