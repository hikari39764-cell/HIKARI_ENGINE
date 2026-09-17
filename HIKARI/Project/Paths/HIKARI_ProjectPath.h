#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace HIKARI::PROJECT_PATHS {

    // Source project and cooked RuntimePayload use the same relative identity.
    std::filesystem::path ResolveRuntimePayloadFile(const std::filesystem::path& projectRoot,
        std::string_view projectRelativePath);

    std::filesystem::path NormalizeProjectRoot(
        const std::filesystem::path& projectRoot);

    std::filesystem::path ResolveProjectPath(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& path);

    std::filesystem::path MakeProjectRelativePath(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& path);

    std::string MakeProjectRelativeString(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& path);

} // namespace HIKARI::PROJECT_PATHS
