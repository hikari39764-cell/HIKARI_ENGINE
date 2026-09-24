#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Assets/Importers/Policy/HIKARI_MaterialImportPolicy.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace HIKARI {
ClusteredGeometryArtifactState AssetDatabase::GetClusteredGeometryArtifactState(const AssetRecord& record) const {
    return GetClusteredGeometryArtifactInfo(record).state;
}

ClusteredGeometryArtifactState AssetDatabase::GetClusteredGeometryArtifactState(const AssetGuid& guid) const {
    return GetClusteredGeometryArtifactInfo(guid).state;
}

ClusteredGeometryArtifactInfo AssetDatabase::GetClusteredGeometryArtifactInfo(const AssetRecord& record) const {
    ClusteredGeometryArtifactInfo info{};
    const AssetArtifactDesc* artifact =
        ASSETS::SEMANTICS::FindAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry);
    if (artifact == nullptr) {
        info.state = ClusteredGeometryArtifactState::Missing;
        info.message = "HCMESH artifact is not recorded in artifact manifest";
        return info;
    }

    const std::filesystem::path hcmeshPath =
        PROJECT_PATHS::ResolveProjectPath(projectRoot_, TEXT::PathFromUtf8(artifact->path));
    info.path = hcmeshPath;
    std::error_code ec{};
    if (!std::filesystem::exists(hcmeshPath, ec) || ec) {
        info.state = ClusteredGeometryArtifactState::Missing;
        info.message = "HCMESH artifact file is missing";
        return info;
    }

    ASSETS::GEOMETRY::HcmeshFileInfo hcmeshInfo{};
    std::string message{};
    if (!ASSETS::GEOMETRY::InspectHcmeshFile(hcmeshPath, hcmeshInfo, message)) {
        info.state = ClusteredGeometryArtifactState::Invalid;
        info.message = message.empty() ? "HCMESH read failed" : message;
        return info;
    }

    info.validationValid = hcmeshInfo.valid;
    if (!hcmeshInfo.valid || hcmeshInfo.surfaceCount == 0u || hcmeshInfo.clusterCount == 0u ||
        hcmeshInfo.vertexCount == 0u || hcmeshInfo.meshletPrimitiveCount == 0u || hcmeshInfo.geometryByteSize == 0u ||
        hcmeshInfo.metadataByteSize == 0u) {
        info.state = ClusteredGeometryArtifactState::Invalid;
        info.message = "HCMESH packed artifact is empty or incomplete";
        return info;
    }

    bool sourceNewerThanArtifact = false;
    if (record.sourceExists) {
        const std::filesystem::path sourcePath = PROJECT_PATHS::ResolveProjectPath(projectRoot_, record.sourcePath);
        const auto sourceTime = std::filesystem::last_write_time(sourcePath, ec);
        if (!ec) {
            const auto hcmeshTime = std::filesystem::last_write_time(hcmeshPath, ec);
            sourceNewerThanArtifact = !ec && sourceTime > hcmeshTime;
        }
    }
    if (sourceNewerThanArtifact || record.importOutdated) {
        info.state = ClusteredGeometryArtifactState::Outdated;
        info.message = sourceNewerThanArtifact ? "Source model is newer than HCMESH"
                                               : "Importer version or artifact state is outdated";
        return info;
    }

    info.state = ClusteredGeometryArtifactState::Valid;
    info.message = message.empty() ? "HCMESH valid" : message;
    return info;
}

ClusteredGeometryArtifactInfo AssetDatabase::GetClusteredGeometryArtifactInfo(const AssetGuid& guid) const {
    const AssetRecord* record = FindByGuid(guid);
    if (record == nullptr) {
        ClusteredGeometryArtifactInfo info{};
        info.state = ClusteredGeometryArtifactState::Missing;
        info.message = "Asset record is missing";
        return info;
    }
    return GetClusteredGeometryArtifactInfo(*record);
}

AssetRecord AssetDatabase::BuildRecordForSource(const std::filesystem::path& sourcePath, bool createMissingMeta) {
    const std::filesystem::path normalizedSource = NormalizeProjectPath(sourcePath);
    const std::filesystem::path absoluteSource = PROJECT_PATHS::ResolveProjectPath(projectRoot_, normalizedSource);

    AssetRecord record{};
    record.sourcePath = normalizedSource;
    record.metaPath = GetMetaPathForSource(normalizedSource);
    record.sourceExists = std::filesystem::exists(absoluteSource);
    record.metaExists = std::filesystem::exists(record.metaPath);
    record.type = GuessAssetTypeFromPath(normalizedSource);
    record.displayName = TEXT::PathToUtf8(normalizedSource.stem());
    bool metadataChanged = false;

    if (record.metaExists) {
        AssetMeta meta{};
        if (ReadMeta(record.metaPath, meta)) {
            record.meta = std::move(meta);
            record.guid = record.meta.guid;
            record.type = record.meta.type == AssetType::Unknown ? record.type : record.meta.type;
            record.displayName = record.meta.displayName.empty() ? record.displayName : record.meta.displayName;
            if (record.meta.sourcePath.empty() ||
                NormalizeProjectPath(TEXT::PathFromUtf8(record.meta.sourcePath)) != normalizedSource) {
                record.meta.sourcePath = TEXT::PathToUtf8(normalizedSource);
                metadataChanged = true;
            }
        } else {
            record.meta = AssetMeta{};
            record.meta.sourcePath = TEXT::PathToUtf8(normalizedSource);
            record.meta.displayName = record.displayName;
            record.meta.type = record.type;
            record.lastImportMessage = "[AssetDatabase] meta JSON parse failed";
        }
    } else if (createMissingMeta) {
        AssetGuid guid = GenerateAssetGuid();
        while (recordsByGuid_.find(guid.value) != recordsByGuid_.end()) {
            guid = GenerateAssetGuid();
        }

        const std::string importerId = SelectDefaultImporterId(record.type, normalizedSource);
        if (const IAssetImporter* importer = importerRegistry_.FindById(importerId)) {
            record.meta = importer->CreateDefaultMeta(normalizedSource, guid);
        } else {
            record.meta = AssetMeta{};
            record.meta.guid = guid;
            record.meta.type = record.type;
            record.meta.importerId = importerId;
            record.meta.sourcePath = TEXT::PathToUtf8(normalizedSource);
            record.meta.displayName = record.displayName;
            record.meta.importSettingsJson = "{}";
        }
        record.guid = record.meta.guid;
        record.displayName = record.meta.displayName;
        metadataChanged = true;
    } else {
        record.meta = AssetMeta{};
        record.meta.type = record.type;
        record.meta.sourcePath = TEXT::PathToUtf8(normalizedSource);
        record.meta.displayName = record.displayName;
    }

    const ASSETS::SEMANTICS::AssetImporterSemantics* sourceSemantics =
        ASSETS::SEMANTICS::ResolveAssetSourceSemantics(normalizedSource);
    const bool pathOwnsShader =
        sourceSemantics != nullptr && sourceSemantics->kind == ASSETS::SEMANTICS::AssetImporterKind::Shader;
    const bool metaClaimsShader = record.meta.type == AssetType::Shader || record.meta.importerId == "ShaderImporter";
    if (pathOwnsShader) {
        const bool mismatched =
            record.meta.type != sourceSemantics->assetType || record.meta.importerId != sourceSemantics->importerId;
        record.type = sourceSemantics->assetType;
        record.meta.type = sourceSemantics->assetType;
        record.meta.importerId = sourceSemantics->importerId;
        if (mismatched) {
            record.meta.importerVersion = 0u;
            record.lastImportMessage = "[AssetDatabase] Shader source ownership repaired";
            metadataChanged = true;
        }
    } else if (metaClaimsShader) {
        record.type = GuessAssetTypeFromPath(normalizedSource);
        record.meta.type = record.type;
        record.meta.importerId.clear();
        record.meta.importerVersion = 0u;
        record.lastImportMessage = "[AssetDatabase] Shader importer rejected outside "
                                   "Assets/Shaders/*.hshader";
        metadataChanged = true;
    }

    // 全ての補正を一度だけ保存し、失敗時は ScanAssets の snapshot を戻す。
    if (createMissingMeta && metadataChanged && !WriteMeta(record)) {
        throw std::runtime_error("Failed to persist asset metadata: " + TEXT::PathToUtf8(record.metaPath));
    }

    if (!record.guid.IsValid() && record.meta.guid.IsValid()) {
        record.guid = record.meta.guid;
    }
    if (record.guid.IsValid() && !IsValidAssetGuid(record.guid.value)) {
        record.lastImportMessage = "[AssetDatabase] invalid GUID format: " + record.guid.value;
    }
    if (record.guid.IsValid()) {
        record.importedDirectory = GetImportedDirectory(record.guid);
        record.artifactManifestPath = GetArtifactManifestPath(record.guid);
        if (std::filesystem::exists(record.artifactManifestPath)) {
            ReadArtifactManifest(record.artifactManifestPath, record.artifactManifest);
        }
    }

    RefreshRecordState(record);
    return record;
}

void AssetDatabase::RefreshRecordState(AssetRecord& record) const {
    const std::filesystem::path absoluteSource = PROJECT_PATHS::ResolveProjectPath(projectRoot_, record.sourcePath);
    record.sourceExists = std::filesystem::exists(absoluteSource);
    record.metaExists = std::filesystem::exists(record.metaPath);
    record.importerMissing =
        !record.meta.importerId.empty() && importerRegistry_.FindById(record.meta.importerId) == nullptr;

    if (record.guid.IsValid()) {
        record.importedDirectory = GetImportedDirectory(record.guid);
        record.artifactManifestPath = GetArtifactManifestPath(record.guid);
    }

    bool artifactMissing = false;
    std::filesystem::file_time_type oldestArtifactTime{};
    bool hasArtifactTime = false;
    for (const AssetArtifactDesc& artifact : record.artifactManifest.artifacts) {
        const std::filesystem::path artifactPath =
            PROJECT_PATHS::ResolveProjectPath(projectRoot_, TEXT::PathFromUtf8(artifact.path));
        if (!std::filesystem::exists(artifactPath)) {
            artifactMissing = true;
            continue;
        }

        std::error_code ec{};
        const auto writeTime = std::filesystem::last_write_time(artifactPath, ec);
        if (!ec && (!hasArtifactTime || writeTime < oldestArtifactTime)) {
            oldestArtifactTime = writeTime;
            hasArtifactTime = true;
        }
    }
    record.artifactMissing = artifactMissing;

    bool sourceNewerThanArtifact = false;
    if (record.sourceExists && hasArtifactTime) {
        std::error_code ec{};
        const auto sourceTime = std::filesystem::last_write_time(absoluteSource, ec);
        sourceNewerThanArtifact = !ec && sourceTime > oldestArtifactTime;
    }
    bool sourceDependencyOutdated = false;
    if (hasArtifactTime) {
        for (const AssetDependencyDesc& dependency : record.artifactManifest.dependencies) {
            if (!ASSETS::SEMANTICS::IsSourceOnlyAssetDependencyRole(dependency.role) || dependency.path.empty()) {
                continue;
            }
            const std::filesystem::path dependencyPath =
                PROJECT_PATHS::ResolveProjectPath(projectRoot_, TEXT::PathFromUtf8(dependency.path));
            std::error_code dependencyEc{};
            if (!std::filesystem::exists(dependencyPath, dependencyEc)) {
                sourceDependencyOutdated = true;
                break;
            }
            const auto dependencyTime = std::filesystem::last_write_time(dependencyPath, dependencyEc);
            if (dependencyEc || dependencyTime > oldestArtifactTime) {
                sourceDependencyOutdated = true;
                break;
            }
        }
    }

    const IAssetImporter* importer = importerRegistry_.FindById(record.meta.importerId);
    const bool importerVersionOutdated =
        importer && record.meta.importerVersion != importer->GetSemantics().importerVersion;
    const auto* primary = ASSETS::SEMANTICS::FindPrimaryAssetArtifactSemantics(record.type);
    const bool needsPrimaryArtifact =
        primary &&
        (record.type != AssetType::Material ||
         ASSETS::IMPORT_POLICY::ResolveMaterialImportPolicy(record.meta.importSettingsJson).ShouldCookHmat()) &&
        !ASSETS::SEMANTICS::HasAssetArtifact(record, primary->kind);
    record.importOutdated = record.importerMissing || record.artifactMissing || sourceNewerThanArtifact ||
                            sourceDependencyOutdated || importerVersionOutdated || needsPrimaryArtifact;

    const std::filesystem::path reportPath = record.importedDirectory / "import_report.json";
    // manifest と report は同じ import 結果を持つ。大きな diagnostics を
    // 起動ごとに二度 parse しない。失敗時は report だけが更新され得る。
    bool needsReport = true;
    if (record.guid.IsValid() && record.artifactManifest.guid == record.guid) {
        record.lastImportSucceeded = record.artifactManifest.lastImportSucceeded;
        record.lastImportMessage = record.artifactManifest.lastImportMessage;
        std::error_code reportError{}, manifestError{};
        const auto reportTime = std::filesystem::last_write_time(reportPath, reportError);
        const auto manifestTime = std::filesystem::last_write_time(record.artifactManifestPath, manifestError);
        // 同一時刻は低精度 filesystem 上の更新順を決められないため保守的に読む。
        needsReport = !reportError && (manifestError || reportTime >= manifestTime);
    }
    nlohmann::json report;
    if (needsReport && !record.importedDirectory.empty() && SERIALIZATION::JSON::ReadJsonFile(reportPath, report) &&
        report.is_object()) {
        record.lastImportSucceeded = report.value("success", false);
        record.lastImportMessage = report.value("message", record.lastImportMessage);
    }
}

} // namespace HIKARI
