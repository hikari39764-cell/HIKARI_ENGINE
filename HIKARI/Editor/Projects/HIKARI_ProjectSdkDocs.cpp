#include "Editor/Projects/HIKARI_ProjectSdkDocs.h"

#include "Core/Text/HIKARI_Utf8.h"
#include "Core/Text/HIKARI_UtfPath.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace HIKARI::EDITOR::PROJECT_SDK {
namespace {

constexpr uintmax_t kMaximumSdkFileBytes = 1024u * 1024u;
constexpr size_t kMaximumSdkHeaderCount = 2048u;
constexpr size_t kMaximumIndexPageSize = 64u;
constexpr size_t kMaximumReadLines = 400u;
constexpr size_t kDefaultReadLines = 160u;
constexpr size_t kMaximumResponseBytes = 32u * 1024u;

struct GuideEntry {
    const char* path;
    const char* title;
    const char* purpose;
    const char* startsWith;
};

constexpr GuideEntry kGuideEntries[] = {
    {"game-project/GameDevelopmentResponsibilities.md", "Game development responsibilities",
     "Which decisions belong in project code, Editor authoring, and runtime instance ownership.",
     "Read this first when deciding where gameplay behavior or authored data belongs."},
    {"game-project/MakingAGame.md", "Making a game",
     "Project setup, asset authoring, runtime flow, and packaging workflow.",
     "Start with the project template and follow the first playable scene path."},
    {"game-project/GameProjectSdkApiGuide.md", "Game Project SDK API guide",
     "Public C++ SDK types, registration, WorldView access, and runtime callbacks.",
     "Start with GameComponent, GameSystem, RuntimeRegistration, and WorldView."},
    {"game-project/ComponentsAndSystemsTutorial.md", "Components and systems tutorial",
     "A focused walkthrough for project-owned components and systems.",
     "Start with registration and the component/system lifecycle examples."},
    {"ui-text-workspaces.md", "UI text workspaces", "Public UI text, style, and workspace authoring surfaces.",
     "Start with the workspace and text-style sections when building menus or HUDs."},
    {"ai/hikari-ai-capabilities.md", "AI project access modes",
     "Assistant and Local Bridge access levels, approvals, and installed SDK material access.",
     "Choose Content, Read project code, or Develop project explicitly in the assistant."},
};

std::filesystem::path Canonical(const std::filesystem::path& path) {
    std::error_code error;
    const auto result = std::filesystem::canonical(path, error);
    if (error) {
        throw std::runtime_error("Installed SDK path is unavailable: " + TEXT::PathToUtf8(path));
    }
    return result;
}

bool IsBelow(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

bool IsReparsePoint(const std::filesystem::path& path) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u;
#else
    std::error_code error;
    return std::filesystem::is_symlink(path, error) && !error;
#endif
}

std::string Fold(std::string_view value) {
    std::string folded(value);
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c); });
    return folded;
}

std::filesystem::path ResolveInstalledFile(const std::filesystem::path& sdkRoot, std::string_view relativePath) {
    if (relativePath.empty() || relativePath.size() > 512u || relativePath.find('\0') != std::string_view::npos ||
        !TEXT::IsValidUtf8(relativePath)) {
        throw std::runtime_error("Choose a bounded path inside the installed public SDK.");
    }
    const auto relative = TEXT::PathFromUtf8(relativePath);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        throw std::runtime_error("SDK paths must be relative to docs/ or include/HIKARI/.");
    }

    std::filesystem::path base;
    std::filesystem::path child;
    constexpr std::string_view docsPrefix = "docs/";
    constexpr std::string_view includePrefix = "include/HIKARI/";
    if (relativePath.starts_with(docsPrefix)) {
        base = sdkRoot / "docs";
        child = TEXT::PathFromUtf8(relativePath.substr(docsPrefix.size()));
        if (child.extension() != ".md") {
            throw std::runtime_error("Only installed Markdown guides can be read from docs/.");
        }
    } else if (relativePath.starts_with(includePrefix)) {
        base = sdkRoot / "include/HIKARI";
        child = TEXT::PathFromUtf8(relativePath.substr(includePrefix.size()));
        if (child.extension() != ".h") {
            throw std::runtime_error("Only public .h headers can be read from include/HIKARI/.");
        }
    } else {
        throw std::runtime_error("Choose a path under docs/ or include/HIKARI/.");
    }

    if (child.empty() || child.is_absolute() || child.has_root_name() || child.has_root_directory()) {
        throw std::runtime_error("Invalid installed SDK path.");
    }
    for (const auto& part : child) {
        const std::string name = TEXT::PathToUtf8(part);
        if (name.empty() || name == "." || name == ".." || name.find(':') != std::string::npos ||
            name.find_first_of("<>\"|?*") != std::string::npos) {
            throw std::runtime_error("Invalid installed SDK path.");
        }
    }

    const auto canonicalSdk = Canonical(sdkRoot);
    const auto canonicalBase = Canonical(base);
    if (!IsBelow(canonicalBase, canonicalSdk)) {
        throw std::runtime_error("Installed SDK directory escapes its package root.");
    }
    const auto resolved = Canonical(canonicalBase / child);
    if (!IsBelow(resolved, canonicalBase) || !std::filesystem::is_regular_file(resolved)) {
        throw std::runtime_error("The requested public SDK file is not installed.");
    }
    const auto size = std::filesystem::file_size(resolved);
    if (size > kMaximumSdkFileBytes) {
        throw std::runtime_error("Installed SDK file exceeds the 1 MiB read limit.");
    }
    return resolved;
}

} // namespace

nlohmann::json Index(const std::filesystem::path& sdkRoot, std::string_view keyword, size_t offset, size_t limit) {
    if (keyword.size() > 128u || keyword.find('\0') != std::string_view::npos || !TEXT::IsValidUtf8(keyword)) {
        throw std::runtime_error("SDK index keyword must be valid UTF-8 within 128 bytes.");
    }
    limit = std::clamp(limit, size_t{1}, kMaximumIndexPageSize);
    const std::string foldedKeyword = Fold(keyword);
    nlohmann::json result{{"complete", false},
                          {"guides", nlohmann::json::array()},
                          {"headers", nlohmann::json::array()},
                          {"missing", nlohmann::json::array()},
                          {"offset", offset},
                          {"limit", limit}};
    std::error_code error;
    const bool hasSdkRoot = std::filesystem::is_directory(sdkRoot, error) && !error;
    error.clear();
    const auto includeRoot = sdkRoot / "include/HIKARI";
    const auto docsRoot = sdkRoot / "docs";
    const bool hasInclude = hasSdkRoot && std::filesystem::is_directory(includeRoot, error) && !error;
    error.clear();
    const bool hasDocs = hasSdkRoot && std::filesystem::is_directory(docsRoot, error) && !error;
    error.clear();
    if (!hasSdkRoot) {
        result["missing"].push_back("Sdk package");
    }
    if (!hasInclude) {
        result["missing"].push_back("include/HIKARI");
    }
    if (!hasDocs) {
        result["missing"].push_back("docs");
    }

    if (hasDocs) {
        for (const GuideEntry& guide : kGuideEntries) {
            const std::string path = std::string("docs/") + guide.path;
            try {
                (void)ResolveInstalledFile(sdkRoot, path);
                result["guides"].push_back({{"path", path},
                                            {"title", guide.title},
                                            {"purpose", guide.purpose},
                                            {"startsWith", guide.startsWith}});
            } catch (const std::exception&) {
                result["missing"].push_back(path);
            }
        }
    }

    if (hasInclude) {
        const auto canonicalSdk = Canonical(sdkRoot);
        const auto canonicalInclude = Canonical(includeRoot);
        if (!IsBelow(canonicalInclude, canonicalSdk)) {
            throw std::runtime_error("Installed public include root escapes its SDK package.");
        }
        std::vector<std::string> headers;
        for (std::filesystem::recursive_directory_iterator it(canonicalInclude, error), end; !error && it != end;
             it.increment(error)) {
            const auto entryPath = it->path();
            if (IsReparsePoint(entryPath)) {
                it.disable_recursion_pending();
                continue;
            }
            const auto canonicalEntry = Canonical(entryPath);
            if (!IsBelow(canonicalEntry, canonicalInclude)) {
                it.disable_recursion_pending();
                continue;
            }
            if (std::filesystem::is_regular_file(canonicalEntry, error) && !error &&
                canonicalEntry.extension() == ".h") {
                headers.push_back(
                    "include/HIKARI/" +
                    TEXT::PathToUtf8(canonicalEntry.lexically_relative(canonicalInclude).generic_wstring()));
                if (headers.size() > kMaximumSdkHeaderCount) {
                    throw std::runtime_error("Installed SDK has more than 2048 public headers.");
                }
            }
            error.clear();
        }
        if (error) {
            throw std::runtime_error("Cannot enumerate installed public SDK headers.");
        }
        std::sort(headers.begin(), headers.end());
        std::vector<std::string> matching;
        for (auto& path : headers) {
            if (foldedKeyword.empty() || Fold(path).find(foldedKeyword) != std::string::npos) {
                matching.push_back(std::move(path));
            }
        }
        result["headerTotal"] = matching.size();
        const size_t first = (std::min)(offset, matching.size());
        const size_t last = (std::min)(first + limit, matching.size());
        for (size_t index = first; index < last; ++index) {
            result["headers"].push_back({{"path", matching[index]}});
        }
        if (last < matching.size()) {
            result["nextOffset"] = last;
        }
    }

    result["complete"] = hasSdkRoot && hasInclude && hasDocs && result["missing"].empty();
    return result;
}

nlohmann::json ReadFile(const std::filesystem::path& sdkRoot, const std::string& relativePath, size_t startLine,
                        size_t maxLines) {
    const auto path = ResolveInstalledFile(sdkRoot, relativePath);
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open installed public SDK file.");
    }
    std::string content;
    std::array<char, 16u * 1024u> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = input.gcount();
        if (bytesRead > 0) {
            if (content.size() + static_cast<size_t>(bytesRead) > kMaximumSdkFileBytes) {
                throw std::runtime_error("Installed SDK file grew beyond the 1 MiB read limit.");
            }
            content.append(buffer.data(), static_cast<size_t>(bytesRead));
        }
    }
    if (input.bad()) {
        throw std::runtime_error("Cannot read installed public SDK file.");
    }
    if (content.find('\0') != std::string::npos || !TEXT::IsValidUtf8(content)) {
        throw std::runtime_error("Installed public SDK file is not valid UTF-8 text.");
    }

    startLine = (std::max)(size_t{1}, startLine);
    maxLines = std::clamp(maxLines, size_t{1}, kMaximumReadLines);
    std::string excerpt;
    excerpt.reserve(kMaximumResponseBytes);
    size_t line = 1u, cursor = 0u, emittedLines = 0u, nextLine = 0u;
    while (cursor < content.size()) {
        const size_t newline = content.find('\n', cursor);
        const size_t lineEnd = newline == std::string::npos ? content.size() : newline + 1u;
        if (line >= startLine) {
            const size_t lineBytes = lineEnd - cursor;
            if (lineBytes > kMaximumResponseBytes) {
                throw std::runtime_error("A single installed SDK line exceeds the 32 KiB excerpt limit.");
            }
            if (emittedLines >= maxLines || excerpt.size() + lineBytes > kMaximumResponseBytes) {
                nextLine = line;
                break;
            }
            excerpt.append(content, cursor, lineBytes);
            ++emittedLines;
        }
        cursor = lineEnd;
        ++line;
    }
    nlohmann::json result{{"path", relativePath},
                          {"startLine", startLine},
                          {"linesReturned", emittedLines},
                          {"content", std::move(excerpt)}};
    if (nextLine != 0u) {
        result["nextLine"] = nextLine;
    }
    return result;
}

} // namespace HIKARI::EDITOR::PROJECT_SDK
