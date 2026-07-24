#include "Render3D/Models/HIKARI_ModelManager.h"

#include <utility>

#include "Assets/Models/Loading/HIKARI_ModelSourceLoader.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Render3D/Models/Primitives/HIKARI_BuiltinModelFactory.h"
#include "Render3D/Models/Runtime/HIKARI_ModelRuntimeResourceBuilder.h"

namespace HIKARI {

    namespace {

        void ClearModelSourceData(ModelAsset& asset) {
            asset.nodes.clear();
            asset.meshes.clear();
            asset.materials.clear();
            asset.textures.clear();
            asset.skins.clear();
            asset.animations.clear();
            asset.importDiagnostics = {};
            asset.defaultSceneRootNode = -1;
            asset.bounds = {};
        }

        bool RequiresLegacyRuntimeResources(const std::string& sourcePath) {
            return ASSETS::SEMANTICS::ClassifyCookedAssetFormat(sourcePath) !=
                CookedAssetFormat::HMODEL;
        }

    } // namespace

    ModelAsset* ModelManager::RegisterAsset(
        const std::string& name,
        const std::string& sourcePath) {

        if (ModelAsset* found = FindAsset(name)) {
            RENDER3D::MODELS::ReleaseModelRuntimeResources(*found);
            ClearModelSourceData(*found);
            found->SetSourcePath(sourcePath);
            found->SetState(ModelAsset::State::Unloaded);
            return found;
        }

        auto asset = std::make_unique<ModelAsset>();
        asset->SetName(name);
        asset->SetSourcePath(sourcePath);
        ModelAsset* result = asset.get();
        assets_.push_back(std::move(asset));
        nameToAsset_[name] = result;
        return result;
    }

    ModelAsset* ModelManager::FindAsset(const std::string& name) {
        const auto found = nameToAsset_.find(name);
        return found == nameToAsset_.end() ? nullptr : found->second;
    }

    const ModelAsset* ModelManager::FindAsset(const std::string& name) const {
        const auto found = nameToAsset_.find(name);
        return found == nameToAsset_.end() ? nullptr : found->second;
    }

    const std::vector<std::unique_ptr<ModelAsset>>&
        ModelManager::GetAssets() const {
        return assets_;
    }

    bool ModelManager::MarkLoaded(const std::string& name) {
        ModelAsset* asset = FindAsset(name);
        if (asset == nullptr) {
            return false;
        }
        asset->SetState(ModelAsset::State::Loaded);
        return true;
    }

    bool ModelManager::MarkFailed(const std::string& name) {
        ModelAsset* asset = FindAsset(name);
        if (asset == nullptr) {
            return false;
        }
        asset->SetState(ModelAsset::State::Failed);
        return true;
    }

    bool ModelManager::LoadAssetNow(const std::string& name) {
        ModelAsset* asset = FindAsset(name);
        if (asset == nullptr) {
            return false;
        }

        RENDER3D::MODELS::ReleaseModelRuntimeResources(*asset);
        ClearModelSourceData(*asset);

        bool loaded = false;
        if (asset->GetSourcePath() == "builtin:cube") {
            loaded = RENDER3D::MODELS::BuildBuiltinCubeModel(*asset);
        } else {
            loaded = ASSETS::MODELS::LoadModelSource(*asset);
            if (loaded) {
                textureResolver_.ResolveAssetTexturePaths(*asset);
            }
            if (loaded &&
                RequiresLegacyRuntimeResources(asset->GetSourcePath())) {
                loaded = RENDER3D::MODELS::BuildModelRuntimeResources(
                    *asset,
                    textureResolver_);
            }
        }

        asset->SetState(
            loaded
                ? ModelAsset::State::Loaded
                : ModelAsset::State::Failed);
        return loaded;
    }

    bool ModelManager::ReloadAssetNow(const std::string& name) {
        if (FindAsset(name) == nullptr) {
            return false;
        }
        UnloadAsset(name);
        return LoadAssetNow(name);
    }

    void ModelManager::UnloadAsset(const std::string& name) {
        ModelAsset* asset = FindAsset(name);
        if (asset == nullptr) {
            return;
        }

        RENDER3D::MODELS::ReleaseModelRuntimeResources(*asset);
        ClearModelSourceData(*asset);
        asset->SetState(ModelAsset::State::Unloaded);
    }

    void ModelManager::UnloadAllAssets() {
        for (const std::unique_ptr<ModelAsset>& asset : assets_) {
            if (asset != nullptr) {
                UnloadAsset(asset->GetName());
            }
        }
    }

    bool ModelManager::LoadAllRegisteredAssets() {
        bool allLoaded = true;
        for (const std::unique_ptr<ModelAsset>& asset : assets_) {
            allLoaded = LoadAssetNow(asset->GetName()) && allLoaded;
        }
        return allLoaded;
    }

    size_t ModelManager::CountLoadedAssets() const {
        size_t count = 0;
        for (const std::unique_ptr<ModelAsset>& asset : assets_) {
            if (asset->GetState() == ModelAsset::State::Loaded) {
                ++count;
            }
        }
        return count;
    }

    size_t ModelManager::CountFailedAssets() const {
        size_t count = 0;
        for (const std::unique_ptr<ModelAsset>& asset : assets_) {
            if (asset->GetState() == ModelAsset::State::Failed) {
                ++count;
            }
        }
        return count;
    }

    void ModelManager::SetTexturePathResolver(
        ModelTexturePathResolver resolver) {
        textureResolver_.SetPathResolver(std::move(resolver));
    }

    void ModelManager::ClearTexturePathResolver() {
        textureResolver_.ClearPathResolver();
    }

    void ModelManager::ResetTextureResolveStats() {
        textureResolver_.ResetStats();
    }

    void ModelManager::RecordTextureResolveFailure(
        ModelTextureResolveFailureKind kind) const {
        textureResolver_.RecordFailure(kind);
    }

    const ModelTextureResolveStats&
        ModelManager::GetTextureResolveStats() const {
        return textureResolver_.GetStats();
    }

} // namespace HIKARI
