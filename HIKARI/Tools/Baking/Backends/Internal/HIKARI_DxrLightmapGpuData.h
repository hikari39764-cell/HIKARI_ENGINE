#pragma once

#include "Tools/Baking/Backends/HIKARI_DxrProgressiveLightmapBaker.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapBakeUtilities.h"

namespace HIKARI::TOOLS::BAKING {

    struct DxrProgressiveLightmapBaker::GpuTriangle {
        MATH::Vec4 position0{};
        MATH::Vec4 position1{};
        MATH::Vec4 position2{};
        MATH::Vec4 normal0{};
        MATH::Vec4 normal1{};
        MATH::Vec4 normal2{};
        MATH::Vec4 diffuse{};
        MATH::Vec4 emissive{};
    };

    struct DxrProgressiveLightmapBaker::GpuLight {
        MATH::Vec4 positionRange{};
        MATH::Vec4 directionOuterCone{};
        MATH::Vec4 radianceInnerCone{};
        uint32_t type = 0u;
        uint32_t padding[3]{};
    };

    struct DxrProgressiveLightmapBaker::DispatchConstants {
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t sampleIndex = 0u;
        uint32_t bounceCount = 0u;
        uint32_t triangleCount = 0u;
        uint32_t lightCount = 0u;
        uint32_t padding0 = 0u;
        uint32_t padding1 = 0u;
        MATH::Vec4 ambientSkyStrength{};
        MATH::Vec4 skyZenith{};
        MATH::Vec4 skyHorizon{};
        MATH::Vec4 skyGround{};
    };

    static_assert(
        sizeof(DxrProgressiveLightmapBaker::GpuTriangle) ==
            128u);
    static_assert(
        sizeof(INTERNAL::DxrLightmapSurfaceTexel) == 32u);
    static_assert(
        sizeof(DxrProgressiveLightmapBaker::GpuLight) ==
            64u);
    static_assert(
        sizeof(DxrProgressiveLightmapBaker::DispatchConstants) ==
            96u);

} // namespace HIKARI::TOOLS::BAKING
