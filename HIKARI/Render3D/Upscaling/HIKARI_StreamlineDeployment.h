#pragma once

#include <filesystem>
#include <string>

namespace HIKARI::RENDER3D::UPSCALING {

    struct StreamlineDeploymentOptions {
        bool requireDlss = false;
        bool requireFrameGeneration = false;
    };

    bool DeployStreamlineRuntime(
        const std::filesystem::path& sourceDirectory,
        const std::filesystem::path& outputDirectory,
        const StreamlineDeploymentOptions& options,
        std::string& errorMessage);

} // namespace HIKARI::RENDER3D::UPSCALING
