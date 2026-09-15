#pragma once

#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeContract.h"

namespace HIKARI::RENDER3D::LIGHTING {

    // Scene は任意数の Provider を保持できる。描画 View は候補を絞り、
    // GPU に同時公開する Provider 数だけをこの固定予算で制限する。
    inline constexpr size_t kLightingProviderViewSlotCount = 2u;

    // solar ray に直交する列と、その列内の累積透過率。未公開は descriptor=0。
    struct CloudSolarVisibilityGpuData {
        MATH::Vec4 originDescriptor{0,0,0,0};
        MATH::Vec4 rightExtent{0,0,0,0};
        MATH::Vec4 upResolution{0,0,0,0};
        MATH::Vec4 directionSteps{0,0,0,0};
    };
    static_assert(sizeof(CloudSolarVisibilityGpuData)==64);

    enum class SceneLightType : uint8_t {
        Directional = 0,
        Point,
        Spot,
    };

    struct SceneLightProvider {
        std::string id{};
        std::string name{};
        bool enabled = false;
        SceneLightType type = SceneLightType::Point;
        MATH::Vec3 position{};
        MATH::Vec3 direction{ 0.0f, 0.0f, 1.0f };
        MATH::Vec3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        float range = 5.0f;
        float sourceRadius = 0.0f;
        bool castsShadows = false;
        float innerConeAngleDegrees = 25.0f;
        float outerConeAngleDegrees = 35.0f;
        float angularFalloff = 1.0f;
        LocalLightFalloff distanceFalloff = LocalLightFalloff::LegacyRange;
        int priority = 0;
    };

    struct ReflectionProbeProvider {
        std::string id{};
        std::string name{};
        bool enabled = false;
        MATH::Vec3 position{};
        float radius = 0.0f;
        float intensity = 1.0f;
        HIKARI::REFLECTION::RuntimeReflectionProbeInfluenceShape influenceShape =
            HIKARI::REFLECTION::RuntimeReflectionProbeInfluenceShape::Sphere;
        HIKARI::REFLECTION::RuntimeReflectionProbeProjectionShape projectionShape =
            HIKARI::REFLECTION::RuntimeReflectionProbeProjectionShape::Infinite;
        MATH::Vec3 influenceBoxCenter{};
        MATH::Vec3 influenceBoxSize{};
        MATH::Vec3 projectionBoxCenter{};
        MATH::Vec3 projectionBoxSize{};
        float blendDistance = 1.0f;
        int priority = 0;
        std::string sourceCubemapAssetId{};
        std::string bakeSet{ "Default" };
        std::string scenario{ "Default" };
    };

    struct IrradianceVolumeProvider {
        std::string id{};
        std::string name{};
        bool enabled = false;
        MATH::Vec3 origin{};
        MATH::Vec3 size{};
        uint32_t countX = 0;
        uint32_t countY = 0;
        uint32_t countZ = 0;
        float intensity = 1.0f;
        float blendDistance = 1.0f;
        float normalBias = 0.2f;
        int priority = 0;
        std::string bakeSet{ "Default" };
        std::string scenario{ "Default" };
    };

    struct SceneLightingSet {
        std::string activeScenario{ "Default" };
        std::vector<SceneLightProvider> lights{};
        std::vector<ReflectionProbeProvider> reflectionProbes{};
        std::vector<IrradianceVolumeProvider> irradianceVolumes{};
    };

} // namespace HIKARI::RENDER3D::LIGHTING
