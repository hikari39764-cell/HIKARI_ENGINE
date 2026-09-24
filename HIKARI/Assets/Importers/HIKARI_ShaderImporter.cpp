#include "Assets/Importers/HIKARI_ShaderImporter.h"
#include <span>
#include <json.hpp>
#include "Assets/Formats/HIKARI_HshadercFormat.h"
#include "Assets/Importers/Internal/HIKARI_ShaderImporterValidation.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Shader/HIKARI_ShaderCookedArtifact.h"
#include "Assets/Shader/HIKARI_ShaderCookedArtifactBuilder.h"
#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "Gfx/HIKARI_ShaderCookCompiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
namespace HIKARI {
namespace {
namespace SHADER = ASSETS::SHADER;
static_assert(GFX::kShaderCookCompilerContractVersion == SHADER::kShaderCompilerContractVersion);
bool ResolveShaderManifestPath(const AssetRecord& record, const AssetImportContext& context,
                               std::filesystem::path& outManifest, std::filesystem::path& outProjectShaderRoot,
                               std::string& outError) {
    std::error_code ec{};
    const std::filesystem::path root = std::filesystem::weakly_canonical(context.projectRoot / "Assets/Shaders", ec);
    if (ec || !std::filesystem::is_directory(root, ec) || ec) {
        outError = "project shader root is missing";
        return false;
    }
    const std::filesystem::path manifest =
        std::filesystem::weakly_canonical(context.projectRoot / record.sourcePath, ec);
    if (ec || !PATHS::IsCanonicalDescendant(root, manifest) || !std::filesystem::is_regular_file(manifest, ec) || ec ||
        TEXT::ToLowerAsciiCopy(manifest.extension().string()) != ".hshader") {
        outError = ".hshader must be a regular file owned by Assets/Shaders";
        return false;
    }
    outManifest = manifest;
    outProjectShaderRoot = root;
    return true;
}
bool ResolveImportedDirectory(const AssetImportContext& context, std::filesystem::path& outDirectory,
                              std::string& outError) {
    std::error_code ec{};
    const std::filesystem::path library = std::filesystem::weakly_canonical(context.libraryRoot, ec);
    const std::filesystem::path imported = std::filesystem::weakly_canonical(context.importedDirectory, ec);
    if (ec || library.empty() || imported.empty() || !PATHS::IsCanonicalDescendant(library, imported) ||
        !std::filesystem::is_directory(imported, ec) || ec) {
        outError = "shader imported directory is outside the Library owner";
        return false;
    }
    outDirectory = imported;
    return true;
}
bool BuildArtifact(const AssetRecord& record, const SHADER::ShaderSourceManifest& manifest,
                   const HASH::Sha256Digest& manifestHash, const GFX::ShaderCookOutput& compiled,
                   SHADER::ShaderCookedArtifact& outArtifact, std::string& outError) {
    return SHADER::BuildShaderCookedArtifact(record.guid, manifest, manifestHash, SHADER::ShaderIncludeRoot::Project,
                                             compiled, outArtifact, outError);
}
void RemoveStaleShaderArtifacts(const std::filesystem::path& importedDirectory,
                                const std::filesystem::path& retainedArtifact, AssetImportPublication* publication) {
    std::error_code error{};
    for (std::filesystem::directory_iterator
             iterator(importedDirectory, std::filesystem::directory_options::skip_permission_denied, error),
         end;
         iterator != end; iterator.increment(error)) {
        if (error) {
            return;
        }
        const std::filesystem::path candidate = iterator->path();
        const std::string filename = TEXT::ToLowerAsciiCopy(candidate.filename().string());
        if (!iterator->is_regular_file(error) || error || !filename.starts_with("shader-") ||
            !filename.ends_with(".hshaderc")) {
            error.clear();
            continue;
        }
        if (publication) {
            if (candidate.lexically_normal() != retainedArtifact.lexically_normal())
                publication->DeleteOnPublish(candidate);
        } else if (!std::filesystem::equivalent(candidate, retainedArtifact, error) && !error) {
            (void)std::filesystem::remove(candidate, error);
        }
        error.clear();
    }
}
} // namespace
ASSETS::SEMANTICS::AssetImporterKind ShaderImporter::GetImporterKind() const noexcept {
    return ASSETS::SEMANTICS::AssetImporterKind::Shader;
}
AssetMeta ShaderImporter::CreateDefaultMeta(const std::filesystem::path& sourcePath, const AssetGuid& guid) const {
    AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(sourcePath, guid, GetImporterKind());
    meta.importSettingsJson = "{}";
    return meta;
}
AssetImportResult ShaderImporter::Import(const AssetRecord& record, const AssetImportContext& context) {
    AssetImportResult result{};
    if (!ASSETS::SEMANTICS::IsAssetSourceForImporter(record.sourcePath, GetImporterKind())) {
        result.message = "[ShaderImporter][ERROR] unsupported source";
        return result;
    }
    std::filesystem::path manifestPath{};
    std::filesystem::path projectShaderRoot{};
    std::filesystem::path importedDirectory{};
    std::string error{};
    if (!ResolveShaderManifestPath(record, context, manifestPath, projectShaderRoot, error)) {
        result.message = "[ShaderImporter][ERROR] " + error;
        return result;
    }
    if (!ResolveImportedDirectory(context, importedDirectory, error)) {
        result.message = "[ShaderImporter][ERROR] " + error;
        return result;
    }
    SHADER::ShaderSourceManifest manifest{};
    HASH::Sha256Digest manifestHash{};
    if (!SHADER::LoadShaderSourceManifest(manifestPath, manifest, error) ||
        !SHADER::ComputeShaderSourceManifestHash(manifest, manifestHash, error)) {
        result.message = "[ShaderImporter][ERROR] manifest validation failed: " + error;
        return result;
    }
    GFX::ShaderCookRequest request{};
    if (!SHADER::BuildShaderCookRequest(manifest, SHADER::ShaderIncludeRoot::Project, projectShaderRoot,
                                        GFX::ResolveShaderSourcePath(L"HIKARI/Shaders"), request, error)) {
        result.message = "[ShaderImporter][ERROR] " + error;
        return result;
    }
    GFX::ShaderCookOutput compiled{};
    if (!GFX::CompileShaderProgramForCook(request, compiled)) {
        result.message = "[ShaderImporter][ERROR] DXC cook failed: " + compiled.diagnostics;
        HIKARI_LOG_ERROR(result.message);
        return result;
    }
    SHADER::ShaderCookedArtifact artifact{};
    if (!BuildArtifact(record, manifest, manifestHash, compiled, artifact, error)) {
        result.message = "[ShaderImporter][ERROR] artifact assembly failed: " + error;
        return result;
    }
    std::vector<uint8_t> serialized{};
    HASH::Sha256Digest fileHash{};
    if (!SHADER::SerializeHshaderc(artifact, serialized, error) ||
        !HASH::ComputeSha256(std::as_bytes(std::span<const uint8_t>(serialized)), fileHash)) {
        result.message = "[ShaderImporter][ERROR] artifact serialization failed: " + error;
        return result;
    }
    const std::filesystem::path outputPath = importedDirectory / ("shader-" + HASH::ToHex(fileHash) + ".hshaderc");
    if (!ASSETS::IMPORTERS::INTERNAL::VerifyShaderImportSourceState(request, compiled.dependencies, manifestPath,
                                                                    manifestHash, error)) {
        result.message = "[ShaderImporter][ERROR] source changed before artifact "
                         "publication: " +
                         error;
        return result;
    }
    std::error_code existsError{};
    const bool artifactExisted = std::filesystem::is_regular_file(outputPath, existsError) && !existsError;
    const auto preparedPath = context.StageFile(outputPath);
    if (!SHADER::WriteHshadercFile(preparedPath, artifact, error)) {
        result.message = "[ShaderImporter][ERROR] artifact publication failed: " + error;
        return result;
    }
    if (!ASSETS::IMPORTERS::INTERNAL::VerifyShaderImportSourceState(request, compiled.dependencies, manifestPath,
                                                                    manifestHash, error)) {
        if (context.publication || !artifactExisted) {
            std::error_code removeError{};
            std::filesystem::remove(preparedPath, removeError);
        }
        result.message = "[ShaderImporter][ERROR] source closure changed before "
                         "publication: " +
                         error;
        return result;
    }
    RemoveStaleShaderArtifacts(importedDirectory, outputPath, context.publication.get());
    if (context.publication) {
        context.publication->AddValidation(
            [request, dependencies = compiled.dependencies, manifestPath, manifestHash](std::string& message) {
                return ASSETS::IMPORTERS::INTERNAL::VerifyShaderImportSourceState(request, dependencies, manifestPath,
                                                                                  manifestHash, message);
            });
    }
    ASSETS::IMPORTERS::INTERNAL::AppendShaderProjectDependencies(
        artifact, "Project/" + manifest.sourcePath.generic_string(), result);
    result.artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
        ASSETS::SEMANTICS::AssetArtifactKind::ShaderProgram,
        PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, outputPath)));
    result.diagnosticsJson =
        nlohmann::json{
            {"schema", "HIKARI.ShaderImportDiagnostics"},
            {"version", 1u},
            {"programId", artifact.programId},
            {"sourceClosureHash", HASH::ToHex(artifact.sourceClosureHash)},
            {"compilerVersion", artifact.compiler.version},
            {"entryCount", artifact.entries.size()},
            {"dependencyCount", artifact.dependencies.size()},
        }
            .dump(2);
    result.success = true;
    result.message = "[ShaderImporter] cooked HSHADERC: " +
                     PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, outputPath);
    HIKARI_LOG_INFO(result.message);
    return result;
}
} // namespace HIKARI
