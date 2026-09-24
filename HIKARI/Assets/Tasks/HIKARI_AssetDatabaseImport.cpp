#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Collision/HIKARI_ModelCollisionArtifact.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include <algorithm>
#include <exception>
#include <unordered_set>
#include <utility>

namespace HIKARI {
namespace {
int ImportPriority(AssetType type) {
    switch (type) {
    case AssetType::Font:
    case AssetType::Texture: return 0;
    case AssetType::Sky: return 1;
    case AssetType::Material: return 2;
    case AssetType::VfxEffect: return 3;
    case AssetType::Model: return 4;
    case AssetType::ModelMaterialBinding: return 5;
    case AssetType::Scene: return 5;
    case AssetType::TextStyle:
    case AssetType::Sequence:
    case AssetType::AnimationStateMachine:
    case AssetType::Dialogue: return 5;
    default: return 6;
    }
}

} // namespace

std::vector<AssetGuid> AssetDatabase::PrepareImportGuids(const std::vector<AssetGuid>& guids, int& rejected,
                                                         bool sortByType) const {
    std::vector<AssetGuid> orderedGuids;
    std::unordered_set<std::string> visited;

    orderedGuids.reserve(guids.size());
    for (const AssetGuid& guid : guids) {
        if (!guid.IsValid() || !visited.insert(guid.value).second) {
            continue;
        }

        const AssetRecord* record = FindByGuid(guid);
        if (!record || !record->CanImport()) {
            ++rejected;
            continue;
        }

        orderedGuids.push_back(guid);
    }

    if (sortByType)
        std::stable_sort(orderedGuids.begin(), orderedGuids.end(), [this](const AssetGuid& lhs, const AssetGuid& rhs) {
            const AssetRecord* lhsRecord = FindByGuid(lhs);
            const AssetRecord* rhsRecord = FindByGuid(rhs);
            const int lhsPriority = lhsRecord ? ImportPriority(lhsRecord->type) : 100;
            const int rhsPriority = rhsRecord ? ImportPriority(rhsRecord->type) : 100;
            return lhsPriority < rhsPriority;
        });

    return orderedGuids;
}

std::vector<AssetGuid> AssetDatabase::CollectOutdatedImportGuids(const std::vector<const AssetRecord*>& records) const {
    std::vector<AssetGuid> guids;
    guids.reserve(records.size());
    for (const auto* record : records) {
        if (record && record->importOutdated && record->CanImport()) {
            guids.push_back(record->guid);
        }
    }
    return guids;
}

std::vector<AssetGuid> AssetDatabase::CollectDependencyImportGuids(const AssetRecord& rootRecord,
                                                                   bool includeSelf) const {
    std::vector<AssetGuid> importGuids;
    std::unordered_set<std::string> visited;
    importGuids.reserve(rootRecord.artifactManifest.dependencies.size() + (includeSelf ? 1u : 0u));

    auto queueGuid = [&](const AssetGuid& candidateGuid) {
        if (!candidateGuid.IsValid() || !visited.insert(candidateGuid.value).second) {
            return;
        }
        importGuids.push_back(candidateGuid);
    };

    for (const AssetDependencyDesc& dependency : rootRecord.artifactManifest.dependencies) {
        if (dependency.guid.IsValid()) {
            queueGuid(dependency.guid);
            continue;
        }
        if (!dependency.path.empty()) {
            if (const AssetRecord* dependencyRecord = FindByPath(dependency.path)) {
                queueGuid(dependencyRecord->guid);
            }
        }
    }

    if (includeSelf) {
        queueGuid(rootRecord.guid);
    }

    return importGuids;
}

bool AssetDatabase::ImportAsset(const AssetGuid& guid) {
    if (!CheckOwnerThread("asset import")) {
        return false;
    }
    if (cookedContent_) {
        HIKARI_LOG_ERROR("[AssetDatabase] cooked content is read-only; "
                         "ImportAsset was rejected.");
        return false;
    }

    AssetRecord* record = FindByGuid(guid);
    if (!record) {
        HIKARI_LOG_ERROR("[AssetDatabase] ImportAsset failed: GUID not found " + guid.value);
        return false;
    }

    if (!record->guid.IsValid()) {
        record->lastImportSucceeded = false;
        record->lastImportMessage = "[AssetDatabase] invalid GUID";
        return false;
    }

    if (!record->sourceExists) {
        AssetImportResult result{};
        result.message = "[AssetDatabase] source missing: " + TEXT::PathToUtf8(record->sourcePath);
        WriteImportReport(*record, result);
        RefreshRecordState(*record);
        return false;
    }

    IAssetImporter* importer = importerRegistry_.FindById(record->meta.importerId);
    if (!importer) {
        AssetImportResult result{};
        result.message = "[AssetDatabase] Unknown Importer: " + record->meta.importerId;
        WriteImportReport(*record, result);
        RefreshRecordState(*record);
        return false;
    }

    AssetImportContext context{};
    context.projectRoot = projectRoot_;
    context.assetsRoot = assetsRoot_;
    context.libraryRoot = libraryRoot_;
    context.sourceMetaRoot = sourceMetaRoot_;
    context.importedDirectory = GetImportedDirectory(record->guid);
    context.assetDatabase = this;
    const AssetRecord sourceSnapshot = *record;
    const uint32_t importerVersion = importer->GetSemantics().importerVersion;

    std::error_code ec{};
    std::filesystem::create_directories(context.importedDirectory, ec);
    if (ec) {
        AssetImportResult result{};
        result.message =
            "[AssetDatabase] failed to create imported directory: " + TEXT::PathToUtf8(context.importedDirectory);
        WriteImportReport(*record, result);
        RefreshRecordState(*record);
        return false;
    }

    AssetImportResult result{};
    try {
        context.publication = std::make_shared<AssetImportPublication>(projectRoot_, libraryRoot_, sourceMetaRoot_);
        result = importer->Import(*record, context);
    } catch (const std::exception& ex) {
        result.success = false;
        result.message = std::string("[AssetDatabase] importer exception: ") + ex.what();
        HIKARI_LOG_ERROR(result.message + " source=" + TEXT::PathToUtf8(record->sourcePath) +
                         " guid=" + record->guid.value);
    } catch (...) {
        result.success = false;
        result.message = "[AssetDatabase] importer exception: unknown";
        HIKARI_LOG_ERROR(result.message + " source=" + TEXT::PathToUtf8(record->sourcePath) +
                         " guid=" + record->guid.value);
    }
    result.publication = context.publication;
    std::string commitMessage{};
    return CommitPreparedImport(sourceSnapshot, importerVersion, std::move(result), commitMessage);
}

bool AssetDatabase::CommitPreparedImport(const AssetRecord& sourceSnapshot, uint32_t importerVersion,
                                         AssetImportResult result, std::string& outMessage) {
    AssetRecord* record = FindByGuid(sourceSnapshot.guid);
    if (record == nullptr) {
        outMessage = "[AssetDatabase] import result discarded because the asset no longer exists";
        return false;
    }
    if (record->sourcePath != sourceSnapshot.sourcePath || record->meta.importerId != sourceSnapshot.meta.importerId ||
        record->meta.importSettingsJson != sourceSnapshot.meta.importSettingsJson) {
        outMessage = "[AssetDatabase] import result discarded because the source or import settings changed";
        return false;
    }

    if (result.success) {
        AssetRecord updated = *record;
        AssetRecord* const originalRecord = record;
        record = &updated;
        record->meta.importerVersion = importerVersion;
        record->meta.sourcePath = TEXT::PathToUtf8(record->sourcePath);
        record->meta.displayName =
            record->displayName.empty() ? TEXT::PathToUtf8(record->sourcePath.stem()) : record->displayName;
        record->artifactManifest.manifestVersion = kAssetArtifactManifestVersion;
        record->artifactManifest.guid = record->guid;
        record->artifactManifest.sourcePath = TEXT::PathToUtf8(record->sourcePath);
        record->artifactManifest.importerId = record->meta.importerId;
        record->artifactManifest.importerVersion = record->meta.importerVersion;
        record->artifactManifest.lastImportSucceeded = true;
        record->artifactManifest.lastImportMessage = result.message;
        record->artifactManifest.diagnosticsJson = result.diagnosticsJson;
        record->artifactManifest.artifacts = result.artifacts;
        record->artifactManifest.dependencies = result.dependencies;
        const auto metaPath = record->metaPath;
        const auto manifestPath = record->artifactManifestPath;
        try {
            if (result.publication) {
                record->metaPath = result.publication->StageFile(metaPath);
                record->artifactManifestPath = result.publication->StageFile(manifestPath);
            }
            if (!WriteMeta(*record)) {
                result.success = false;
                result.message = "[AssetDatabase] import succeeded but meta write failed";
            } else if (!WriteArtifactManifest(*record, result)) {
                result.success = false;
                result.message = "[AssetDatabase] import succeeded but artifact manifest write failed";
            }
            if (result.success && result.publication) {
                std::string publicationMessage;
                if (!result.publication->Publish(publicationMessage)) {
                    result.success = false;
                    result.message = std::move(publicationMessage);
                }
            }
        } catch (const std::exception& error) {
            result.success = false;
            result.message = std::string("[AssetDatabase] publication failed: ") + error.what();
        }
        if (result.success) {
            updated.metaPath = metaPath;
            updated.artifactManifestPath = manifestPath;
            *originalRecord = std::move(updated);
        }
        record = originalRecord;
    }

    WriteImportReport(*record, result);
    RefreshRecordState(*record);
    if (result.success) {
        AdvanceContentRevision();
    }
    outMessage = result.message;
    return result.success;
}

bool AssetDatabase::RebuildModelCollisionArtifact(const AssetGuid& guid, std::string& outMessage) {
    if (!CheckOwnerThread("collision artifact rebuild")) {
        outMessage = "collision artifact rebuild requires the owner thread";
        return false;
    }
    if (cookedContent_) {
        outMessage = "cooked content is read-only";
        return false;
    }

    AssetRecord* record = FindByGuid(guid);
    if (record == nullptr || record->type != AssetType::Model) {
        outMessage = "model asset was not found";
        return false;
    }

    const ASSETS::COLLISION::ModelCollisionArtifactResult collision =
        ASSETS::COLLISION::BuildModelCollisionArtifact(*record, projectRoot_, GetImportedDirectory(guid));
    outMessage = collision.message;
    if (!collision.success) {
        return false;
    }

    std::vector<AssetArtifactDesc> artifacts = record->artifactManifest.artifacts;
    std::erase_if(artifacts, [](const AssetArtifactDesc& artifact) {
        return ASSETS::SEMANTICS::MatchesAssetArtifact(artifact,
                                                       ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry, true);
    });
    if (collision.ready) {
        std::error_code relativeEc{};
        const std::filesystem::path relative = std::filesystem::relative(collision.path, projectRoot_, relativeEc);
        artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
            ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry,
            relativeEc ? TEXT::PathToUtf8(collision.path) : TEXT::PathToUtf8(relative.lexically_normal())));
    }

    AssetImportResult manifestUpdate{};
    manifestUpdate.success = true;
    manifestUpdate.message = record->artifactManifest.lastImportMessage.empty()
                                 ? collision.message
                                 : record->artifactManifest.lastImportMessage;
    nlohmann::json diagnostics = nlohmann::json::parse(record->artifactManifest.diagnosticsJson, nullptr, false);
    if (!diagnostics.is_object()) {
        diagnostics = nlohmann::json::object();
    }
    diagnostics["collisionGeometry"] = {
        {"format", std::string(ASSETS::SEMANTICS::ToString(CookedAssetFormat::HCOLLISION))},
        {"ready", collision.ready},
        {"shapeCount", collision.shapeCount},
        {"message", collision.message},
        {"authoring", "Model Collision Workspace"},
    };
    manifestUpdate.diagnosticsJson = diagnostics.dump(2);
    manifestUpdate.dependencies = record->artifactManifest.dependencies;
    manifestUpdate.artifacts = std::move(artifacts);
    if (!WriteArtifactManifest(*record, manifestUpdate)) {
        outMessage = "failed to update model artifact manifest";
        return false;
    }
    if (collision.removeExisting) {
        std::error_code removeEc{};
        std::filesystem::remove(collision.path, removeEc);
        if (removeEc) {
            HIKARI_LOG_WARN("[AssetDatabase] obsolete collision artifact could not be removed: " + removeEc.message());
        }
    }

    record->artifactManifest.manifestVersion = kAssetArtifactManifestVersion;
    record->artifactManifest.guid = record->guid;
    record->artifactManifest.sourcePath = TEXT::PathToUtf8(record->sourcePath);
    record->artifactManifest.importerId = record->meta.importerId;
    record->artifactManifest.importerVersion = record->meta.importerVersion;
    record->artifactManifest.lastImportSucceeded = true;
    record->artifactManifest.lastImportMessage = manifestUpdate.message;
    record->artifactManifest.diagnosticsJson = manifestUpdate.diagnosticsJson;
    record->artifactManifest.dependencies = manifestUpdate.dependencies;
    record->artifactManifest.artifacts = manifestUpdate.artifacts;
    RefreshRecordState(*record);
    AdvanceContentRevision();
    return true;
}

AssetImportBatchResult AssetDatabase::ImportAssets(const std::vector<AssetGuid>& guids) {
    return ImportAssetBatch(guids, true);
}

AssetImportBatchResult AssetDatabase::ImportAssetBatch(const std::vector<AssetGuid>& guids, bool sortByType) {
    AssetImportBatchResult batch{};
    if (!CheckOwnerThread("asset batch import")) {
        batch.failed = static_cast<int>(guids.size());
        return batch;
    }
    const auto orderedGuids = PrepareImportGuids(guids, batch.failed, sortByType);

    for (const AssetGuid& guid : orderedGuids) {
        ++batch.attempted;
        if (ImportAsset(guid)) {
            ++batch.succeeded;
        } else {
            ++batch.failed;
        }
    }

    return batch;
}

AssetImportBatchResult AssetDatabase::ImportAllOutdated() {
    if (!CheckOwnerThread("outdated asset import")) {
        return {};
    }
    const auto importGuids = CollectOutdatedImportGuids(CollectAll());
    return ImportAssets(importGuids);
}

AssetImportBatchResult AssetDatabase::ImportOutdatedInDirectory(const std::filesystem::path& directory,
                                                                bool recursive) {
    if (!CheckOwnerThread("directory asset import")) {
        return {};
    }

    const auto importGuids = CollectOutdatedImportGuids(CollectInDirectory(directory, recursive));
    return ImportAssets(importGuids);
}

AssetImportBatchResult AssetDatabase::ImportDependencies(const AssetGuid& guid, bool includeSelf) {
    if (!CheckOwnerThread("dependency asset import")) {
        return {};
    }
    AssetImportBatchResult batch{};
    const AssetRecord* rootRecord = FindByGuid(guid);
    if (!rootRecord) {
        HIKARI_LOG_ERROR("[AssetDatabase] ImportDependencies failed: GUID not found " + guid.value);
        return batch;
    }

    return ImportAssetBatch(CollectDependencyImportGuids(*rootRecord, includeSelf), false);
}

} // namespace HIKARI
