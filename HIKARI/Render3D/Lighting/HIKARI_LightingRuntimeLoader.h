#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"

namespace HIKARI {

class AssetRegistry;
class SkyManager;

namespace RENDER3D::LIGHTING {

struct LightingRuntimeLoadRequest {
    std::string sceneGuid{};
    std::filesystem::path projectRoot{};
    std::unordered_set<std::string> skyAssetIds{};
    SkySettings sky{};
    std::string expectedSceneHash{};
    std::unordered_map<std::string, std::string> expectedBakeAuthoringHashes{};
    SceneLightingSet lighting{};
    bool allowAuthoringReflectionProbeFallback = true;
    bool preferBakeManifest = true;
};

struct LightingRuntimeLoadResult {
    bool success = true;
    SceneLightingRuntimeData runtimeData{};
    std::vector<std::string> messages{};
};

class LightingRuntimeLoader {
public:
    LightingRuntimeLoadResult Load(const LightingRuntimeLoadRequest& request, const AssetRegistry& assetRegistry,
                                   SkyManager& skyManager) const;

private:
    bool TryLoadBakeManifest(const LightingRuntimeLoadRequest& request, SceneLightingRuntimeData& runtimeData,
                             std::vector<std::string>& messages) const;

    void LoadSkyLightingFromAssets(const LightingRuntimeLoadRequest& request, const AssetRegistry& assetRegistry,
                                   SkyManager& skyManager, SceneLightingRuntimeData& runtimeData,
                                   std::vector<std::string>& messages) const;

    void LoadReflectionProbesFromAuthoringSources(const LightingRuntimeLoadRequest& request,
                                                  const AssetRegistry& assetRegistry,
                                                  SceneLightingRuntimeData& runtimeData,
                                                  std::vector<std::string>& messages) const;
};

} // namespace RENDER3D::LIGHTING

} // namespace HIKARI
