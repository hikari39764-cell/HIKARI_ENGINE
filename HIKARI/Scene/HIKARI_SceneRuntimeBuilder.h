#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
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
        std::unordered_set<std::string> textureAssetIds{};
        std::unordered_set<std::string> materialAssetIds{};
        std::unordered_set<std::string> modelMaterialSetAssetIds{};
        std::unordered_set<std::string> vfxAssetIds{};
        std::string lightingSceneHash{};
        std::unordered_map<std::string, std::string>
            lightingBakeAuthoringHashes{};
        RENDER3D::LIGHTING::SceneLightingSet lighting{};
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
