#include "Core/IO/HIKARI_PathNormalization.h"

#include <system_error>
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::PATHS {

std::filesystem::path MakeAbsoluteNormalized(const std::filesystem::path& path) {
    std::error_code ec{};
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return ec ? path.lexically_normal() : absolute.lexically_normal();
}

bool IsCanonicalDescendant(const std::filesystem::path& root, const std::filesystem::path& file) {
    // UTF-8 で比べる（generic_string は ANSI 変換で、コードページ外の文字を含むパスで失敗する）。
    std::string rootKey = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(root));
    const std::string fileKey = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(file));
    while (!rootKey.empty() && rootKey.back() == '/')
        rootKey.pop_back();
    return !rootKey.empty() && fileKey.size() > rootKey.size() && fileKey.compare(0u, rootKey.size(), rootKey) == 0 &&
           fileKey[rootKey.size()] == '/';
}

bool IsContainedPath(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code error{};
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return false;
    }
    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, error);
    return !error && IsCanonicalDescendant(canonicalRoot, canonicalPath);
}

bool HasParentTraversal(const std::filesystem::path& path) noexcept {
    for (const std::filesystem::path& part : path) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

bool IsLexicallyWithin(const std::filesystem::path& root, const std::filesystem::path& candidate) noexcept {
    auto rootIt = root.begin();
    auto candidateIt = candidate.begin();
    while (rootIt != root.end()) {
        if (candidateIt == candidate.end() || *rootIt != *candidateIt) {
            return false;
        }
        ++rootIt;
        ++candidateIt;
    }
    return true;
}

} // namespace HIKARI::PATHS
