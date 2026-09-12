#pragma once

#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakePolicy.h"
#include "Assets/Lighting/HIKARI_LightmapAtlasLayout.h"
#include "Assets/Lighting/HIKARI_LightmapSurfaceContract.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Material/HIKARI_MaterialSnapshot.h"

namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT {

    struct LightingTransportTriangle {
        MATH::Vec3 position[3]{};
        MATH::Vec3 normal[3]{};
        MATH::Vec2 lightmapUv[3]{};
        // atlas 配置で lightmapUv を変更しても、材質の UV は保持する。
        MATH::Vec2 materialUv0[3]{};
        MATH::Vec2 materialUv1[3]{};
        MATH::Vec4 tangent[3]{};
        uint32_t materialIndex = MATERIAL::kInvalidGpuMaterialIndex;
        uint64_t objectId = 0u;
        uint32_t lightmapAllocationIndex =
            ASSETS::LIGHTING::
                kInvalidLightmapSurfaceIndex;
    };

    struct LightingTransportSurfaceTarget {
        uint64_t objectId = 0u;
        std::string name{};
        uint32_t outputResolution =
            ASSETS::LIGHTING::
                kSurfaceLightmapDefaultResolution;
        uint32_t uvSet =
            ASSETS::LIGHTING::kSurfaceLightmapUvSet;
        bool eligible = false;
        std::string eligibilityMessage{};
        std::vector<uint32_t> triangleIndices{};
        std::vector<
            ASSETS::LIGHTING::LightmapSurfaceBinding>
                surfaceBindings{};
        std::vector<
            ASSETS::LIGHTING::LightmapAtlasAllocation>
                atlasAllocations{};
        float requestedTexelsPerWorldUnit = 0.0f;
        float resolvedTexelsPerWorldUnit = 0.0f;
        bool atlasDensityReduced = false;
        uint32_t analyzedSurfaceCount = 0u;
        uint32_t eligibleSurfaceCount = 0u;
        uint32_t unsupportedSurfaceCount = 0u;
        uint32_t invalidUvSurfaceCount = 0u;
        uint32_t alphaMaskedSurfaceCount = 0u;
        uint32_t blendedSurfaceCount = 0u;
    };

    struct LightingTransportLight {
        uint32_t type = 0u;
        MATH::Vec3 position{};
        float range = 0.0f;
        MATH::Vec3 direction{ 0.0f, -1.0f, 0.0f };
        float outerConeCos = -1.0f;
        MATH::Vec3 radiance{ 1.0f, 1.0f, 1.0f };
        float innerConeCos = -1.0f;
        float angularFalloff = 1.0f;
        LocalLightFalloff distanceFalloff = LocalLightFalloff::LegacyRange;
    };

    struct LightingTransportEnvironment {
        MATH::Vec3 ambientRadiance{
            0.05f, 0.05f, 0.05f
        };
        MATH::Vec3 skyZenithRadiance{
            0.08f, 0.22f, 0.55f
        };
        MATH::Vec3 skyHorizonRadiance{
            0.65f, 0.78f, 0.95f
        };
        MATH::Vec3 skyGroundRadiance{
            0.08f, 0.08f, 0.10f
        };
        float skyStrength = 0.0f;
    };

    struct LightingTransportScene {
        std::vector<LightingTransportTriangle> triangles{};
        std::vector<MATERIAL::MaterialSnapshot> materials{};
        std::vector<LightingTransportSurfaceTarget> targets{};
        std::vector<LightingTransportLight> lights{};
        LightingTransportEnvironment environment{};
    };

} // namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT
