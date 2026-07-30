#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakePolicy.h"
#include "Assets/Lighting/HIKARI_LightmapSurfaceContract.h"

namespace HIKARI::ASSETS::LIGHTING {

    struct LightmapAtlasRect {
        uint32_t x = 0u;
        uint32_t y = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    struct LightmapAtlasSurfaceMetrics {
        LightmapSurfaceIdentity surface{};
        MATH::Vec2 uvMin{};
        MATH::Vec2 uvMax{ 1.0f, 1.0f };
        float worldArea = 0.0f;
        float uvArea = 0.0f;
        uint32_t minimumInteriorWidth =
            kSurfaceLightmapMinimumAuthoredUvExtent;
        uint32_t minimumInteriorHeight =
            kSurfaceLightmapMinimumAuthoredUvExtent;
    };

    struct LightmapAtlasAllocation {
        LightmapSurfaceIdentity surface{};
        LightmapAtlasRect outerRect{};
        LightmapAtlasRect interiorRect{};
        MATH::Vec4 uvScaleBias{
            1.0f, 1.0f, 0.0f, 0.0f
        };
    };

    struct LightmapAtlasSettings {
        float targetTexelsPerWorldUnit =
            kSurfaceLightmapDefaultTexelsPerWorldUnit;
        uint32_t minimumResolution =
            kSurfaceLightmapMinimumAtlasResolution;
        uint32_t maximumResolution =
            kSurfaceLightmapMaximumAtlasResolution;
        uint32_t guardTexels =
            kSurfaceLightmapAtlasGuardTexels;
        uint32_t alignmentTexels =
            kSurfaceLightmapAtlasAlignmentTexels;
    };

    struct LightmapAtlasLayout {
        uint32_t resolution = 0u;
        float requestedTexelsPerWorldUnit = 0.0f;
        float resolvedTexelsPerWorldUnit = 0.0f;
        bool densityReduced = false;
        std::vector<LightmapAtlasAllocation> allocations{};
        std::string message{};

        bool IsValid() const noexcept {
            return resolution > 0u &&
                !allocations.empty();
        }
    };

    bool BuildLightmapAtlasLayout(
        const std::vector<LightmapAtlasSurfaceMetrics>& surfaces,
        const LightmapAtlasSettings& settings,
        LightmapAtlasLayout& outLayout);

} // namespace HIKARI::ASSETS::LIGHTING
