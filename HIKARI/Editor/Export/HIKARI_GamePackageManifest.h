#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::EDITOR {

    struct GamePackageManifestOptions {
        std::string startupSceneGuid{};
        std::vector<std::string> exportedSceneGuids{};
        std::string defaultWindowMode{};
        std::string executableFileName{};
    };

    bool IsForbiddenGamePackagePath(const std::filesystem::path& relativePath);

    bool ValidateGamePackageBoundary(
        const std::filesystem::path& packageRoot,
        const GamePackageManifestOptions& options,
        std::string& outMessage);

    bool WriteGamePackageManifest(
        const std::filesystem::path& packageRoot,
        const GamePackageManifestOptions& options,
        std::string& outMessage);

} // namespace HIKARI::EDITOR
