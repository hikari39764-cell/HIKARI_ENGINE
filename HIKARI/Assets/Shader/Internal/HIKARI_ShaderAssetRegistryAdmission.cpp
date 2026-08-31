#include "Assets/Shader/Internal/HIKARI_ShaderAssetRegistryAdmission.h"

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "Assets/Formats/HIKARI_HshadercFormat.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Shader/Internal/HIKARI_ShaderArtifactValidation.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::SHADER::INTERNAL {
    namespace {

        bool ResolveOwnedProjectArtifact(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& relativePath,
            std::filesystem::path& outPath) {
            if (relativePath.empty() || relativePath.is_absolute() ||
                relativePath.has_root_name() ||
                relativePath.has_root_directory()) {
                return false;
            }
            for (const std::filesystem::path& component : relativePath) {
                if (component == "..") {
                    return false;
                }
            }

            std::error_code ec{};
            const std::filesystem::path root =
                std::filesystem::weakly_canonical(projectRoot, ec);
            if (ec) {
                return false;
            }
            ec.clear();
            const std::filesystem::path artifact =
                std::filesystem::weakly_canonical(
                    projectRoot / relativePath,
                    ec);
            if (ec) {
                return false;
            }

            std::string rootKey = TEXT::ToLowerAsciiCopy(
                root.generic_string());
            const std::string artifactKey = TEXT::ToLowerAsciiCopy(
                artifact.generic_string());
            while (!rootKey.empty() && rootKey.back() == '/') {
                rootKey.pop_back();
            }
            if (rootKey.empty() || artifactKey.size() <= rootKey.size() ||
                artifactKey.compare(0u, rootKey.size(), rootKey) != 0 ||
                artifactKey[rootKey.size()] != '/') {
                return false;
            }
            outPath = artifact;
            return true;
        }

    } // namespace

    bool AppendProjectShaderAssetDescriptor(
        const ShaderAssetRegistryAdmissionContext& context,
        const AssetRecord& record,
        AssetRegistry& registry) {
        if (context.mode !=
                ShaderAssetRegistryAdmissionMode::Authoring &&
            context.mode !=
                ShaderAssetRegistryAdmissionMode::Cooked) {
            HIKARI_LOG_ERROR(
                "[ShaderAssetRegistry] admission mode is invalid");
            return false;
        }
        if (record.type != AssetType::Shader) {
            HIKARI_LOG_ERROR(
                "[ShaderAssetRegistry] non-Shader admission request");
            return false;
        }
        // Failing the isolated staged build preserves the last complete live
        // registry while Editor import catches up. Shipping cooked records
        // enter this path only after strict manifest admission.
        if (record.importOutdated || record.artifactMissing ||
            !record.artifactManifest.lastImportSucceeded) {
            HIKARI_LOG_ERROR(
                "[ShaderAssetRegistry] artifact is not import-ready: " +
                record.guid.value);
            return false;
        }
        std::string authorityError{};
        if (!ValidateShaderAssetRecordAuthority(
                record,
                authorityError)) {
            HIKARI_LOG_ERROR(
                "[ShaderAssetRegistry] " + authorityError);
            return false;
        }

        const auto& semantics =
            ASSETS::SEMANTICS::GetAssetArtifactSemantics(
                ASSETS::SEMANTICS::AssetArtifactKind::ShaderProgram);
        const AssetArtifactDesc* authority = nullptr;
        for (const AssetArtifactDesc& artifact :
             record.artifactManifest.artifacts) {
            if (artifact.role != semantics.role) {
                continue;
            }
            if (!ASSETS::SEMANTICS::MatchesAssetArtifact(
                    artifact,
                    ASSETS::SEMANTICS::
                        AssetArtifactKind::ShaderProgram) ||
                ASSETS::SEMANTICS::ClassifyCookedAssetFormat(
                    artifact.path) != CookedAssetFormat::HSHADERC ||
                authority != nullptr || artifact.path.empty()) {
                HIKARI_LOG_ERROR(
                    "[ShaderAssetRegistry] duplicate or invalid artifact "
                    "authority: " + record.guid.value);
                return false;
            }
            authority = &artifact;
        }
        if (authority == nullptr) {
            HIKARI_LOG_ERROR(
                "[ShaderAssetRegistry] exact HSHADERC artifact is "
                "missing: " + record.guid.value);
            return false;
        }

        std::filesystem::path artifactPath{};
        if (!ResolveOwnedProjectArtifact(
                context.projectRoot,
                authority->path,
                artifactPath)) {
            HIKARI_LOG_ERROR(
                "[ShaderAssetRegistry] unsafe artifact path: " +
                record.guid.value);
            return false;
        }
        if (context.mode ==
            ShaderAssetRegistryAdmissionMode::Authoring) {
            std::string ownerError{};
            if (!ValidateShaderAuthoringArtifactOwnership(
                    record,
                    context.projectRoot,
                    authority->path,
                    ownerError)) {
                HIKARI_LOG_ERROR(
                    "[ShaderAssetRegistry] " + ownerError);
                return false;
            }
        }
        std::string contentAddressError{};
        if (!ValidateContentAddressedShaderArtifact(
                artifactPath,
                contentAddressError)) {
            HIKARI_LOG_ERROR(
                "[ShaderAssetRegistry] " + contentAddressError);
            return false;
        }

        auto program = std::make_shared<ShaderCookedArtifact>();
        std::string readError{};
        if (!ReadHshadercFile(
                artifactPath,
                *program,
                readError)) {
            HIKARI_LOG_ERROR(
                "[ShaderAssetRegistry] artifact admission failed: " +
                record.guid.value + ": " +
                (readError.empty()
                    ? "HSHADERC read failed"
                    : readError));
            return false;
        }
        if (program->sourceGuid != record.guid) {
            HIKARI_LOG_ERROR(
                "[ShaderAssetRegistry] source GUID mismatch: " +
                record.guid.value);
            return false;
        }
        if (context.mode ==
            ShaderAssetRegistryAdmissionMode::Authoring) {
            std::string sourceError{};
            if (!ValidateShaderArtifactAgainstAuthoringSources(
                    record,
                    *program,
                    context.projectRoot,
                    context.engineShaderRoot,
                    sourceError)) {
                HIKARI_LOG_ERROR(
                    "[ShaderAssetRegistry] " + sourceError);
                return false;
            }
        }

        auto descriptor = std::make_unique<ShaderAssetDescriptor>();
        descriptor->id.value = record.guid.value;
        descriptor->type = AssetType::Shader;
        descriptor->sourcePath = authority->path;
        descriptor->version = record.meta.importerVersion;
        descriptor->program = std::move(program);
        return registry.RegisterDescriptor(std::move(descriptor));
    }

} // namespace HIKARI::ASSETS::SHADER::INTERNAL
