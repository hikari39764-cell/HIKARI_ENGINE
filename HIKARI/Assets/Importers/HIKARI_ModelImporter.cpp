#include "Assets/Importers/HIKARI_ModelImportInternal.h"
#include "HIKARI_ModelImporter.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/Collision/HIKARI_ModelCollisionArtifact.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportInputs.h"
#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "Assets/Geometry/HIKARI_ClusteredGeometryValidator.h"
#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Assets/Importers/Policy/HIKARI_ModelImportPolicy.h"
#include "Assets/Lighting/HIKARI_LightmapUvGeneration.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/Loading/HIKARI_ModelSourceLoader.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Tasks/HIKARI_AssetTaskService.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "HIKARI_TextureImportBackend_DirectXTex.h"

namespace HIKARI {

using namespace MODEL_IMPORT_INTERNAL;

ASSETS::SEMANTICS::AssetImporterKind ModelImporter::GetImporterKind() const noexcept {
    return ASSETS::SEMANTICS::AssetImporterKind::Model;
}

AssetMeta ModelImporter::CreateDefaultMeta(const std::filesystem::path& sourcePath, const AssetGuid& guid) const {
    AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(sourcePath, guid, GetImporterKind());
    meta.importSettingsJson = ASSETS::IMPORT_POLICY::MakeDefaultModelImportSettingsJson(sourcePath);
    return meta;
}

AssetImportResult ModelImporter::Import(const AssetRecord& record, const AssetImportContext& context) {
    AssetImportResult result{};
    const std::string taskItem = record.sourcePath.filename().string();
    const auto reportStage = [&](const char* stage, float normalized, bool determinate = true) {
        if (context.task != nullptr) {
            context.task->ReportStage(stage, normalized, determinate, taskItem);
        }
    };
    const auto canceled = [&]() { return context.task != nullptr && context.task->IsCancellationRequested(); };
    if (!ASSETS::SEMANTICS::IsAssetSourceForImporter(record.sourcePath, GetImporterKind())) {
        const auto& semantics = GetSemantics();
        result.message = "[AssetImporter] HMODEL cook supports " + std::string(semantics.supportedSources) +
                         " only. source=" + record.sourcePath.generic_string();
        HIKARI_LOG_ERROR(result.message);
        return result;
    }

    const ASSETS::IMPORT_POLICY::ModelImportPolicy importPolicy =
        ASSETS::IMPORT_POLICY::ResolveModelImportPolicy(record.sourcePath, record.meta.importSettingsJson);
    const ModelGeometryCookProfile clusterProfile = importPolicy.clusterOptions.profile;
    const bool buildClusterGeometry = importPolicy.clusterOptions.buildClusterGeometry;
    const ASSETS::GEOMETRY::ClusteredGeometryCookSettings& clusterSettings = importPolicy.clusteredGeometryCookSettings;
    const std::filesystem::path absoluteSource =
        PROJECT_PATHS::ResolveProjectPath(context.projectRoot, record.sourcePath);

    reportStage("Parsing model source", 0.08f, false);
    if (canceled()) {
        result.message = "[AssetImporter] model import canceled";
        return result;
    }
    ModelAsset model{};
    model.SetName(record.guid.value);
    model.SetSourcePath(absoluteSource.generic_string());
    if (!ASSETS::MODELS::LoadModelSource(model, &result.inputs.files)) {
        result.message =
            "[AssetImporter] failed to read model source for HMODEL cook. source=" + record.sourcePath.generic_string();
        HIKARI_LOG_ERROR(result.message);
        return result;
    }

    model.SetName(record.guid.value);
    model.SetSourcePath(record.sourcePath.generic_string());
    for (const auto& texture : model.textures) {
        if (!texture.sourcePath.empty())
            result.inputs.files.push_back(CaptureAssetInputFile(
                PROJECT_PATHS::ResolveProjectPath(context.projectRoot, TEXT::PathFromUtf8(texture.sourcePath))));
    }
    // ローダーが実際に開いた BIN/MTL と packing 前の画像を依存として保存する。
    std::unordered_set<std::string> dependencyPaths;
    for (auto& input : result.inputs.files) {
        const auto path = PROJECT_PATHS::MakeProjectRelativePath(context.projectRoot, TEXT::PathFromUtf8(input.path));
        input.path = TEXT::PathToUtf8(path);
        if (!dependencyPaths.insert(input.path).second)
            continue;
        AssetDependencyDesc dependency;
        dependency.path = input.path;
        const auto* referenced = context.assetDatabase ? context.assetDatabase->FindByPath(path) : nullptr;
        if (referenced) {
            dependency.guid = referenced->guid;
            dependency.role = "Texture";
        } else {
            dependency.role = "SourceCompanion";
        }
        result.dependencies.push_back(std::move(dependency));
    }

    BuildCanonicalMetallicRoughnessTextures(model, context, result.artifacts);

    int htexReferenceCount = 0;
    int fallbackTextureCount = 0;
    std::vector<TextureCookDiagnostic> textureDiagnostics;
    textureDiagnostics.reserve(model.textures.size());
    for (size_t i = 0; i < model.textures.size(); ++i) {
        if (canceled()) {
            result.message = "[AssetImporter] model import canceled";
            return result;
        }
        reportStage("Resolving model texture dependencies",
                    0.18f + 0.14f * (model.textures.empty()
                                         ? 1.0f
                                         : static_cast<float>(i) / static_cast<float>(model.textures.size())));
        TextureAsset3D& texture = model.textures[i];
        TextureCookDiagnostic diagnostic{};
        diagnostic.index = static_cast<int>(i);
        diagnostic.name = texture.name;
        diagnostic.originalPath = texture.sourcePath;

        AssetDependencyDesc dependency{};
        bool hasDependency = false;
        if (ResolveTextureToHtex(context.projectRoot, context.sourceMetaRoot, context.libraryRoot, texture, dependency,
                                 hasDependency, diagnostic)) {
            ++htexReferenceCount;
            diagnostic.htexReady = true;
        } else if (!texture.sourcePath.empty()) {
            ++fallbackTextureCount;
            InspectSourceTextureAlpha(PROJECT_PATHS::ResolveProjectPath(context.projectRoot, texture.sourcePath),
                                      diagnostic);
        }

        if (hasDependency && dependencyPaths.insert(dependency.path).second) {
            result.dependencies.push_back(std::move(dependency));
        }
        diagnostic.cookedPath = texture.sourcePath;
        textureDiagnostics.push_back(std::move(diagnostic));
    }

    // 古い texture cook を参照した model を最新として公開しない。
    if (context.assetDatabase) {
        for (const auto& dependency : result.dependencies) {
            const auto* referenced = context.assetDatabase->FindByGuid(dependency.guid);
            if (!referenced || referenced->type != AssetType::Texture || referenced->artifactManifest.artifacts.empty())
                continue;
            std::string reason;
            AssetMeta persisted;
            const bool current =
                context.assetDatabase->ReadMeta(referenced->metaPath, persisted) &&
                EquivalentAssetImportMeta(persisted, referenced->meta) &&
                referenced->artifactManifest.inputs.settings == CanonicalImportSettings(persisted.importSettingsJson) &&
                CheckAssetImportInputs(context.projectRoot, referenced->artifactManifest.inputs, reason);
            if (!current) {
                result.message = "Reimport the texture dependency first: " + referenced->sourcePath.generic_string();
                return result;
            }
        }
    }

    const MaterialAlphaPolicyStats alphaPolicyStats =
        ApplyCanonicalBaseColorAlphaMaterialPolicy(model, textureDiagnostics);
    ApplyCanonicalSpecularPolicy(model);

    reportStage("Preparing lightmap UVs", 0.34f, false);
    ASSETS::LIGHTING::LightmapUvGenerationReport lightmapUvReport{};
    ASSETS::LIGHTING::LightmapUvGenerationCallbacks lightmapUvCallbacks{};
    lightmapUvCallbacks.isCancellationRequested = canceled;
    lightmapUvCallbacks.reportProgress = [&](uint32_t currentPrimitive, uint32_t primitiveCount,
                                             std::string_view primitiveName) {
        if (context.task == nullptr) {
            return;
        }

        AssetTaskProgress progress{};
        progress.stage = "Preparing lightmap UVs";
        progress.currentItem = taskItem;
        if (!primitiveName.empty()) {
            progress.currentItem += " :: " + std::string(primitiveName);
        }
        progress.determinate = primitiveCount > 0u;
        progress.completedUnits = currentPrimitive;
        progress.totalUnits = primitiveCount;
        const float uvProgress =
            primitiveCount > 0u ? static_cast<float>(currentPrimitive) / static_cast<float>(primitiveCount) : 1.0f;
        progress.normalized = 0.34f + uvProgress * 0.02f;
        context.task->Report(std::move(progress));
    };
    ASSETS::LIGHTING::PrepareModelLightmapUvs(model, importPolicy.lightmapUv, lightmapUvReport, &lightmapUvCallbacks);
    if (lightmapUvReport.canceled || canceled()) {
        result.message = "[AssetImporter] model import canceled during lightmap UV generation";
        return result;
    }
    model.importDiagnostics.lightmapUvAuthoredValidCount = lightmapUvReport.authoredValidCount;
    model.importDiagnostics.lightmapUvMissingCount = lightmapUvReport.missingCount;
    model.importDiagnostics.lightmapUvInvalidCount = lightmapUvReport.invalidCount;
    model.importDiagnostics.lightmapUvGeneratedCount = lightmapUvReport.generatedCount;
    model.importDiagnostics.lightmapUvGenerationFailedCount = lightmapUvReport.generationFailedCount;
    model.importDiagnostics.messages.push_back(
        "[LightmapUV] mode=" + std::string(ASSETS::LIGHTING::ToString(importPolicy.lightmapUv.mode)) +
        " authoredValid=" + std::to_string(lightmapUvReport.authoredValidCount) + " missing=" +
        std::to_string(lightmapUvReport.missingCount) + " invalid=" + std::to_string(lightmapUvReport.invalidCount) +
        " generated=" + std::to_string(lightmapUvReport.generatedCount) +
        " failed=" + std::to_string(lightmapUvReport.generationFailedCount));
    for (const std::string& message : lightmapUvReport.messages) {
        model.importDiagnostics.messages.push_back("[LightmapUV] " + message);
    }

    const std::filesystem::path finalPath = context.importedDirectory / "model.hmodel";
    const std::filesystem::path tempPath =
        context.publication ? context.StageFile(finalPath) : context.importedDirectory / "model.importing.hmodel";

    reportStage("Writing HMODEL", 0.36f);
    std::error_code removeEc{};
    std::filesystem::remove(tempPath, removeEc);
    if (removeEc) {
        result.message = "[AssetImporter] failed to clear stale temporary HMODEL: " + tempPath.generic_string();
        HIKARI_LOG_ERROR(result.message);
        return result;
    }

    std::string hmodelMessage{};
    if (!WriteHmodelFile(tempPath, model, hmodelMessage)) {
        std::error_code cleanupEc{};
        std::filesystem::remove(tempPath, cleanupEc);
        result.message = hmodelMessage.empty() ? "[AssetImporter] HMODEL write failed" : hmodelMessage;
        HIKARI_LOG_ERROR(result.message);
        return result;
    }

    std::string hmodelCommitMessage{};
    if (!context.publication && !IO::CommitStagedFile(tempPath, finalPath, hmodelCommitMessage)) {
        std::error_code cleanupEc{};
        std::filesystem::remove(tempPath, cleanupEc);
        result.message = "[AssetImporter] failed to replace HMODEL artifact. temp=" + tempPath.generic_string() +
                         " final=" + finalPath.generic_string() + " reason=" + hmodelCommitMessage;
        HIKARI_LOG_ERROR(result.message);
        return result;
    }

    bool hcmeshReady = false;
    std::string hcmeshMessage{};
    RENDER3D::CLUSTER::ClusteredGeometryBuildReport clusteredReport{};
    ASSETS::GEOMETRY::ClusteredGeometryValidationResult clusteredValidation{};
    const RENDER3D::CLUSTER::ClusteredGeometryBuildReport* clusteredReportPtr = nullptr;
    const ASSETS::GEOMETRY::ClusteredGeometryValidationResult* clusteredValidationPtr = nullptr;

    if (!buildClusterGeometry) {
        hcmeshMessage = "[AssetImporter] HCMESH cook disabled by model import settings";
    } else {
        if (canceled()) {
            result.message = "[AssetImporter] model import canceled";
            return result;
        }
        reportStage("Cooking clustered geometry", 0.52f, false);
        RENDER3D::CLUSTER::ClusteredGeometryAsset clusteredGeometry{};
        clusteredReportPtr = &clusteredReport;
        if (ASSETS::GEOMETRY::CookClusteredGeometryFromModel(model, record.guid, clusterSettings, clusteredGeometry,
                                                             clusteredReport)) {
            clusteredValidation = ASSETS::GEOMETRY::ValidateClusteredGeometryAsset(clusteredGeometry);
            clusteredValidationPtr = &clusteredValidation;
            if (clusteredValidation.valid) {
                reportStage("Writing HCMESH", 0.76f);
                const std::filesystem::path finalHcmeshPath = context.importedDirectory / "clustered_mesh.hcmesh";
                const std::filesystem::path tempHcmeshPath =
                    context.publication ? context.StageFile(finalHcmeshPath)
                                        : context.importedDirectory / "clustered_mesh.importing.hcmesh";
                std::error_code removeHcmeshEc{};
                std::filesystem::remove(tempHcmeshPath, removeHcmeshEc);
                if (removeHcmeshEc) {
                    hcmeshMessage =
                        "[AssetImporter] failed to clear stale temporary HCMESH: " + tempHcmeshPath.generic_string();
                } else if (ASSETS::GEOMETRY::WriteHcmeshFile(tempHcmeshPath, clusteredGeometry, hcmeshMessage)) {
                    std::string commitMessage{};
                    if (context.publication || IO::CommitStagedFile(tempHcmeshPath, finalHcmeshPath, commitMessage)) {
                        hcmeshReady = true;
                        result.artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
                            ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry,
                            PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, finalHcmeshPath)));
                    } else {
                        std::error_code cleanupEc{};
                        std::filesystem::remove(tempHcmeshPath, cleanupEc);
                        hcmeshMessage = "[AssetImporter] failed to replace HCMESH "
                                        "artifact. temp=" +
                                        tempHcmeshPath.generic_string() + " final=" + finalHcmeshPath.generic_string() +
                                        " reason=" + commitMessage;
                    }
                }
            } else {
                hcmeshMessage = clusteredValidation.messages.empty() ? "[AssetImporter] HCMESH validation failed"
                                                                     : clusteredValidation.messages.front();
            }
        } else {
            hcmeshMessage = clusteredReport.messages.empty()
                                ? "[AssetImporter] HCMESH cook produced no clusterable primitive"
                                : clusteredReport.messages.front();
        }
    }

    if (!hcmeshReady && context.publication) {
        context.publication->DiscardFile(context.importedDirectory / "clustered_mesh.hcmesh");
    }
    if (canceled()) {
        result.message = "[AssetImporter] model import canceled";
        return result;
    }
    reportStage("Compiling model collision", 0.86f, false);
    const ASSETS::COLLISION::ModelCollisionArtifactResult collisionArtifact =
        ASSETS::COLLISION::BuildModelCollisionArtifact(
            record, context.projectRoot,
            context.publication ? context.publication->StageDirectory(context.importedDirectory)
                                : context.importedDirectory);
    const bool hcollisionReady = collisionArtifact.ready;
    if (!collisionArtifact.success) {
        result.message = "[AssetImporter] model collision setup compile failed: " + collisionArtifact.message;
        HIKARI_LOG_ERROR(result.message);
        return result;
    }
    if (hcollisionReady) {
        const auto collisionPath = context.importedDirectory / collisionArtifact.path.filename();
        if (context.publication)
            context.StageFile(collisionPath);
        result.artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
            ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry,
            PROJECT_PATHS::MakeProjectRelativePath(context.projectRoot, collisionPath).generic_string()));
    }
    if (collisionArtifact.removeExisting && context.publication) {
        context.publication->DeleteOnPublish(context.importedDirectory / "collision_geometry.hcollision");
    }

    result.success = true;
    reportStage("Building model import report", 0.96f);
    result.message =
        "[AssetImporter] Wrote HMODEL meshes=" + std::to_string(model.meshes.size()) +
        " materials=" + std::to_string(model.materials.size()) + " textures=" + std::to_string(model.textures.size()) +
        " htexRefs=" + std::to_string(htexReferenceCount) +
        " fallbackTextures=" + std::to_string(fallbackTextureCount) +
        " hcmesh=" + (hcmeshReady ? "ready" : "fallback") + " hcollision=" + (hcollisionReady ? "ready" : "none");
    nlohmann::json diagnostics = BuildModelDiagnostics(
        model, textureDiagnostics, htexReferenceCount, fallbackTextureCount, clusteredReportPtr, clusteredValidationPtr,
        clusterProfile, &clusterSettings, hcmeshReady, hcmeshMessage, alphaPolicyStats);
    diagnostics["collisionGeometry"] = {
        {"format", "HCOLLISION"},
        {"ready", hcollisionReady},
        {"shapeCount", collisionArtifact.shapeCount},
        {"message", collisionArtifact.message},
        {"authoring", "Model Collision Workspace"},
    };
    result.diagnosticsJson = diagnostics.dump(2);
    result.artifacts.push_back(
        ASSETS::SEMANTICS::MakeAssetArtifact(ASSETS::SEMANTICS::AssetArtifactKind::MainModel,
                                             PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, finalPath)));

    HIKARI_LOG_INFO(result.message + " source=" + record.sourcePath.generic_string() + " guid=" + record.guid.value);
    return result;
}

} // namespace HIKARI
