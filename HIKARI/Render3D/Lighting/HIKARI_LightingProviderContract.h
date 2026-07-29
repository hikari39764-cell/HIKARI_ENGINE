#pragma once

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

    struct ReflectionProbeProvider {
        std::string id{};
        std::string name{};
        bool enabled = false;
        MATH::Vec3 position{};
        float radius = 0.0f;
        float intensity = 1.0f;
        REFLECTION::RuntimeReflectionProbeInfluenceShape influenceShape =
            REFLECTION::RuntimeReflectionProbeInfluenceShape::Sphere;
        REFLECTION::RuntimeReflectionProbeProjectionShape projectionShape =
            REFLECTION::RuntimeReflectionProbeProjectionShape::Infinite;
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
        int priority = 0;
        std::string bakeSet{ "Default" };
        std::string scenario{ "Default" };
    };

    struct SceneLightingSet {
        std::string activeScenario{ "Default" };
        std::vector<ReflectionProbeProvider> reflectionProbes{};
        std::vector<IrradianceVolumeProvider> irradianceVolumes{};
    };

} // namespace HIKARI::RENDER3D::LIGHTING
