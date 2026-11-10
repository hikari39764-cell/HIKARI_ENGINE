#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace HIKARI::ASSETS::COOKING {

struct AssetCookOptions {
    std::filesystem::path projectRoot{};
    std::filesystem::path contentRoot{};
    std::string startupSceneGuid{};
    std::vector<std::string> sceneGuids{};
    // Project-declared roots close over assets referenced from compiled
    // GameProject code (UI, VFX, model presets, dialogue) that cannot be
    // discovered by scanning scene JSON alone.
    std::vector<std::filesystem::path> assetRoots{};
    // Direct-file consumers such as audio and font/UI presentation use a
    // quarantined RuntimePayload tree instead of shipping authored Assets.
    std::vector<std::filesystem::path> runtimePayloadRoots{};
    std::function<bool(const std::string&, float)> progress{};
};

struct AssetCookResult {
    bool success = false;
    std::string message{};
    size_t assetCount = 0u;
    size_t fileCount = 0u;
    size_t missingReferenceCount = 0u;
    // 焼いた場面の元の路径（項目の根からの相対）。
    std::vector<std::filesystem::path> scenePaths{};
};

// 元のファイルをそのまま配る時の、焼いた内容の中の位置（RuntimePayload/<項目の根からの相対>）。
std::filesystem::path MakeRuntimePayloadPath(const std::filesystem::path& sourcePath);

class AssetCooker {
public:
    AssetCookResult Cook(const AssetCookOptions& options) const;
};

} // namespace HIKARI::ASSETS::COOKING
