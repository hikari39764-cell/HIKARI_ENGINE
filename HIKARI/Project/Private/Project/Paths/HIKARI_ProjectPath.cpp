#include "Project/Paths/HIKARI_ProjectPath.h"

#include <system_error>
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::PROJECT_PATHS {

std::filesystem::path ResolveRuntimePayloadFile(const std::filesystem::path& projectRoot, std::string_view text) {
    if (projectRoot.empty() || text.empty() || text.size() > 4096u || text.find('\0') != std::string_view::npos ||
        text.find(':') != std::string_view::npos)
        return {};
    const auto relative = TEXT::PathFromUtf8(text);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        return {};
    for (const auto& part : relative)
        if (part == "..")
            return {};
    const auto root = NormalizeProjectRoot(projectRoot);
    for (const auto& prefix : {std::filesystem::path{}, std::filesystem::path("RuntimePayload")}) {
        std::error_code error;
        const auto resolved = std::filesystem::weakly_canonical(root / prefix / relative, error);
        if (error)
            continue;
        const auto check = resolved.lexically_relative(root);
        if (check.empty() || *check.begin() == ".." || check.is_absolute())
            continue;
        if (std::filesystem::is_regular_file(resolved, error) && !error)
            return resolved;
    }
    return {};
}

std::filesystem::path NormalizeProjectRoot(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) {
        return {};
    }

    std::error_code ec{};
    std::filesystem::path normalized = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec || normalized.empty()) {
        ec.clear();
        normalized = std::filesystem::absolute(projectRoot, ec);
    }
    return ec || normalized.empty() ? projectRoot.lexically_normal() : normalized.lexically_normal();
}

std::filesystem::path ResolveProjectPath(const std::filesystem::path& projectRoot, const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (projectRoot / path).lexically_normal();
}

std::filesystem::path MakeProjectRelativePath(const std::filesystem::path& projectRoot,
                                              const std::filesystem::path& path) {
    std::error_code ec{};
    const std::filesystem::path relative = std::filesystem::relative(path, projectRoot, ec);
    return ec || relative.empty() ? path.lexically_normal() : relative.lexically_normal();
}

std::string MakeProjectRelativeString(const std::filesystem::path& projectRoot, const std::filesystem::path& path) {
    return MakeProjectRelativePath(projectRoot, path).generic_string();
}

} // namespace HIKARI::PROJECT_PATHS
