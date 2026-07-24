#pragma once

#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Sequence/HIKARI_SequenceAssetStore.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"

namespace HIKARI {

    class DocumentSceneBase;
    struct PbrMaterialAssetData;
    struct SceneDependencySet;

    class DocumentSceneAssetBindings final {
    public:
        AssetDatabase& Database() noexcept { return database_; }
        const AssetDatabase& Database() const noexcept { return database_; }
        AssetRegistry& Registry() noexcept { return registry_; }
        const AssetRegistry& Registry() const noexcept { return registry_; }
        SequenceAssetStore& SequenceAssets() noexcept {
            return sequenceAssets_;
        }
        ModelManager& Models() noexcept { return models_; }

        bool ReloadAssets();
        void ConfigureModelTextureResolver();
        const AssetRecord* FindUniqueTextureAssetByFilename(
            const std::string& filename,
            const std::string& sourceTexturePath) const;
        std::string ResolveModelTexturePath(
            const std::string& sourceTexturePath,
            MaterialTextureUsage usage) const;
        bool NeedsRuntimeDependencyRegistryRefresh(
            const SceneDependencySet& dependencies) const;
        bool RefreshTextureByPath(const std::string& path);
        bool ReloadModelAsset(const AssetId& modelId);
        int RebindModelComponents(DocumentSceneBase& scene);
        int RebuildMaterialOverrides(DocumentSceneBase& scene);
        int RebuildMaterialOverridesForMaterial(
            DocumentSceneBase& scene,
            const AssetGuid& materialGuid);
        int ApplyMaterialOverridePreview(
            DocumentSceneBase& scene,
            const AssetGuid& materialGuid,
            const PbrMaterialAssetData& data);

    private:
        AssetDatabase database_{};
        AssetRegistry registry_{};
        SequenceAssetStore sequenceAssets_{};
        ModelManager models_{};
    };

} // namespace HIKARI
