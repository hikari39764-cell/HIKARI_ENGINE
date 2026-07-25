#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace HIKARI {

    struct ProjectLaunchOptions {
        std::optional<std::filesystem::path> explicitProjectPath{};
        std::optional<std::filesystem::path> runtimeConfigProjectRoot{};
    };

    bool OpenProjectForLaunch(
        const ProjectLaunchOptions& options,
        std::string* outMessage = nullptr);

    bool ApplyProjectWorkingDirectory(
        std::string* outMessage = nullptr);

} // namespace HIKARI
