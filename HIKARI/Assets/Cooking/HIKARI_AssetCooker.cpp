#include "Assets/Cooking/HIKARI_AssetCookInternal.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "Assets/Cooking/HIKARI_CookedContentManifest.h"
#include "RenderContracts/Shader/HIKARI_HshadercFormat.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Shader/Internal/HIKARI_ShaderArtifactValidation.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include <algorithm>
#include <utility>

namespace HIKARI::ASSETS::COOKING {
using namespace INTERNAL;
namespace {

bool IsRuntimeArtifact(const AssetArtifactDesc& artifact) {
    if (artifact.path.empty()) {
        return false;
    }
    if (ASSETS::SEMANTICS::MatchesAssetArtifact(artifact, ASSETS::SEMANTICS::AssetArtifactKind::DebugTextureDds,
                                                true)) {
        return false;
    }

    return !LIGHTING::IsLightingBakeCaptureFile(artifact.path);
}

bool HasArtifactKind(const std::vector<AssetArtifactDesc>& artifacts, ASSETS::SEMANTICS::AssetArtifactKind kind,
                     bool acceptCompatible = true) {
    return std::any_of(artifacts.begin(), artifacts.end(), [kind, acceptCompatible](const AssetArtifactDesc& artifact) {
        return ASSETS::SEMANTICS::MatchesAssetArtifact(artifact, kind, acceptCompatible);
    });
}

bool ValidateShaderArtifactFile(const AssetRecord& record, const std::filesystem::path& root,
                                const std::filesystem::path& relativePath, std::string& outMessage,
                                SHADER::ShaderCookedArtifact* outCooked = nullptr) {
    std::filesystem::path artifactPath{};
    if (!ResolveContainedFilePath(root, relativePath, artifactPath, outMessage)) {
        return false;
    }
    if (!SHADER::INTERNAL::ValidateContentAddressedShaderArtifact(artifactPath, outMessage)) {
        return false;
    }
    SHADER::ShaderCookedArtifact cooked{};
    std::string artifactError{};
    if (!SHADER::ReadHshadercFile(artifactPath, cooked, artifactError) ||
        cooked.sourceGuid.value != record.guid.value) {
        outMessage = "Shader artifact failed strict validation for asset " + record.guid.value + ": " +
                     (artifactError.empty() ? "source GUID mismatch" : artifactError);
        return false;
    }
    if (outCooked != nullptr) {
        *outCooked = std::move(cooked);
    }
    return true;
}

bool ValidateShaderArtifactsForCook(const AssetRecord& record, const std::vector<AssetArtifactDesc>& artifacts,
                                    const std::filesystem::path& projectRoot, std::string& outMessage) {
    if (record.type != AssetType::Shader) {
        return true;
    }
    if (record.importOutdated) {
        outMessage = "Shader asset is outdated and must be reimported: " + record.guid.value;
        return false;
    }
    if (!SHADER::INTERNAL::ValidateShaderAssetRecordAuthority(record, outMessage)) {
        return false;
    }

    const AssetArtifactDesc* primary = nullptr;
    for (const AssetArtifactDesc& artifact : artifacts) {
        const bool namesShaderRole = artifact.role == "ShaderProgram";
        const bool namesShaderFormat =
            artifact.format == "HSHADERC" ||
            ASSETS::SEMANTICS::ClassifyCookedAssetFormat(artifact.path) == CookedAssetFormat::HSHADERC;
        if (!namesShaderRole && !namesShaderFormat) {
            continue;
        }
        if (!ASSETS::SEMANTICS::MatchesAssetArtifact(artifact, ASSETS::SEMANTICS::AssetArtifactKind::ShaderProgram) ||
            ASSETS::SEMANTICS::ClassifyCookedAssetFormat(artifact.path) != CookedAssetFormat::HSHADERC ||
            primary != nullptr) {
            outMessage = "Shader asset must provide exactly one exact "
                         "ShaderProgram/HSHADERC artifact: " +
                         record.guid.value;
            return false;
        }
        primary = &artifact;
    }
    if (primary == nullptr || !PATHS::IsSafeRelativePath(primary->path)) {
        outMessage = "Shader asset is missing a safe HSHADERC artifact: " + record.guid.value;
        return false;
    }
    if (!SHADER::INTERNAL::ValidateShaderAuthoringArtifactOwnership(record, projectRoot, primary->path, outMessage)) {
        return false;
    }

    SHADER::ShaderCookedArtifact cooked{};
    if (!ValidateShaderArtifactFile(record, projectRoot, primary->path, outMessage, &cooked)) {
        return false;
    }
    const CONTENT::EngineContentContext& content = CONTENT::GetEngineContentContext();
    const std::filesystem::path engineShaderRoot =
        content.IsOpen() ? content.Resolve("Shaders") : std::filesystem::path{};
    return SHADER::INTERNAL::ValidateShaderArtifactAgainstAuthoringSources(record, cooked, projectRoot,
                                                                           engineShaderRoot, outMessage);
}

bool RequiresSourcePayload(const AssetRecord& record, const std::vector<AssetArtifactDesc>& artifacts) {
    const auto* primary = SEMANTICS::FindPrimaryAssetArtifactSemantics(record.type);
    return !primary || !HasArtifactKind(artifacts, primary->kind, record.type != AssetType::Shader);
}

bool RequiresPrimaryArtifact(AssetType type) {
    // Material は設定によってソースのまま配布できる。
    return type != AssetType::Material && SEMANTICS::FindPrimaryAssetArtifactSemantics(type) != nullptr;
}

std::filesystem::path MakeRuntimePayloadPath(const std::filesystem::path& sourcePath) {
    return (std::filesystem::path("RuntimePayload") / sourcePath).lexically_normal();
}

} // namespace

AssetCookResult AssetCooker::Cook(const AssetCookOptions& options) const {
    AssetCookResult result{};
    if (options.projectRoot.empty() || options.contentRoot.empty()) {
        result.message = "Cook project root and content root are required.";
        return result;
    }

    std::error_code ec{};
    if (!std::filesystem::is_regular_file(options.projectRoot / kProjectDescriptorFilename, ec) || ec) {
        result.message = "Cook project descriptor is missing: " +
                         (options.projectRoot / kProjectDescriptorFilename).generic_string();
        return result;
    }

    std::filesystem::create_directories(options.contentRoot, ec);
    if (ec) {
        result.message = "Could not create cooked content root: " + options.contentRoot.generic_string();
        return result;
    }

    AssetDatabase assetDatabase{};
    if (!assetDatabase.Initialize(options.projectRoot) || !assetDatabase.ScanAssets(false)) {
        result.message = "Could not scan authored assets for cooking.";
        return result;
    }

    CookPlan plan{};
    if (!BuildCookPlan(options, assetDatabase, plan, result.message)) {
        return result;
    }
    result.missingReferenceCount = plan.missingReferenceCount;
    if (result.missingReferenceCount != 0u) {
        result.message = "Export contains " + std::to_string(result.missingReferenceCount) +
                         " unresolved asset reference(s). Repair references before exporting.";
        return result;
    }

    std::vector<std::string> assetGuids(plan.assetGuids.begin(), plan.assetGuids.end());
    std::sort(assetGuids.begin(), assetGuids.end());

    std::unordered_map<std::string, std::filesystem::path> cookedSourceByOriginalPath{};
    std::unordered_map<std::string, std::vector<AssetArtifactDesc>> runtimeArtifactsByGuid{};

    for (const std::string& guid : assetGuids) {
        if (options.progress && !options.progress("Validating asset " + guid, 0.0f)) {
            result.message = "Export cancelled. The previous package was preserved.";
            return result;
        }
        const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{guid});
        if (record == nullptr || record->duplicateGuid) {
            result.message = "Cook asset record is invalid or duplicated: " + guid;
            return result;
        }
        if (!PATHS::IsSafeRelativePath(record->sourcePath)) {
            result.message = "Authored asset source path is unsafe: " + record->sourcePath.generic_string();
            return result;
        }

        if (!record->sourceExists ||
            ((RequiresPrimaryArtifact(record->type) || record->type == AssetType::Font ||
              record->type == AssetType::TextStyle || record->type == AssetType::PostEffect ||
              record->type == AssetType::PostProfile || record->type == AssetType::Environment ||
              record->type == AssetType::Weather || record->type == AssetType::RenderStyle ||
              record->type == AssetType::ColorLut || record->type == AssetType::LightProfile) &&
             (record->importOutdated || record->artifactMissing || !record->lastImportSucceeded))) {
            result.message = "Asset needs a successful, current import: " + TEXT::PathToUtf8(record->sourcePath) +
                             " (" + guid + "). " + record->lastImportMessage;
            return result;
        }

        std::vector<AssetArtifactDesc> runtimeArtifacts{};
        for (const AssetArtifactDesc& artifact : record->artifactManifest.artifacts) {
            if (IsRuntimeArtifact(artifact)) {
                runtimeArtifacts.push_back(artifact);
            }
        }
        if (!ValidateShaderArtifactsForCook(*record, runtimeArtifacts, options.projectRoot, result.message)) {
            return result;
        }
        if (RequiresPrimaryArtifact(record->type) && RequiresSourcePayload(*record, runtimeArtifacts)) {
            result.message = "Required cooked artifact is missing for asset " + guid + " (" +
                             record->sourcePath.generic_string() + "). Reimport the asset before exporting.";
            return result;
        }

        const bool sourcePayload = RequiresSourcePayload(*record, runtimeArtifacts);
        std::filesystem::path cookedSourcePath =
            sourcePayload ? MakeRuntimePayloadPath(record->sourcePath) : record->sourcePath;
        if (record->type == AssetType::Sprite || record->type == AssetType::SpriteMaterial) {
            const SEMANTICS::AssetArtifactSemantics* primary =
                SEMANTICS::FindPrimaryAssetArtifactSemantics(record->type);
            const std::string runtimeArtifact =
                primary == nullptr ? std::string{} : SEMANTICS::FindAssetArtifactPath(*record, primary->kind);
            if (runtimeArtifact.empty()) {
                result.message = "Normalized runtime artifact is missing: " + guid;
                return result;
            }
            cookedSourcePath = TEXT::PathFromUtf8(runtimeArtifact);
        }
        cookedSourceByOriginalPath[MakePathKey(record->sourcePath)] = std::move(cookedSourcePath);
        runtimeArtifactsByGuid.emplace(guid, std::move(runtimeArtifacts));
    }

    CookedContentManifest manifest{};
    manifest.startupSceneGuid = options.startupSceneGuid;
    manifest.exportedSceneGuids = options.sceneGuids;
    manifest.assets.reserve(assetGuids.size());

    for (const std::string& guid : assetGuids) {
        if (options.progress &&
            !options.progress("Copying asset " + guid, 0.1f + 0.8f * static_cast<float>(manifest.assets.size()) /
                                                                  (std::max)(size_t{1}, assetGuids.size()))) {
            result.message = "Export cancelled. The previous package was preserved.";
            return result;
        }
        const AssetRecord* sourceRecord = assetDatabase.FindByGuid(AssetGuid{guid});
        if (sourceRecord == nullptr) {
            result.message = "Cook asset disappeared: " + guid;
            return result;
        }

        AssetRecord cookedRecord = *sourceRecord;
        cookedRecord.metaPath.clear();
        cookedRecord.artifactManifestPath.clear();
        cookedRecord.sourcePath = cookedSourceByOriginalPath.at(MakePathKey(sourceRecord->sourcePath));
        cookedRecord.meta.sourcePath = cookedRecord.sourcePath.generic_string();
        cookedRecord.artifactManifest.sourcePath = cookedRecord.sourcePath.generic_string();
        cookedRecord.artifactManifest.artifacts = runtimeArtifactsByGuid.at(guid);

        std::vector<AssetDependencyDesc> dependencies{};
        for (AssetDependencyDesc dependency : CookDependencies(*sourceRecord, plan)) {
            if (ASSETS::SEMANTICS::IsSourceOnlyAssetDependencyRole(dependency.role)) {
                continue;
            }

            if (dependency.role == "Sequence.AudioCue") {
                dependency.path = MakeRuntimePayloadPath(dependency.path).generic_string();
                dependencies.push_back(std::move(dependency));
                continue;
            }
            const AssetRecord* dependencyRecord = nullptr;
            if (dependency.guid.IsValid()) {
                dependencyRecord = assetDatabase.FindByGuid(dependency.guid);
            } else if (!dependency.path.empty()) {
                dependencyRecord = assetDatabase.FindByPath(dependency.path);
            }
            if (dependencyRecord == nullptr || !plan.assetGuids.contains(dependencyRecord->guid.value)) {
                continue;
            }

            dependency.guid = dependencyRecord->guid;
            const auto cookedPath = cookedSourceByOriginalPath.find(MakePathKey(dependencyRecord->sourcePath));
            dependency.path =
                cookedPath == cookedSourceByOriginalPath.end() ? std::string{} : cookedPath->second.generic_string();
            dependencies.push_back(std::move(dependency));
        }
        cookedRecord.artifactManifest.dependencies = std::move(dependencies);

        if (RequiresSourcePayload(*sourceRecord, runtimeArtifactsByGuid.at(guid))) {
            if (!CopyCookPayloadFile(options.projectRoot / sourceRecord->sourcePath,
                                     options.contentRoot / cookedRecord.sourcePath, result.fileCount, result.message)) {
                return result;
            }
            // 分区場景の格子ファイル（取り込みが付属ファイルとして記録したもの）も場景の隣の同じ相対位置へ配る。
            if (sourceRecord->type == AssetType::Scene) {
                for (const AssetDependencyDesc& dependency : sourceRecord->artifactManifest.dependencies) {
                    const std::filesystem::path companion = TEXT::PathFromUtf8(dependency.path);
                    if (dependency.role == "SourceCompanion" && !dependency.guid.IsValid() &&
                        PATHS::IsSafeRelativePath(companion) &&
                        !CopyCookPayloadFile(options.projectRoot / companion,
                                             options.contentRoot / MakeRuntimePayloadPath(companion), result.fileCount,
                                             result.message)) {
                        return result;
                    }
                }
            }
        }

        for (const AssetArtifactDesc& artifact : cookedRecord.artifactManifest.artifacts) {
            const bool isShaderProgram =
                sourceRecord->type == AssetType::Shader &&
                ASSETS::SEMANTICS::MatchesAssetArtifact(artifact, ASSETS::SEMANTICS::AssetArtifactKind::ShaderProgram);
            if (!PATHS::IsSafeRelativePath(artifact.path)) {
                result.message = "Cooked artifact path is unsafe: " + artifact.path;
                return result;
            }
            std::filesystem::path source = options.projectRoot / artifact.path;
            std::filesystem::path destination = options.contentRoot / artifact.path;
            if (isShaderProgram &&
                (!ResolveContainedFilePath(options.projectRoot, artifact.path, source, result.message) ||
                 !ResolveContainedFilePath(options.contentRoot, artifact.path, destination, result.message))) {
                return result;
            }
            if (!CopyCookPayloadFile(source, destination, result.fileCount, result.message)) {
                return result;
            }
            if (isShaderProgram &&
                !ValidateShaderArtifactFile(*sourceRecord, options.contentRoot, artifact.path, result.message)) {
                return result;
            }
        }

        cookedRecord.sourceExists = true;
        cookedRecord.metaExists = true;
        cookedRecord.importOutdated = false;
        cookedRecord.lastImportSucceeded = true;
        cookedRecord.duplicateGuid = false;
        cookedRecord.importerMissing = false;
        cookedRecord.artifactMissing = false;
        cookedRecord.lastImportMessage = "Published by HIKARI AssetCooker";
        manifest.assets.push_back(std::move(cookedRecord));
    }

    if (options.progress && !options.progress("Copying runtime settings and payloads", 0.92f)) {
        result.message = "Export cancelled. The previous package was preserved.";
        return result;
    }

    if (!CopyCookPayloadFile(options.projectRoot / kProjectDescriptorFilename,
                             options.contentRoot / kProjectDescriptorFilename, result.fileCount, result.message) ||
        !CopyRuntimeSettingsAndData(options, result.fileCount, result.message) ||
        !CopyDeclaredRuntimePayloads(options, result.fileCount, result.message) ||
        !CopyLightingContent(options, plan, result.fileCount, result.message) ||
        !CookProceduralGeometry(options, plan, assetDatabase, result.fileCount, result.message) ||
        !CopyOpeningAnimation(options.projectRoot, options.contentRoot, result.fileCount, result.message)) {
        return result;
    }

    if (!WriteCookedContentManifest(options.contentRoot / kCookedContentManifestFilename, manifest, &result.message)) {
        return result;
    }
    ++result.fileCount;

    result.assetCount = manifest.assets.size();
    result.success = true;
    result.message = "Cooked " + std::to_string(result.assetCount) + " asset(s) and published " +
                     std::to_string(result.fileCount) + " file(s).";
    if (result.missingReferenceCount > 0u) {
        result.message += " Missing optional/reference entries: " + std::to_string(result.missingReferenceCount) + ".";
    }
    HIKARI_LOG_INFO("[AssetCooker] " + result.message);
    return result;
}

} // namespace HIKARI::ASSETS::COOKING
