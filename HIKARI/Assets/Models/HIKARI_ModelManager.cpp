#include "Assets/Models/HIKARI_ModelManager.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <utility>

#include "Assets/Models/Loading/HIKARI_ModelSourceLoader.h"

namespace HIKARI {

namespace {

void ClearModelSourceData(ModelAsset& asset) {
    NUMERIC::AdvanceNonZero(asset.sourceRevision);
    asset.nodes.clear();
    asset.meshes.clear();
    asset.materials.clear();
    asset.materialImportSources.clear();
    asset.textures.clear();
    asset.skins.clear();
    asset.animations.clear();
    asset.importDiagnostics = {};
    asset.defaultSceneRootNode = -1;
    asset.bounds = {};
}

} // namespace

ModelAsset* ModelManager::RegisterAsset(const std::string& name, const std::string& sourcePath) {
    if (ModelAsset* found = FindAsset(name)) {
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

const std::vector<std::unique_ptr<ModelAsset>>& ModelManager::GetAssets() const { return assets_; }

bool ModelManager::LoadAssetNow(const std::string& name) {
    ModelAsset* asset = FindAsset(name);
    if (asset == nullptr) {
        return false;
    }

    ClearModelSourceData(*asset);

    const bool loaded = ASSETS::MODELS::LoadModelSource(*asset);
    if (loaded) {
        textureResolver_.ResolveAssetTexturePaths(*asset);
    }

    asset->SetState(loaded ? ModelAsset::State::Loaded : ModelAsset::State::Failed);
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

    ClearModelSourceData(*asset);
    asset->SetState(ModelAsset::State::Unloaded);
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

void ModelManager::SetTexturePathResolver(ModelTexturePathResolver resolver) {
    textureResolver_.SetPathResolver(std::move(resolver));
}

void ModelManager::ResetTextureResolveStats() { textureResolver_.ResetStats(); }

void ModelManager::RecordTextureResolveFailure(ModelTextureResolveFailureKind kind) const {
    textureResolver_.RecordFailure(kind);
}

} // namespace HIKARI
