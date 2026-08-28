#include "Gfx/HIKARI_ShaderCookCompiler.h"

#include <exception>
#include <unordered_set>

#include "Core/Text/HIKARI_AsciiCase.h"
#include "Gfx/Internal/HIKARI_ShaderCookFileSystem.h"

namespace HIKARI::GFX {

    bool VerifyShaderCookDependencies(
        const ShaderCookRequest& request,
        const std::vector<ShaderCookDependency>& dependencies,
        std::string& outError) noexcept {
        try {
            if (dependencies.empty() ||
                dependencies.size() > kMaximumShaderCookDependencies) {
                outError = "shader dependency verification count is invalid";
                return false;
            }
            INTERNAL::ShaderCookRoot sourceRoot{};
            std::vector<INTERNAL::ShaderCookRoot> includeRoots{};
            if (!INTERNAL::BuildShaderCookRoots(
                    request,
                    includeRoots,
                    sourceRoot,
                    outError)) {
                return false;
            }
            INTERNAL::ShaderCookFile expectedPrimary{};
            if (!INTERNAL::ReadShaderCookFile(
                    sourceRoot,
                    request.sourcePath,
                    ".hlsl",
                    expectedPrimary,
                    outError)) {
                return false;
            }
            std::unordered_set<std::string> logicalPaths{};
            std::string previous{};
            uint64_t totalBytes = 0u;
            bool hasPrimary = false;
            for (const ShaderCookDependency& dependency : dependencies) {
                const std::string key =
                    TEXT::ToLowerAsciiCopy(dependency.logicalPath);
                if ((!previous.empty() &&
                     dependency.logicalPath <= previous) ||
                    !logicalPaths.insert(key).second ||
                    dependency.byteSize == 0u ||
                    dependency.byteSize > kMaximumShaderCookSourceFileBytes ||
                    dependency.byteSize >
                        kMaximumShaderCookClosureBytes - totalBytes) {
                    outError =
                        "shader dependency verification table is invalid";
                    return false;
                }
                INTERNAL::ShaderCookFile verified{};
                if (!INTERNAL::ReadShaderCookLogicalDependency(
                        sourceRoot,
                        includeRoots,
                        dependency,
                        verified,
                        outError)) {
                    return false;
                }
                hasPrimary = hasPrimary ||
                    (dependency.logicalPath ==
                        expectedPrimary.dependency.logicalPath &&
                     dependency.byteSize ==
                        expectedPrimary.dependency.byteSize &&
                     dependency.contentHash ==
                        expectedPrimary.dependency.contentHash);
                totalBytes += dependency.byteSize;
                previous = dependency.logicalPath;
            }
            if (!hasPrimary) {
                outError =
                    "shader dependency closure is missing its primary source";
                return false;
            }
            outError.clear();
            return true;
        }
        catch (const std::exception& error) {
            outError = std::string(
                "shader dependency verification failed: ") + error.what();
            return false;
        }
    }

} // namespace HIKARI::GFX
