#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Assets/HIKARI_DocumentAssetStores.h"
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Assets/Environment/HIKARI_EnvironmentAssetStore.h"
#include "Assets/Post/HIKARI_PostAssetStore.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAssetStore.h"
#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Assets/Sprite/HIKARI_SpriteAssetStore.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialStore.h"
#include "Assets/Models/HIKARI_ModelManager.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Scene/Lighting/HIKARI_LocalTextureResolver.h"

namespace HIKARI {

class DocumentSceneBase;
class ComponentRegistry;
class PrefabAssetStore;
class SceneRuntimeBuilder;
class SkyManager;
struct MaterialAssetData;
struct PbrMaterialAssetData;
struct SceneDependencySet;

class DocumentSceneAssetBindings final {
public:
    AssetDatabase& Database() noexcept { return database_; }
    const AssetDatabase& Database() const noexcept { return database_; }
    AssetRegistry& Registry() noexcept { return registry_; }
    const AssetRegistry& Registry() const noexcept { return registry_; }
    SequenceAssetStore& SequenceAssets() noexcept { return sequenceAssets_; }
    GameplayCameraAssetStore& GameplayCameras() noexcept { return gameplayCameras_; }
    POST::PostAssetStore& PostAssets() noexcept { return postAssets_; }
    ENVIRONMENT::EnvironmentAssetStore& EnvironmentAssets() noexcept { return environmentAssets_; }
    RENDERSTYLE::RenderStyleAssetStore& RenderStyleAssets() noexcept { return renderStyleAssets_; }
    TextAssetStore& TextAssets() noexcept { return textAssets_; }
    SPRITE::SpriteAssetStore& SpriteAssets() noexcept { return spriteAssets_; }
    SPRITE::SpriteMaterialStore& SpriteMaterials() noexcept { return spriteMaterials_; }
    ModelManager& Models() noexcept { return models_; }
    LocalTextureResolver& LocalTextures() noexcept { return localTextures_; }

    bool ReloadAssets(const SceneDependencySet* required = nullptr, const AssetGuid* admissionRoot = nullptr,
                      const std::function<bool(const AssetRegistry&, std::string&)>& validatePrepared = {},
                      std::string* outError = nullptr);
    bool PrepareRuntimePrefabAssets(const AssetGuid& prefabGuid, const PrefabAssetStore& prefabAssets,
                                    const SceneRuntimeBuilder& builder, const ComponentRegistry& componentRegistry,
                                    SkyManager& skyManager, std::string& outError);
    bool NeedsRuntimeProgramImport(const SceneDependencySet& required) const;
    void ConfigureModelTextureResolver();
    const AssetRecord* FindUniqueTextureAssetByFilename(const std::string& filename,
                                                        const std::string& sourceTexturePath) const;
    std::string ResolveModelTexturePath(const std::string& sourceTexturePath, MaterialTextureUsage usage) const;
    bool NeedsRuntimeDependencyRegistryRefresh(const SceneDependencySet& dependencies,
                                               std::string* outReason = nullptr) const;
    bool RefreshTextureByPath(const std::string& path);
    bool ReloadModelAsset(const AssetGuid& modelId);
    int RebindModelComponents(DocumentSceneBase& scene);
    int RebuildMaterialOverrides(DocumentSceneBase& scene);
    int RebuildMaterialOverridesForMaterial(DocumentSceneBase& scene, const AssetGuid& materialGuid);
    int ApplyMaterialOverridePreview(DocumentSceneBase& scene, const AssetGuid& materialGuid,
                                     const MaterialAssetData& data);
    int ApplyMaterialOverridePreview(DocumentSceneBase& scene, const AssetGuid& materialGuid,
                                     const PbrMaterialAssetData& data);

private:
    AssetDatabase database_{};
    AssetRegistry registry_{};
    uint64_t registrySourceRevision_ = 0u;
    // この実行中に自動で取り込みへ回した資産。取り込みに失敗しても同じ実行中は繰り返さない。
    std::unordered_set<std::string> autoImportQueued_{};
    SequenceAssetStore sequenceAssets_{};
    GameplayCameraAssetStore gameplayCameras_;
    POST::PostAssetStore postAssets_;
    ENVIRONMENT::EnvironmentAssetStore environmentAssets_;
    RENDERSTYLE::RenderStyleAssetStore renderStyleAssets_;
    TextAssetStore textAssets_;
    SPRITE::SpriteAssetStore spriteAssets_{};
    SPRITE::SpriteMaterialStore spriteMaterials_{};
    ModelManager models_{};
    LocalTextureResolver localTextures_{};
};

} // namespace HIKARI
