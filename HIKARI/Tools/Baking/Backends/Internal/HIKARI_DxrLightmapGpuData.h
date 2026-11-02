#pragma once

#include "Tools/Baking/Backends/HIKARI_DxrProgressiveLightmapBaker.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapBakeUtilities.h"

namespace HIKARI::TOOLS::BAKING {

namespace INTERNAL {
enum DxrLightmapRootParameter : uint32_t {
    kRootConstants,
    kRootScene,
    kRootTriangles,
    kRootTexels,
    kRootLights,
    kRootOutput,
    kRootMaterials,
    kRootTextures,
    kRootParameters,
    kRootResources,
    kRootEnvironment,
    kRootAtmosphere,
    kRootParameterCount
};
}

struct DxrProgressiveLightmapBaker::GpuTriangle {
    MATH::Vec4 position0{};
    MATH::Vec4 position1{};
    MATH::Vec4 position2{};
    MATH::Vec4 normal0{};
    MATH::Vec4 normal1{};
    MATH::Vec4 normal2{};
    MATH::Vec4 tangent[3]{};
    MATH::Vec4 materialUv[3]{};
    uint32_t materialIndex = 0;
    uint32_t padding[3]{};
};

struct DxrProgressiveLightmapBaker::GpuLight {
    MATH::Vec4 positionRange{};
    MATH::Vec4 directionOuterCone{};
    MATH::Vec4 radianceInnerCone{};
    uint32_t type = 0u;
    float angularFalloff = 1.0f;
    uint32_t reserved = 0u;
    float sourceRadius = 0.0f;
};

struct DxrProgressiveLightmapBaker::DispatchConstants {
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t sampleIndex = 0u;
    uint32_t bounceCount = 0u;
    uint32_t triangleCount = 0u;
    uint32_t lightCount = 0u;
    uint32_t skyMode = 0u;
    uint32_t skyTextureIndex = UINT32_MAX;
    uint32_t sampleGridWidth = 1u;
    uint32_t sampleGridHeight = 1u;
    uint32_t padding[2]{};
    MATH::Vec4 skyParams{}; // x 天空光の倍率、y 空の回転
    MATH::Vec4 skyTintExposure{};
};

static_assert(sizeof(DxrProgressiveLightmapBaker::GpuTriangle) == 208u);
static_assert(offsetof(DxrProgressiveLightmapBaker::GpuTriangle, materialIndex) == 192u);
static_assert(sizeof(INTERNAL::DxrLightmapSurfaceTexel) == 32u);
static_assert(sizeof(INTERNAL::DxrLightmapAccumulation) == 32u);
static_assert(sizeof(DxrProgressiveLightmapBaker::GpuLight) == 64u);
static_assert(sizeof(DxrProgressiveLightmapBaker::DispatchConstants) == 80u);

} // namespace HIKARI::TOOLS::BAKING
