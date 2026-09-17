#pragma once
#include <filesystem>
#include <json.hpp>

namespace HIKARI::EDITOR::PROJECT_CODE {
    // These helpers own project-code file admission, snapshots and publication.
    // No Scene, World, compiler or AI model is retained here.
    bool IsCodePath(const std::filesystem::path& relative);
    std::filesystem::path ResolveCodeFile(const std::filesystem::path& root, const std::string& relative);
    nlohmann::json Snapshot(const std::filesystem::path& root);
    nlohmann::json ReadFile(const std::filesystem::path& root, const std::string& relative);
    nlohmann::json ReadSdkHeader(const std::filesystem::path& sdk, const std::string& relative);
    nlohmann::json Prepare(const std::filesystem::path& project, const std::filesystem::path& sdk,
        const std::filesystem::path& job, const nlohmann::json& edits);
    void Apply(const std::filesystem::path& project, const std::filesystem::path& job,
        const nlohmann::json& prepared);
}
