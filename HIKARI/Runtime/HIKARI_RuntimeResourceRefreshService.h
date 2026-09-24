#pragma once

#include <string>
#include <vector>

#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI {

class DocumentSceneBase;

struct RuntimeResourceRefreshReport {
    bool assetRegistryPublished = false;
    int textureInvalidatedCount = 0;
    int skyInvalidatedCount = 0;
    int modelReloadedCount = 0;
    int modelReboundComponentCount = 0;
    int materialReloadedCount = 0;
    int materialReboundComponentCount = 0;
    int failedCount = 0;
    std::vector<std::string> messages{};
};

class RuntimeResourceRefreshService {
public:
    RuntimeResourceRefreshReport RefreshAssets(DocumentSceneBase& scene, const std::vector<AssetId>& assetIds,
                                               bool assetRegistryAlreadyPublished = false);

    RuntimeResourceRefreshReport RefreshCurrentSceneResources(DocumentSceneBase& scene,
                                                              bool assetRegistryAlreadyPublished = false);

private:
    bool EnsureAssetRegistryPublished(DocumentSceneBase& scene, bool alreadyPublished,
                                      RuntimeResourceRefreshReport& report);

    bool RefreshTextureAsset(DocumentSceneBase& scene, const TextureAssetDescriptor& descriptor,
                             RuntimeResourceRefreshReport& report);

    bool RefreshSkyAsset(DocumentSceneBase& scene, const SkyAssetDescriptor& descriptor,
                         RuntimeResourceRefreshReport& report);

    bool RefreshModelAsset(DocumentSceneBase& scene, const ModelAssetDescriptor& descriptor,
                           RuntimeResourceRefreshReport& report, bool rebindAfterReload);

    bool RefreshMaterialAsset(DocumentSceneBase& scene, const MaterialAssetDescriptor& descriptor,
                              RuntimeResourceRefreshReport& report);

    void ReloadCurrentSceneModelDependencies(DocumentSceneBase& scene, RuntimeResourceRefreshReport& report);

    void AppendMessage(RuntimeResourceRefreshReport& report, std::string message) const;
};

} // namespace HIKARI
