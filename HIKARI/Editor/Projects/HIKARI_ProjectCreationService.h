#pragma once

#include <filesystem>
#include <string>

namespace HIKARI::EDITOR {

    struct ProjectCreationRequest {
        std::filesystem::path parentDirectory{};
        std::string name{};
    };

    struct ProjectCreationResult {
        std::filesystem::path projectRoot{};
        std::filesystem::path descriptorPath{};
    };

    std::filesystem::path GetDefaultProjectParentDirectory();

    bool ValidateProjectCreation(const ProjectCreationRequest& request,
        std::filesystem::path& target, std::string& message);

    bool CreateProject(
        const ProjectCreationRequest& request,
        ProjectCreationResult& outResult,
        std::string& outMessage);

} // namespace HIKARI::EDITOR
