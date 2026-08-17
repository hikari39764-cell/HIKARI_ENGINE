#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::WATER {

    constexpr std::size_t kWaterSurfaceWaveCount = 4u;

    enum class WaterSurfaceShadingModel : uint8_t {
        AnimeSummer = 0,
        RealisticClearSummer = 1,
    };

    struct WaterWaveRenderData {
        float directionRadians = 0.0f;
        float amplitude = 0.0f;
        float wavelength = 1.0f;
        float steepness = 0.0f;
    };

    // Scene から Render3D へ渡す narrow, GPU-neutral な値スナップショット。
    // GPU 配置と DirectX 型は renderer 側に閉じ、Scene 側へ漏らさない。
    struct WaterSurfaceRenderData {
        WaterSurfaceShadingModel shadingModel =
            WaterSurfaceShadingModel::AnimeSummer;
        float timeScale = 1.0f;
        float choppiness = 0.0f;
        float microWaveStrength = 0.0f;
        MATH::Vec3 depthTransmittance{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 scatteringColor{};
        MATH::Vec3 foamColor{ 1.0f, 1.0f, 1.0f };
        float foamAmount = 0.0f;
        float absorptionDistance = 1.0f;
        float surfaceRoughness = 0.1f;
        std::array<WaterWaveRenderData, kWaterSurfaceWaveCount> waves{};
        uint32_t coverageResolution = 256u;
        std::vector<float> liquidCoverage{};
    };

    bool WaterSurfaceRenderDataEqual(
        const WaterSurfaceRenderData& left,
        const WaterSurfaceRenderData& right) noexcept;

} // namespace HIKARI::RENDER3D::WATER
