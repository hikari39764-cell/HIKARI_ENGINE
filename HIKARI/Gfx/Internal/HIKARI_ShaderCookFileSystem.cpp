#include "Gfx/Internal/HIKARI_ShaderCookFileSystem.h"

#include <algorithm>
#include <fstream>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::GFX::INTERNAL {
    namespace {

        bool IsSafeRelativePath(
            const std::filesystem::path& path) {
            if (path.empty() || path.is_absolute() ||
                path.has_root_name() || path.has_root_directory()) {
                return false;
            }
            for (const std::filesystem::path& component : path) {
                if (component == "..") {
                    return false;
                }
            }
            return path.lexically_normal() == path;
        }

        bool IsContainedPath(
            const std::filesystem::path& root,
            const std::filesystem::path& file) {
            std::string rootKey = TEXT::ToLowerAsciiCopy(
                root.generic_string());
            const std::string fileKey = TEXT::ToLowerAsciiCopy(
                file.generic_string());
            while (!rootKey.empty() && rootKey.back() == '/') {
                rootKey.pop_back();
            }
            return !rootKey.empty() && fileKey.size() > rootKey.size() &&
                fileKey.compare(0u, rootKey.size(), rootKey) == 0 &&
                fileKey[rootKey.size()] == '/';
        }

        bool MakeRoot(
            ShaderCookIncludeRoot kind,
            const std::filesystem::path& physical,
            ShaderCookRoot& outRoot,
            std::string& outError) {
            std::error_code ec{};
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(physical, ec);
            if (ec || !std::filesystem::is_directory(canonical, ec) || ec) {
                outError = "shader include root is missing or invalid: " +
                    physical.generic_string();
                return false;
            }
            outRoot.kind = kind;
            outRoot.physicalRoot = canonical;
            outRoot.logicalPrefix = std::string(ToString(kind));
            return true;
        }

        bool ResolveFile(
            const ShaderCookRoot& root,
            const std::filesystem::path& candidate,
            std::filesystem::path& outPhysical,
            std::filesystem::path& outRelative,
            std::string& outError) {
            std::error_code ec{};
            const std::filesystem::path combined = candidate.is_absolute()
                ? candidate
                : root.physicalRoot / candidate;
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(combined, ec);
            if (ec || !IsContainedPath(root.physicalRoot, canonical) ||
                !std::filesystem::is_regular_file(canonical, ec) || ec) {
                outError = "shader source is outside its owned root or missing";
                return false;
            }
            const std::filesystem::path relative =
                canonical.lexically_relative(root.physicalRoot);
            if (!IsSafeRelativePath(relative)) {
                outError = "shader source could not form a safe logical path";
                return false;
            }
            outPhysical = canonical;
            outRelative = relative;
            return true;
        }

        bool ReadExactFile(
            const std::filesystem::path& path,
            std::vector<uint8_t>& outBytes,
            std::string& outError) {
            std::error_code ec{};
            const uintmax_t fileBytes = std::filesystem::file_size(path, ec);
            if (ec || fileBytes == 0u ||
                fileBytes > kMaximumShaderCookSourceFileBytes ||
                fileBytes > static_cast<uintmax_t>(SIZE_MAX)) {
                outError = "shader source size is outside its limit";
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                outError = "could not open shader source";
                return false;
            }
            std::vector<uint8_t> bytes(static_cast<size_t>(fileBytes));
            input.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input || input.gcount() !=
                    static_cast<std::streamsize>(bytes.size()) ||
                input.peek() != std::char_traits<char>::eof() ||
                std::find(bytes.begin(), bytes.end(), 0u) != bytes.end()) {
                outError = "shader source changed while reading or has NUL";
                return false;
            }
            outBytes = std::move(bytes);
            return true;
        }

        bool SameDependency(
            const ShaderCookDependency& lhs,
            const ShaderCookDependency& rhs) noexcept {
            return lhs.logicalPath == rhs.logicalPath &&
                lhs.byteSize == rhs.byteSize &&
                lhs.contentHash == rhs.contentHash;
        }

    } // namespace

    bool BuildShaderCookRoots(
        const ShaderCookRequest& request,
        std::vector<ShaderCookRoot>& outIncludeRoots,
        ShaderCookRoot& outProjectRoot,
        std::string& outError) {
        if (!MakeRoot(
                ShaderCookIncludeRoot::Project,
                request.projectShaderRoot,
                outProjectRoot,
                outError)) {
            return false;
        }
        std::vector<ShaderCookIncludeRoot> roots = request.includeRoots;
        std::sort(roots.begin(), roots.end());
        if (std::adjacent_find(roots.begin(), roots.end()) != roots.end()) {
            outError = "shader include root is duplicated";
            return false;
        }
        std::vector<ShaderCookRoot> resolved{};
        resolved.reserve(roots.size());
        for (ShaderCookIncludeRoot kind : roots) {
            ShaderCookRoot root{};
            if (kind == ShaderCookIncludeRoot::Project) {
                root = outProjectRoot;
            }
            else if (kind == ShaderCookIncludeRoot::EngineSdk) {
                if (!MakeRoot(
                        kind,
                        request.engineSdkIncludeRoot,
                        root,
                        outError)) {
                    return false;
                }
            }
            else {
                outError = "shader include root kind is invalid";
                return false;
            }
            resolved.push_back(std::move(root));
        }
        outIncludeRoots = std::move(resolved);
        outError.clear();
        return true;
    }

    bool ReadShaderCookFile(
        const ShaderCookRoot& root,
        const std::filesystem::path& relativePath,
        std::string_view requiredExtension,
        ShaderCookFile& outFile,
        std::string& outError) {
        if (!IsSafeRelativePath(relativePath) ||
            TEXT::ToLowerAsciiCopy(relativePath.extension().string()) !=
                requiredExtension) {
            outError = "shader source logical path or extension is invalid";
            return false;
        }
        std::filesystem::path physical{};
        std::filesystem::path relative{};
        if (!ResolveFile(
                root,
                relativePath,
                physical,
                relative,
                outError) ||
            TEXT::ToLowerAsciiCopy(relative.extension().string()) !=
                requiredExtension) {
            if (outError.empty()) {
                outError =
                    "resolved shader source extension is invalid";
            }
            return false;
        }
        ShaderCookFile file{};
        if (!ReadExactFile(physical, file.bytes, outError) ||
            !HASH::ComputeSha256(
                std::as_bytes(std::span<const uint8_t>(file.bytes)),
                file.dependency.contentHash)) {
            if (outError.empty()) {
                outError = "could not hash shader source";
            }
            return false;
        }
        file.physicalPath = physical;
        file.dependency.logicalPath = root.logicalPrefix + "/" +
            relative.generic_string();
        file.dependency.byteSize = file.bytes.size();
        outFile = std::move(file);
        outError.clear();
        return true;
    }

    bool ReadShaderCookPhysicalFile(
        const ShaderCookRoot& root,
        const std::filesystem::path& candidate,
        std::string_view requiredExtension,
        ShaderCookFile& outFile,
        std::string& outError) {
        std::filesystem::path physical{};
        std::filesystem::path relative{};
        if (!ResolveFile(
                root,
                candidate,
                physical,
                relative,
                outError)) {
            return false;
        }
        return ReadShaderCookFile(
            root,
            relative,
            requiredExtension,
            outFile,
            outError);
    }

    bool ReadShaderCookLogicalDependency(
        const ShaderCookRoot& projectRoot,
        const std::vector<ShaderCookRoot>& includeRoots,
        const ShaderCookDependency& expected,
        ShaderCookFile& outFile,
        std::string& outError) {
        const ShaderCookRoot* owner = nullptr;
        std::string_view relative{};
        if (expected.logicalPath.rfind("Project/", 0u) == 0u) {
            owner = &projectRoot;
            relative = std::string_view(expected.logicalPath).substr(8u);
        }
        else if (expected.logicalPath.rfind("EngineSdk/", 0u) == 0u) {
            for (const ShaderCookRoot& root : includeRoots) {
                if (root.kind == ShaderCookIncludeRoot::EngineSdk) {
                    owner = &root;
                    break;
                }
            }
            relative = std::string_view(expected.logicalPath).substr(10u);
        }
        const std::string extension = TEXT::ToLowerAsciiCopy(
            std::filesystem::path(expected.logicalPath)
                .extension().string());
        if (owner == nullptr || relative.empty() ||
            (extension != ".hlsl" && extension != ".hlsli" &&
             extension != ".h") ||
            !ReadShaderCookFile(
                *owner,
                std::filesystem::path(relative),
                extension,
                outFile,
                outError) ||
            !SameDependency(outFile.dependency, expected)) {
            if (outError.empty()) {
                outError = "shader dependency changed during cook";
            }
            return false;
        }
        return true;
    }

} // namespace HIKARI::GFX::INTERNAL
