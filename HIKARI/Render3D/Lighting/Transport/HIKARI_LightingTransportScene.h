#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakePolicy.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT {

    struct LightingTransportTriangle {
        MATH::Vec3 position[3]{};
        MATH::Vec3 normal[3]{};
        MATH::Vec2 lightmapUv[3]{};
        MATH::Vec3 diffuseReflectance{
            1.0f, 1.0f, 1.0f
        };
        MATH::Vec3 emissiveRadiance{};
        uint64_t objectId = 0u;
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
    };

    struct LightingTransportLight {
        uint32_t type = 0u;
        MATH::Vec3 position{};
        float range = 0.0f;
        MATH::Vec3 direction{ 0.0f, -1.0f, 0.0f };
        float outerConeCos = -1.0f;
        MATH::Vec3 radiance{ 1.0f, 1.0f, 1.0f };
        float innerConeCos = -1.0f;
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
        std::vector<LightingTransportSurfaceTarget> targets{};
        std::vector<LightingTransportLight> lights{};
        LightingTransportEnvironment environment{};
    };

} // namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT
