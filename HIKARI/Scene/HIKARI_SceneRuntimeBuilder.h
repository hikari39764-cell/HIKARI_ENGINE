#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {

    class ComponentRegistry;

    class AssetRegistry;
    class ComponentRegistry;
    class ModelManager;
    class SkyManager;
    class World;
    struct SceneDocument;

    struct SceneDependencySet {
        std::unordered_set<std::string> modelAssetIds{};
        std::unordered_set<std::string> skyAssetIds{};
        std::unordered_set<std::string> reflectionProbeCubemapAssetIds{};
        std::unordered_set<std::string> textureAssetIds{};
        std::unordered_set<std::string> materialAssetIds{};
        bool reflectionProbeEnabled = false;
        MATH::Vec3 reflectionProbePosition{ 0.0f, 2.0f, 0.0f };
        float reflectionProbeRadius = 8.0f;
        float reflectionProbeIntensity = 1.0f;
        ReflectionProbeInfluenceShape reflectionProbeInfluenceShape = ReflectionProbeInfluenceShape::Sphere;
        ReflectionProbeProjectionShape reflectionProbeProjectionShape = ReflectionProbeProjectionShape::Infinite;
        MATH::Vec3 reflectionProbeInfluenceBoxCenter{ 0.0f, 2.0f, 0.0f };
        MATH::Vec3 reflectionProbeInfluenceBoxSize{ 8.0f, 4.0f, 8.0f };
        MATH::Vec3 reflectionProbeProjectionBoxCenter{ 0.0f, 2.0f, 0.0f };
        MATH::Vec3 reflectionProbeProjectionBoxSize{ 8.0f, 4.0f, 8.0f };
        float reflectionProbeBlendDistance = 1.0f;
        int reflectionProbePriority = 0;
        bool lightProbeVolumeEnabled = false;
        float lightProbeVolumeIntensity = 1.0f;
    };

    class SceneRuntimeBuilder {
    public:
        SceneDependencySet CollectDependencies(
            const SceneDocument& document,
            const ComponentRegistry* enabledComponents = nullptr) const;
        bool PreloadDependencies(
            const SceneDependencySet& dependencies,
            const AssetRegistry& assetRegistry,
            ModelManager& modelManager,
            SkyManager& skyManager,
            const std::filesystem::path& projectRoot,
            const std::string& sceneGuid) const;

        bool BuildWorldFromDocument(
            const SceneDocument& document,
            World& world,
            const AssetRegistry& assetRegistry,
            const ComponentRegistry& componentRegistry,
            ModelManager& modelManager,
            SkyManager& skyManager) const;
    };

} // namespace HIKARI
