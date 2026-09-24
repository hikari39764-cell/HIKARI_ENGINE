#include "Core/IO/HIKARI_PathNormalization.h"

#include <system_error>
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::PATHS {

std::filesystem::path MakeAbsoluteNormalized(const std::filesystem::path& path) {
    std::error_code ec{};
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return ec ? path.lexically_normal() : absolute.lexically_normal();
}

bool IsCanonicalDescendant(const std::filesystem::path& root, const std::filesystem::path& file) {
    std::string rootKey = TEXT::ToLowerAsciiCopy(root.generic_string());
    const std::string fileKey = TEXT::ToLowerAsciiCopy(file.generic_string());
    while (!rootKey.empty() && rootKey.back() == '/')
        rootKey.pop_back();
    return !rootKey.empty() && fileKey.size() > rootKey.size() && fileKey.compare(0u, rootKey.size(), rootKey) == 0 &&
           fileKey[rootKey.size()] == '/';
}
} // namespace HIKARI::PATHS
