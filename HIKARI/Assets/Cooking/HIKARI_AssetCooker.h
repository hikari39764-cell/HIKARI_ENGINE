#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::ASSETS::COOKING {

    struct AssetCookOptions {
        std::filesystem::path projectRoot{};
        std::filesystem::path contentRoot{};
        std::string startupSceneGuid{};
        std::vector<std::string> sceneGuids{};
    };

    struct AssetCookResult {
        bool success = false;
        std::string message{};
        size_t assetCount = 0u;
        size_t fileCount = 0u;
        size_t missingReferenceCount = 0u;
    };

    class AssetCooker {
    public:
        AssetCookResult Cook(
            const AssetCookOptions& options) const;
    };

} // namespace HIKARI::ASSETS::COOKING
