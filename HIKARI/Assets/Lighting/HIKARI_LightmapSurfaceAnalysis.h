#pragma once

#include <cstdint>
#include <string>

#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"

namespace HIKARI::ASSETS::LIGHTING {

enum class LightmapSurfaceIssue : uint32_t {
    None = 0u,
    UnsupportedVertexLayout = 1u << 0,
    MorphTargetsUnsupported = 1u << 1,
    MissingGeometry = 1u << 2,
    IndexOutOfRange = 1u << 3,
    UvNonFinite = 1u << 4,
    UvOutsideUnitRange = 1u << 5,
    UvDegenerate = 1u << 6,
    AlphaMaskedSurface = 1u << 7,
    BlendedSurface = 1u << 8,
    UvOverlap = 1u << 9,
    MissingLightmapUv = 1u << 10,
};

struct LightmapSurfaceAnalysis {
    uint32_t issueMask = 0u;
    uint32_t triangleCount = 0u;

    bool IsEligible() const noexcept { return issueMask == 0u && triangleCount > 0u; }

    bool HasIssue(LightmapSurfaceIssue issue) const noexcept {
        return (issueMask & static_cast<uint32_t>(issue)) != 0u;
    }
};

LightmapSurfaceAnalysis AnalyzeLightmapSurface(const MeshPrimitive& primitive, AlphaMode alphaMode);

std::string DescribeLightmapSurfaceAnalysis(const LightmapSurfaceAnalysis& analysis);

} // namespace HIKARI::ASSETS::LIGHTING
