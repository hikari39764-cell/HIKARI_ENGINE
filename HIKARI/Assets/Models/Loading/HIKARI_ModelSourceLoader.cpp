#include "Assets/Models/Loading/HIKARI_ModelSourceLoader.h"

#include <string>
#include <utility>

#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Assets/Models/Loading/HIKARI_AssimpModelLoader.h"
#include "Assets/Models/Loading/HIKARI_GltfModelLoader.h"
#include "Assets/Models/Loading/HIKARI_ObjModelLoader.h"
#endif
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Core/Text/HIKARI_AsciiCase.h"
#endif
#include "Assets/Models/HIKARI_ModelBounds.h"

namespace HIKARI::ASSETS::MODELS {

namespace {

#if defined(HIKARI_WITH_EDITOR)
std::string GetLowerFileExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }

    std::string extension = path.substr(dot);
    TEXT::ToLowerAsciiInPlace(extension);
    return extension;
}
#endif

bool LoadCookedModel(ModelAsset& asset) {
    const std::string runtimePath = asset.GetSourcePath();
    const std::string runtimeName = asset.GetName();
    const uint64_t runtimeSourceRevision = asset.sourceRevision;

    ModelAsset cooked{};
    std::string message{};
    if (!ReadHmodelFile(runtimePath, cooked, message)) {
        return false;
    }

    cooked.SetName(runtimeName.empty() ? cooked.id.value : runtimeName);
    cooked.SetSourcePath(runtimePath);
    asset = std::move(cooked);
    // Loading a cooked payload replaces the object in place. Keep the
    // manager-owned runtime generation so resolved animation pointers
    // from the previous payload remain detectably stale.
    asset.sourceRevision = runtimeSourceRevision;
    return true;
}

} // namespace

bool LoadModelSource(ModelAsset& asset, std::vector<AssetImportInputFile>* sourceInputs) {
    if (sourceInputs)
        sourceInputs->clear();
    const std::string sourcePath = asset.GetSourcePath();
    bool loaded = false;

    if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(sourcePath) == CookedAssetFormat::HMODEL) {
        loaded = LoadCookedModel(asset);
    }
#if defined(HIKARI_WITH_EDITOR)
    else {
        const std::string extension = GetLowerFileExtension(sourcePath);
        if (extension == ".gltf") {
            loaded = LoadGltfModelSource(asset, sourceInputs);
        } else if (extension == ".obj") {
            loaded = LoadObjModelSource(asset, sourceInputs);
        } else if (extension == ".fbx") {
            loaded = LoadAssimpModelSource(asset);
        }
    }
#endif

    if (loaded) {
        BOUNDS::EnsureModelBounds(asset);
    }
    return loaded;
}

} // namespace HIKARI::ASSETS::MODELS
