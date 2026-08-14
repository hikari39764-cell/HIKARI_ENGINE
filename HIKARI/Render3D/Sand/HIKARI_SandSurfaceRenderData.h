#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::SAND {

    constexpr std::size_t kSandSurfaceMaxGpuDataRecords = 1024u;
    constexpr std::size_t kSandSurfaceMaxHeightCells = 1024u * 1024u;

    // Scene から Render3D へ渡す narrow, GPU-neutral な入力。
    // Scene は GPU スロット配置や DirectX パッキングを所有しない。
    struct SandSurfaceRenderData {
        uint32_t gridResolution = 64u;
        MATH::Vec2 extents{ 20.0f, 20.0f };
        float heightScale = 0.32f;
        float rimStrength = 0.28f;

        MATH::Vec3 lightTint{ 0.95f, 0.84f, 0.60f };
        float sparkleStrength = 0.35f;
        MATH::Vec3 darkTint{ 0.62f, 0.47f, 0.29f };
        float rippleAmount = 0.45f;
        float pitDarkening = 0.32f;
    };

    bool SandSurfaceRenderDataEqual(
        const SandSurfaceRenderData& lhs,
        const SandSurfaceRenderData& rhs) noexcept;

} // namespace HIKARI::RENDER3D::SAND
