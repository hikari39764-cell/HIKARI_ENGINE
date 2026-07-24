#pragma once

#include <filesystem>
#include <string>

namespace HIKARI::PROJECT_PATHS {

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
