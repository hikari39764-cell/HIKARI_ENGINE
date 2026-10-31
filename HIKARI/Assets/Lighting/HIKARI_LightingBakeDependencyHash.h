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
    // 中身を読めず依存を辿れなかった資産（GUID と理由）。ある間、焼き込みの状態は常に「要更新」として扱う。
    std::vector<std::string> unreadableAssets{};
};

std::string BuildLightingSceneHash(const SceneDocument& document);

std::unordered_map<std::string, std::string> BuildLightingBakeRecordAuthoringHashes(const SceneDocument& document);

LightingBakeHashSnapshot BuildLightingBakeHashSnapshot(const SceneDocument& document,
                                                       const AssetDatabase& assetDatabase);

} // namespace ASSETS::LIGHTING
} // namespace HIKARI
