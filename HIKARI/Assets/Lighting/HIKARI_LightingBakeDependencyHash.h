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
            std::unordered_map<uint64_t, std::string> objectAuthoringHashes{};
            std::vector<std::string> missingAssetIds{};
        };

        LightingBakeHashSnapshot BuildLightingBakeHashSnapshot(
            const SceneDocument& document,
            const AssetDatabase& assetDatabase);

    } // namespace ASSETS::LIGHTING
} // namespace HIKARI
