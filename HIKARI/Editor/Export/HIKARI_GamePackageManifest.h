#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::EDITOR {

    struct GamePackageManifestOptions {
        std::string startupSceneGuid{};
        std::vector<std::string> exportedSceneGuids{};
    };

    bool ValidateGamePackageBoundary(
        const std::filesystem::path& packageRoot,
        std::string& outMessage);

    bool WriteGamePackageManifest(
        const std::filesystem::path& packageRoot,
        const GamePackageManifestOptions& options,
        std::string& outMessage);

} // namespace HIKARI::EDITOR
