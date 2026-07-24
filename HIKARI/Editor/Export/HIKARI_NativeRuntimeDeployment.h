#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace HIKARI::EDITOR {

    struct NativeRuntimeDeploymentResult {
        bool success = false;
        uint32_t filesCopied = 0;
        std::string message{};
    };

    NativeRuntimeDeploymentResult DeployNativeRuntimeDependencies(
        const std::filesystem::path& sourceDirectory,
        const std::filesystem::path& outputDirectory);

} // namespace HIKARI::EDITOR
