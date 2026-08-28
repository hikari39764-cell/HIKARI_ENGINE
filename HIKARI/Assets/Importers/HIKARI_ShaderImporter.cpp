#include "Assets/Importers/HIKARI_ShaderImporter.h"
#include <algorithm>
#include <span>
#include <unordered_map>
#include <json.hpp>
#include "Assets/Formats/HIKARI_HshadercFormat.h"
#include "Assets/Importers/Internal/HIKARI_ShaderImporterValidation.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Shader/HIKARI_ShaderCookedArtifact.h"
#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Gfx/HIKARI_ShaderCookCompiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
namespace HIKARI {
    namespace {
        namespace SHADER = ASSETS::SHADER;
        static_assert(
            GFX::kShaderCookCompilerContractVersion ==
            SHADER::kShaderCompilerContractVersion);
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
        bool ResolveShaderManifestPath(
            const AssetRecord& record,
            const AssetImportContext& context,
            std::filesystem::path& outManifest,
            std::filesystem::path& outProjectShaderRoot,
            std::string& outError) {
            std::error_code ec{};
            const std::filesystem::path root =
                std::filesystem::weakly_canonical(
                    context.projectRoot / "Assets/Shaders",
                    ec);
            if (ec || !std::filesystem::is_directory(root, ec) || ec) {
                outError = "project shader root is missing";
                return false;
            }
            const std::filesystem::path manifest =
                std::filesystem::weakly_canonical(
                    context.projectRoot / record.sourcePath,
                    ec);
            if (ec || !IsContainedPath(root, manifest) ||
                !std::filesystem::is_regular_file(manifest, ec) || ec ||
                TEXT::ToLowerAsciiCopy(manifest.extension().string()) !=
                    ".hshader") {
                outError =
                    ".hshader must be a regular file owned by Assets/Shaders";
                return false;
            }
            outManifest = manifest;
            outProjectShaderRoot = root;
            return true;
        }
        bool ResolveImportedDirectory(
            const AssetImportContext& context,
            std::filesystem::path& outDirectory,
            std::string& outError) {
            std::error_code ec{};
            const std::filesystem::path library =
                std::filesystem::weakly_canonical(context.libraryRoot, ec);
            const std::filesystem::path imported =
                std::filesystem::weakly_canonical(
                    context.importedDirectory,
                    ec);
            if (ec || library.empty() || imported.empty() ||
                !IsContainedPath(library, imported) ||
                !std::filesystem::is_directory(imported, ec) || ec) {
                outError =
                    "shader imported directory is outside the Library owner";
                return false;
            }
            outDirectory = imported;
            return true;
        }
        GFX::ShaderStage ToGfxStage(SHADER::ShaderStage stage) {
            switch (stage) {
            case SHADER::ShaderStage::Vertex:
                return GFX::ShaderStage::Vertex;
            case SHADER::ShaderStage::Pixel:
                return GFX::ShaderStage::Pixel;
            case SHADER::ShaderStage::Compute:
                return GFX::ShaderStage::Compute;
            case SHADER::ShaderStage::Amplification:
                return GFX::ShaderStage::Amplification;
            case SHADER::ShaderStage::Mesh:
                return GFX::ShaderStage::Mesh;
            case SHADER::ShaderStage::Count:
                break;
            }
            return GFX::ShaderStage::Pixel;
        }
        GFX::ShaderCookIncludeRoot ToGfxRoot(
            SHADER::ShaderIncludeRoot root) {
            switch (root) {
            case SHADER::ShaderIncludeRoot::Project:
                return GFX::ShaderCookIncludeRoot::Project;
            case SHADER::ShaderIncludeRoot::EngineSdk:
                return GFX::ShaderCookIncludeRoot::EngineSdk;
            case SHADER::ShaderIncludeRoot::Count:
                break;
            }
            return GFX::ShaderCookIncludeRoot::Count;
        }
        SHADER::ShaderResourceBindingKind ToArtifactBindingKind(
            GFX::ShaderCookBindingKind kind) {
            switch (kind) {
            case GFX::ShaderCookBindingKind::ConstantBuffer:
                return SHADER::ShaderResourceBindingKind::ConstantBuffer;
            case GFX::ShaderCookBindingKind::ShaderResource:
                return SHADER::ShaderResourceBindingKind::ShaderResource;
            case GFX::ShaderCookBindingKind::UnorderedAccess:
                return SHADER::ShaderResourceBindingKind::UnorderedAccess;
            case GFX::ShaderCookBindingKind::Sampler:
                return SHADER::ShaderResourceBindingKind::Sampler;
            case GFX::ShaderCookBindingKind::Count:
                break;
            }
            return SHADER::ShaderResourceBindingKind::Count;
        }
        bool BuildCookRequest(
            const SHADER::ShaderSourceManifest& manifest,
            const std::filesystem::path& projectShaderRoot,
            GFX::ShaderCookRequest& outRequest,
            std::string& outError) {
            GFX::ShaderCookRequest request{};
            request.projectShaderRoot = projectShaderRoot;
            request.sourcePath = manifest.sourcePath;
            request.engineSdkIncludeRoot =
                GFX::ResolveShaderSourcePath(
                    L"HIKARI/Shaders");
            request.entries.reserve(manifest.entries.size());
            for (const SHADER::ShaderEntryPoint& entry : manifest.entries) {
                request.entries.push_back({
                    entry.id,
                    ToGfxStage(entry.stage),
                    entry.entry,
                    entry.profile
                });
            }
            request.defines.reserve(manifest.defines.size());
            for (const SHADER::ShaderDefine& define : manifest.defines) {
                request.defines.push_back({ define.name, define.value });
            }
            request.includeRoots.reserve(manifest.includeRoots.size());
            bool needsEngineSdk = false;
            for (SHADER::ShaderIncludeRoot root : manifest.includeRoots) {
                request.includeRoots.push_back(ToGfxRoot(root));
                needsEngineSdk = needsEngineSdk ||
                    root == SHADER::ShaderIncludeRoot::EngineSdk;
            }
            request.engineInterface = {
                manifest.engineInterfaceId,
                manifest.engineInterfaceVersion,
                manifest.engineInterfaceHash
            };
            request.rootLayout = {
                manifest.rootLayoutId,
                manifest.rootLayoutVersion,
                manifest.rootLayoutHash
            };
            request.requiredCaps = {
                manifest.requiredCaps.minimumShaderModel,
                manifest.requiredCaps.minimumRootSignatureVersion,
                manifest.requiredCaps.requiredFeatureMask
            };
            if (needsEngineSdk &&
                !request.engineSdkIncludeRoot.is_absolute()) {
                outError =
                    "engine Shader SDK root is not initialized for cook";
                return false;
            }
            outRequest = std::move(request);
            return true;
        }
        bool BuildArtifact(
            const AssetRecord& record,
            const SHADER::ShaderSourceManifest& manifest,
            const HASH::Sha256Digest& manifestHash,
            const GFX::ShaderCookOutput& compiled,
            SHADER::ShaderCookedArtifact& outArtifact,
            std::string& outError) {
            if (!compiled.success ||
                compiled.toolchain.contractVersion !=
                    SHADER::kShaderCompilerContractVersion ||
                compiled.entries.size() != manifest.entries.size() ||
                compiled.engineInterface.id != manifest.engineInterfaceId ||
                compiled.engineInterface.version !=
                    manifest.engineInterfaceVersion ||
                compiled.engineInterface.hash !=
                    manifest.engineInterfaceHash ||
                compiled.rootLayout.id != manifest.rootLayoutId ||
                compiled.rootLayout.version != manifest.rootLayoutVersion ||
                compiled.rootLayout.hash != manifest.rootLayoutHash ||
                compiled.requiredCaps.minimumShaderModel !=
                    manifest.requiredCaps.minimumShaderModel ||
                compiled.requiredCaps.minimumRootSignatureVersion !=
                    manifest.requiredCaps.minimumRootSignatureVersion ||
                compiled.requiredCaps.requiredFeatureMask !=
                    manifest.requiredCaps.requiredFeatureMask) {
                outError = "shader compiler output identity is invalid";
                return false;
            }
            SHADER::ShaderCookedArtifact artifact{};
            artifact.sourceGuid = record.guid;
            artifact.programId = manifest.programId;
            artifact.manifestHash = manifestHash;
            artifact.compiler = {
                compiled.toolchain.compilerId,
                compiled.toolchain.compilerVersion,
                compiled.toolchain.contractVersion,
                compiled.toolchain.compilerBinaryHash,
                compiled.toolchain.validatorBinaryHash
            };
            artifact.engineInterfaceId = compiled.engineInterface.id;
            artifact.engineInterfaceVersion =
                compiled.engineInterface.version;
            artifact.engineInterfaceHash = compiled.engineInterface.hash;
            artifact.rootLayoutId = compiled.rootLayout.id;
            artifact.rootLayoutVersion = compiled.rootLayout.version;
            artifact.rootLayoutHash = compiled.rootLayout.hash;
            artifact.requiredCaps = {
                compiled.requiredCaps.minimumShaderModel,
                compiled.requiredCaps.minimumRootSignatureVersion,
                compiled.requiredCaps.requiredFeatureMask
            };
            artifact.dependencies.reserve(compiled.dependencies.size());
            for (const GFX::ShaderCookDependency& dependency :
                 compiled.dependencies) {
                artifact.dependencies.push_back({
                    dependency.logicalPath,
                    dependency.byteSize,
                    dependency.contentHash
                });
            }
            const std::string mainLogicalPath = "Project/" +
                manifest.sourcePath.lexically_normal().generic_string();
            if (std::none_of(
                    artifact.dependencies.begin(),
                    artifact.dependencies.end(),
                    [&mainLogicalPath](
                        const SHADER::ShaderSourceDependency& dependency) {
                        return dependency.logicalPath == mainLogicalPath;
                    })) {
                outError =
                    "manifest source path does not match its canonical file";
                return false;
            }
            if (!SHADER::ComputeShaderSourceClosureHash(
                    artifact.manifestHash,
                    artifact.dependencies,
                    artifact.sourceClosureHash)) {
                outError = "could not compute shader source closure hash";
                return false;
            }
            std::unordered_map<
                std::string,
                const SHADER::ShaderEntryPoint*> entriesById{};
            for (const SHADER::ShaderEntryPoint& entry : manifest.entries) {
                entriesById.emplace(entry.id, &entry);
            }
            artifact.entries.reserve(compiled.entries.size());
            for (const GFX::ShaderCookEntryOutput& compiledEntry :
                 compiled.entries) {
                const auto found = entriesById.find(compiledEntry.id);
                if (found == entriesById.end()) {
                    outError = "compiled shader entry is not in the manifest";
                    return false;
                }
                SHADER::ShaderCookedEntry entry{};
                entry.entry = *found->second;
                entry.compilerArguments =
                    compiledEntry.canonicalArguments;
                entry.dxil = compiledEntry.dxil;
                entry.bindings.reserve(compiledEntry.bindings.size());
                for (const GFX::ShaderCookBinding& binding :
                     compiledEntry.bindings) {
                    entry.bindings.push_back({
                        ToArtifactBindingKind(binding.kind),
                        binding.registerIndex,
                        binding.registerSpace,
                        binding.count,
                        binding.visibilityMask
                    });
                }
                if (!SHADER::ComputeShaderArgumentsHash(
                        entry.compilerArguments,
                        entry.compilerArgumentsHash) ||
                    !SHADER::ComputeShaderReflectionHash(
                        entry.bindings,
                        entry.reflectionHash) ||
                    !HASH::ComputeSha256(
                        std::as_bytes(
                            std::span<const uint8_t>(entry.dxil)),
                        entry.dxilHash)) {
                    outError = "could not hash compiled shader entry";
                    return false;
                }
                artifact.entries.push_back(std::move(entry));
            }
            if (!SHADER::ValidateShaderCookedArtifact(artifact, outError)) {
                return false;
            }
            outArtifact = std::move(artifact);
            return true;
        }
    } // namespace
    ASSETS::SEMANTICS::AssetImporterKind
        ShaderImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::Shader;
    }
    AssetMeta ShaderImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {
        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson = "{}";
        return meta;
    }
    AssetImportResult ShaderImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {
        AssetImportResult result{};
        if (!ASSETS::SEMANTICS::IsAssetSourceForImporter(
                record.sourcePath,
                GetImporterKind())) {
            result.message = "[ShaderImporter][ERROR] unsupported source";
            return result;
        }
        std::filesystem::path manifestPath{};
        std::filesystem::path projectShaderRoot{};
        std::filesystem::path importedDirectory{};
        std::string error{};
        if (!ResolveShaderManifestPath(
                record,
                context,
                manifestPath,
                projectShaderRoot,
                error)) {
            result.message = "[ShaderImporter][ERROR] " + error;
            return result;
        }
        if (!ResolveImportedDirectory(
                context,
                importedDirectory,
                error)) {
            result.message = "[ShaderImporter][ERROR] " + error;
            return result;
        }
        SHADER::ShaderSourceManifest manifest{};
        HASH::Sha256Digest manifestHash{};
        if (!SHADER::LoadShaderSourceManifest(
                manifestPath,
                manifest,
                error) ||
            !SHADER::ComputeShaderSourceManifestHash(
                manifest,
                manifestHash,
                error)) {
            result.message =
                "[ShaderImporter][ERROR] manifest validation failed: " +
                error;
            return result;
        }
        GFX::ShaderCookRequest request{};
        if (!BuildCookRequest(
                manifest,
                projectShaderRoot,
                request,
                error)) {
            result.message = "[ShaderImporter][ERROR] " + error;
            return result;
        }
        GFX::ShaderCookOutput compiled{};
        if (!GFX::CompileShaderProgramForCook(request, compiled)) {
            result.message = "[ShaderImporter][ERROR] DXC cook failed: " +
                compiled.diagnostics;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }
        SHADER::ShaderCookedArtifact artifact{};
        if (!BuildArtifact(
                record,
                manifest,
                manifestHash,
                compiled,
                artifact,
                error)) {
            result.message =
                "[ShaderImporter][ERROR] artifact assembly failed: " +
                error;
            return result;
        }
        std::vector<uint8_t> serialized{};
        HASH::Sha256Digest fileHash{};
        if (!SHADER::SerializeHshaderc(
                artifact,
                serialized,
                error) ||
            !HASH::ComputeSha256(
                std::as_bytes(
                    std::span<const uint8_t>(serialized)),
                fileHash)) {
            result.message =
                "[ShaderImporter][ERROR] artifact serialization failed: " +
                error;
            return result;
        }
        const std::filesystem::path outputPath =
            importedDirectory /
            ("shader-" + HASH::ToHex(fileHash) + ".hshaderc");
        if (!ASSETS::IMPORTERS::INTERNAL::VerifyShaderImportSourceState(
                request,
                compiled.dependencies,
                manifestPath,
                manifestHash,
                error)) {
            result.message =
                "[ShaderImporter][ERROR] source changed before artifact "
                "publication: " + error;
            return result;
        }
        std::error_code existsError{};
        const bool artifactExisted = std::filesystem::is_regular_file(
            outputPath,
            existsError) && !existsError;
        if (!SHADER::WriteHshadercFile(outputPath, artifact, error)) {
            result.message =
                "[ShaderImporter][ERROR] artifact publication failed: " +
                error;
            return result;
        }
        if (!ASSETS::IMPORTERS::INTERNAL::VerifyShaderImportSourceState(
                request,
                compiled.dependencies,
                manifestPath,
                manifestHash,
                error)) {
            if (!artifactExisted) {
                std::error_code removeError{};
                std::filesystem::remove(outputPath, removeError);
            }
            result.message =
                "[ShaderImporter][ERROR] source closure changed before "
                "publication: " + error;
            return result;
        }
        ASSETS::IMPORTERS::INTERNAL::AppendShaderProjectDependencies(
            artifact,
            "Project/" + manifest.sourcePath.generic_string(),
            result);
        result.artifacts.push_back(
            ASSETS::SEMANTICS::MakeAssetArtifact(
                ASSETS::SEMANTICS::AssetArtifactKind::ShaderProgram,
                PROJECT_PATHS::MakeProjectRelativeString(
                    context.projectRoot,
                    outputPath)));
        result.diagnosticsJson = nlohmann::json{
            { "schema", "HIKARI.ShaderImportDiagnostics" },
            { "version", 1u },
            { "programId", artifact.programId },
            { "sourceClosureHash", HASH::ToHex(
                artifact.sourceClosureHash) },
            { "compilerVersion", artifact.compiler.version },
            { "entryCount", artifact.entries.size() },
            { "dependencyCount", artifact.dependencies.size() },
        }.dump(2);
        result.success = true;
        result.message = "[ShaderImporter] cooked HSHADERC: " +
            PROJECT_PATHS::MakeProjectRelativeString(
                context.projectRoot,
                outputPath);
        HIKARI_LOG_INFO(result.message);
        return result;
    }
} // namespace HIKARI
