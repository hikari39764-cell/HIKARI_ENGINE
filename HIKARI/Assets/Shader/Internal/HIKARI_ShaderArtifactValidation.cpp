#include "Assets/Shader/Internal/HIKARI_ShaderArtifactValidation.h"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Shader/HIKARI_ShaderCookedArtifact.h"
#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::SHADER::INTERNAL {
    namespace {

        bool IsSafeRelativePath(
            const std::filesystem::path& path) noexcept {
            if (path.empty() || path.is_absolute() ||
                path.has_root_directory() || path.has_root_name()) {
                return false;
            }
            for (const std::filesystem::path& component : path) {
                if (component == "..") {
                    return false;
                }
            }
            return true;
        }

        bool ResolveContainedFilePath(
            const std::filesystem::path& root,
            const std::filesystem::path& relativePath,
            std::filesystem::path& outPath) {
            if (!IsSafeRelativePath(relativePath)) {
                return false;
            }
            std::error_code ec{};
            const std::filesystem::path canonicalRoot =
                std::filesystem::weakly_canonical(root, ec);
            if (ec) {
                return false;
            }
            ec.clear();
            const std::filesystem::path candidate =
                std::filesystem::weakly_canonical(
                    canonicalRoot / relativePath,
                    ec);
            if (ec) {
                return false;
            }

            std::string rootKey = TEXT::ToLowerAsciiCopy(
                canonicalRoot.generic_string());
            const std::string candidateKey = TEXT::ToLowerAsciiCopy(
                candidate.generic_string());
            while (!rootKey.empty() && rootKey.back() == '/') {
                rootKey.pop_back();
            }
            if (rootKey.empty() ||
                candidateKey.size() <= rootKey.size() ||
                candidateKey.compare(
                    0u,
                    rootKey.size(),
                    rootKey) != 0 ||
                candidateKey[rootKey.size()] != '/') {
                return false;
            }
            outPath = candidate;
            return true;
        }

    } // namespace

    bool ValidateContentAddressedShaderArtifact(
        const std::filesystem::path& artifactPath,
        std::string& outError) {
        const std::string filename = TEXT::ToLowerAsciiCopy(
            artifactPath.filename().string());
        constexpr std::string_view prefix = "shader-";
        constexpr std::string_view suffix = ".hshaderc";
        const size_t expectedLength =
            prefix.size() + 64u + suffix.size();
        HASH::Sha256Digest namedHash{};
        HASH::Sha256Digest fileHash{};
        if (filename.size() != expectedLength ||
            !filename.starts_with(prefix) ||
            !filename.ends_with(suffix) ||
            !HASH::ParseSha256(
                std::string_view(filename).substr(prefix.size(), 64u),
                namedHash) ||
            !HASH::ComputeFileSha256(
                artifactPath,
                fileHash,
                outError) ||
            namedHash != fileHash) {
            if (outError.empty()) {
                outError = "Shader artifact filename/content hash mismatch";
            }
            return false;
        }
        outError.clear();
        return true;
    }

    bool ValidateShaderAssetRecordAuthority(
        const AssetRecord& record,
        std::string& outError) {
        const auto& importer =
            ASSETS::SEMANTICS::GetAssetImporterSemantics(
                ASSETS::SEMANTICS::AssetImporterKind::Shader);
        const std::string sourcePath =
            record.sourcePath.lexically_normal().generic_string();
        const AssetArtifactManifest& manifest =
            record.artifactManifest;
        if (record.type != AssetType::Shader ||
            !IsValidAssetGuid(record.guid.value) ||
            record.meta.guid != record.guid ||
            record.meta.type != record.type ||
            record.meta.sourcePath != sourcePath ||
            record.meta.importerId != importer.importerId ||
            record.meta.importerVersion != importer.importerVersion ||
            manifest.manifestVersion !=
                kAssetArtifactManifestVersion ||
            manifest.guid != record.guid ||
            manifest.sourcePath != sourcePath ||
            manifest.importerId != record.meta.importerId ||
            manifest.importerVersion !=
                record.meta.importerVersion ||
            !manifest.lastImportSucceeded) {
            outError =
                "Shader AssetRecord/meta/artifact-manifest authority "
                "does not match: " + record.guid.value;
            return false;
        }
        outError.clear();
        return true;
    }

    bool ValidateShaderAuthoringArtifactOwnership(
        const AssetRecord& record,
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& artifactRelativePath,
        std::string& outError) {
        if (record.importedDirectory.empty()) {
            outError =
                "Shader imported owner directory is missing: " +
                record.guid.value;
            return false;
        }
        std::filesystem::path artifactPath{};
        if (!ResolveContainedFilePath(
                projectRoot,
                artifactRelativePath,
                artifactPath)) {
            outError =
                "Shader artifact resolves outside project root: " +
                record.guid.value;
            return false;
        }

        const std::filesystem::path importedRoot =
            record.importedDirectory.is_absolute()
                ? record.importedDirectory
                : projectRoot / record.importedDirectory;
        std::error_code ec{};
        const std::filesystem::path importedRelative =
            std::filesystem::relative(
                artifactPath,
                importedRoot,
                ec);
        std::filesystem::path ownedArtifactPath{};
        if (ec || !ResolveContainedFilePath(
                importedRoot,
                importedRelative,
                ownedArtifactPath) ||
            TEXT::ToLowerAsciiCopy(
                ownedArtifactPath.generic_string()) !=
            TEXT::ToLowerAsciiCopy(
                artifactPath.generic_string())) {
            outError =
                "Shader artifact is outside its imported owner "
                "directory: " + record.guid.value;
            return false;
        }
        if (!ValidateContentAddressedShaderArtifact(
                ownedArtifactPath,
                outError)) {
            outError += ": " + record.guid.value;
            return false;
        }
        outError.clear();
        return true;
    }

    bool ValidateShaderArtifactAgainstAuthoringSources(
        const AssetRecord& record,
        const ShaderCookedArtifact& artifact,
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& engineShaderRoot,
        std::string& outError) {
        if (!ValidateShaderAssetRecordAuthority(record, outError) ||
            artifact.sourceGuid != record.guid) {
            if (outError.empty()) {
                outError = "Shader artifact source GUID mismatch";
            }
            return false;
        }

        const std::filesystem::path projectShaderRoot =
            projectRoot / "Assets/Shaders";
        std::error_code ec{};
        const std::filesystem::path manifestRelative =
            std::filesystem::relative(
                projectRoot / record.sourcePath,
                projectShaderRoot,
                ec);
        std::filesystem::path manifestPath{};
        if (ec || !ResolveContainedFilePath(
                projectShaderRoot,
                manifestRelative,
                manifestPath)) {
            outError =
                "Shader manifest resolves outside Assets/Shaders: " +
                record.guid.value;
            return false;
        }

        ShaderSourceManifest manifest{};
        HASH::Sha256Digest manifestHash{};
        if (!LoadShaderSourceManifest(
                manifestPath,
                manifest,
                outError) ||
            !ComputeShaderSourceManifestHash(
                manifest,
                manifestHash,
                outError) ||
            manifestHash != artifact.manifestHash) {
            outError =
                "Shader manifest changed; reimport asset " +
                artifact.sourceGuid.value;
            return false;
        }

        const std::string mainLogicalPath = "Project/" +
            manifest.sourcePath.lexically_normal().generic_string();
        const bool hasMainSource = std::any_of(
            artifact.dependencies.begin(),
            artifact.dependencies.end(),
            [&mainLogicalPath](
                const ShaderSourceDependency& dependency) {
                return dependency.logicalPath == mainLogicalPath;
            });
        if (!hasMainSource ||
            artifact.programId != manifest.programId ||
            artifact.engineInterfaceId != manifest.engineInterfaceId ||
            artifact.engineInterfaceVersion !=
                manifest.engineInterfaceVersion ||
            artifact.engineInterfaceHash !=
                manifest.engineInterfaceHash ||
            artifact.rootLayoutId != manifest.rootLayoutId ||
            artifact.rootLayoutVersion != manifest.rootLayoutVersion ||
            artifact.rootLayoutHash != manifest.rootLayoutHash ||
            artifact.requiredCaps.minimumShaderModel !=
                manifest.requiredCaps.minimumShaderModel ||
            artifact.requiredCaps.minimumRootSignatureVersion !=
                manifest.requiredCaps.minimumRootSignatureVersion ||
            artifact.requiredCaps.requiredFeatureMask !=
                manifest.requiredCaps.requiredFeatureMask ||
            artifact.entries.size() != manifest.entries.size()) {
            outError =
                "Shader artifact does not match its current manifest: " +
                record.guid.value;
            return false;
        }

        std::vector<ShaderEntryPoint> expectedEntries = manifest.entries;
        std::sort(
            expectedEntries.begin(),
            expectedEntries.end(),
            [](const ShaderEntryPoint& lhs,
               const ShaderEntryPoint& rhs) {
                return lhs.id < rhs.id;
            });
        for (size_t index = 0u;
            index < expectedEntries.size();
            ++index) {
            const ShaderEntryPoint& expected = expectedEntries[index];
            const ShaderEntryPoint& actual =
                artifact.entries[index].entry;
            if (actual.id != expected.id ||
                actual.stage != expected.stage ||
                actual.entry != expected.entry ||
                actual.profile != expected.profile) {
                outError =
                    "Shader artifact entry contract is stale: " +
                    record.guid.value;
                return false;
            }
        }

        for (const ShaderSourceDependency& dependency :
             artifact.dependencies) {
            const std::filesystem::path* root = nullptr;
            std::string_view relative{};
            if (dependency.logicalPath.rfind("Project/", 0u) == 0u) {
                root = &projectShaderRoot;
                relative = std::string_view(
                    dependency.logicalPath).substr(8u);
            }
            else if (dependency.logicalPath.rfind(
                    "EngineSdk/", 0u) == 0u &&
                engineShaderRoot.is_absolute()) {
                root = &engineShaderRoot;
                relative = std::string_view(
                    dependency.logicalPath).substr(10u);
            }

            std::filesystem::path sourcePath{};
            HASH::Sha256Digest sourceHash{};
            std::string hashError{};
            ec.clear();
            if (root == nullptr || relative.empty() ||
                !ResolveContainedFilePath(
                    *root,
                    std::filesystem::path(relative),
                    sourcePath) ||
                std::filesystem::file_size(sourcePath, ec) !=
                    dependency.byteSize || ec ||
                !HASH::ComputeFileSha256(
                    sourcePath,
                    sourceHash,
                    hashError) ||
                sourceHash != dependency.contentHash) {
                outError =
                    "Shader source closure changed; reimport asset " +
                    artifact.sourceGuid.value;
                return false;
            }
        }
        outError.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER::INTERNAL
