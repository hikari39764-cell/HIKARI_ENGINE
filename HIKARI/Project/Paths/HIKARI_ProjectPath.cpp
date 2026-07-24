#include "Project/Paths/HIKARI_ProjectPath.h"

#include <system_error>

namespace HIKARI::PROJECT_PATHS {

    std::filesystem::path ResolveProjectPath(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& path) {

        if (path.empty()) {
            return {};
        }
        if (path.is_absolute()) {
            return path.lexically_normal();
        }
        return (projectRoot / path).lexically_normal();
    }

    std::filesystem::path MakeProjectRelativePath(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& path) {

        std::error_code ec{};
        const std::filesystem::path relative =
            std::filesystem::relative(path, projectRoot, ec);
        return ec || relative.empty()
            ? path.lexically_normal()
            : relative.lexically_normal();
    }

    std::string MakeProjectRelativeString(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& path) {

        return MakeProjectRelativePath(projectRoot, path).generic_string();
    }

} // namespace HIKARI::PROJECT_PATHS
