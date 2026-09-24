#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace HIKARI {

class AssetDatabase;
struct SceneDocument;

namespace ASSETS::LIGHTING {

struct LightingBakeHashSnapshot {
    std::string sceneHash{};
    std::string dependencyHash{};
    std::unordered_map<std::string, std::string> recordAuthoringHashes{};
    std::vector<std::string> missingAssetIds{};
};

std::string BuildLightingSceneHash(const SceneDocument& document);

std::unordered_map<std::string, std::string> BuildLightingBakeRecordAuthoringHashes(const SceneDocument& document);

LightingBakeHashSnapshot BuildLightingBakeHashSnapshot(const SceneDocument& document,
                                                       const AssetDatabase& assetDatabase);

} // namespace ASSETS::LIGHTING
} // namespace HIKARI
