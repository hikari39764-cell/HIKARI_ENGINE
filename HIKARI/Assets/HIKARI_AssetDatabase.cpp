#include "HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <sstream>
#include <unordered_set>

#include <json.hpp>

#include "Assets/Collision/HIKARI_ModelCollisionArtifact.h"
#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Assets/Importers/Policy/HIKARI_MaterialImportPolicy.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Importers/HIKARI_MaterialImporter.h"
#include "Importers/HIKARI_ModelImporter.h"
#include "Importers/HIKARI_AnimationStateMachineAssetImporter.h"
#include "Importers/HIKARI_SceneAssetImporter.h"
#include "Importers/HIKARI_SequenceAssetImporter.h"
#include "Importers/HIKARI_SkyCubemapImporter.h"
#include "Importers/HIKARI_TextureImportBackend_DirectXTex.h"
#include "Importers/HIKARI_TextureImporter.h"
#include "Importers/HIKARI_VfxAssetImporter.h"

namespace HIKARI {


    namespace {
        constexpr std::string_view kSourceMetaSuffix = ".hikari.asset.json";
        constexpr uint32_t kArtifactManifestVersion = 1;



        bool EndsWith(std::string_view text, std::string_view suffix) {
            return text.size() >= suffix.size() &&
                text.substr(text.size() - suffix.size()) == suffix;
        }

        bool IsIgnoredDirectoryName(const std::string& name) {
            const std::string lower = TEXT::ToLowerAsciiCopy(name);
            return lower == ".git" ||
                lower == "library" ||
                lower == "projectsettings" ||
                lower == "tmp" ||
                lower == "cache";
        }

        bool IsMetaPath(const std::filesystem::path& path) {
            return EndsWith(TEXT::ToLowerAsciiCopy(path.filename().string()), kSourceMetaSuffix);
        }

        std::filesystem::path MakeSourceMetaPath(
            const std::filesystem::path& sourceMetaRoot,
            const std::filesystem::path& relativeSource) {

            if (sourceMetaRoot.empty() || relativeSource.empty()) {
                return {};
            }

            std::filesystem::path metaPath = sourceMetaRoot / relativeSource.parent_path();
            metaPath /= relativeSource.filename().string() + std::string(kSourceMetaSuffix);
            return metaPath.lexically_normal();
        }

        std::string NormalizeImporterId(std::string importerId) {
            // 旧 Stub 名で保存済みの meta を、現在の正式 importer 名へ寄せる。
            if (importerId == "ModelImporterStub") {
                return std::string(ASSETS::SEMANTICS::GetAssetImporterSemantics(
                    ASSETS::SEMANTICS::AssetImporterKind::Model).importerId);
            }
            if (importerId == "SceneImporterStub") {
                return std::string(ASSETS::SEMANTICS::GetAssetImporterSemantics(
                    ASSETS::SEMANTICS::AssetImporterKind::Scene).importerId);
            }
            if (importerId == "VfxImporterStub") {
                return std::string(ASSETS::SEMANTICS::GetAssetImporterSemantics(
                    ASSETS::SEMANTICS::AssetImporterKind::VfxEffect).importerId);
            }
            return importerId;
        }

        nlohmann::json SerializeDependencies(const std::vector<AssetDependencyDesc>& dependencies) {
            nlohmann::json out = nlohmann::json::array();
            for (const AssetDependencyDesc& dependency : dependencies) {
                out.push_back({
                    { "guid", dependency.guid.value },
                    { "path", dependency.path },
                    { "role", dependency.role },
                });
            }
            return out;
        }

        nlohmann::json SerializeArtifacts(const std::vector<AssetArtifactDesc>& artifacts) {
            nlohmann::json out = nlohmann::json::array();
            for (const AssetArtifactDesc& artifact : artifacts) {
                out.push_back({
                    { "role", artifact.role },
                    { "path", artifact.path },
                    { "format", artifact.format },
                });
            }
            return out;
        }

        void ParseDependencies(const nlohmann::json& root, std::vector<AssetDependencyDesc>& out) {
            out.clear();
            if (!root.contains("dependencies") || !root["dependencies"].is_array()) {
                return;
            }

            for (const nlohmann::json& node : root["dependencies"]) {
                if (!node.is_object()) {
                    continue;
                }
                AssetDependencyDesc dependency{};
                dependency.guid.value = node.value("guid", "");
                dependency.path = node.value("path", "");
                dependency.role = node.value("role", "");
                out.push_back(std::move(dependency));
            }
        }

        void ParseArtifacts(const nlohmann::json& root, std::vector<AssetArtifactDesc>& out) {
            out.clear();
            if (!root.contains("artifacts") || !root["artifacts"].is_array()) {
                return;
            }

            for (const nlohmann::json& node : root["artifacts"]) {
                if (!node.is_object()) {
                    continue;
                }
                AssetArtifactDesc artifact{};
                artifact.role = node.value("role", "");
                artifact.path = node.value("path", "");
                artifact.format = node.value("format", "");
                out.push_back(std::move(artifact));
            }
        }

        int ImportPriority(AssetType type) {
            switch (type) {
            case AssetType::Texture:
                return 0;
            case AssetType::Sky:
                return 1;
            case AssetType::Material:
                return 2;
            case AssetType::VfxEffect:
                return 3;
            case AssetType::Model:
                return 4;
            case AssetType::Scene:
                return 5;
            case AssetType::Sequence:
            case AssetType::AnimationStateMachine:
                return 5;
            default:
                return 6;
            }
        }

    }

    bool AssetDatabase::Initialize(const std::filesystem::path& projectRoot) {
        std::error_code ec{};
        const std::filesystem::path root = projectRoot.empty()
            ? std::filesystem::current_path(ec)
            : projectRoot;

        projectRoot_ = std::filesystem::absolute(root, ec).lexically_normal();
        if (ec) {
            projectRoot_ = root.lexically_normal();
        }

        projectSettingsRoot_ = projectRoot_ / "ProjectSettings";

        nlohmann::json pipelineSettings{};
        const std::filesystem::path settingsPath = projectSettingsRoot_ / "asset_pipeline.json";
        (void)SERIALIZATION::JSON::ReadJsonFile(
            settingsPath,
            pipelineSettings);

        const auto resolvePipelinePath =
            [this, &pipelineSettings](const char* key, const char* fallback) {
                const std::string value = pipelineSettings.is_object()
                    ? pipelineSettings.value(key, std::string(fallback))
                    : std::string(fallback);
                return PROJECT_PATHS::ResolveProjectPath(projectRoot_, value);
            };

        assetsRoot_ = resolvePipelinePath("assetsRoot", "Assets");
        libraryRoot_ = resolvePipelinePath("libraryRoot", "Library");
        sourceMetaRoot_ = resolvePipelinePath("sourceMetaRoot", "ProjectSettings/AssetMeta");

        RegisterDefaultImporters();
        EnsureProjectDirectories();
        return true;
    }

    const std::filesystem::path& AssetDatabase::GetProjectRoot() const {
        return projectRoot_;
    }

    const std::filesystem::path& AssetDatabase::GetAssetsRoot() const {
        return assetsRoot_;
    }

    const std::filesystem::path& AssetDatabase::GetLibraryRoot() const {
        return libraryRoot_;
    }

    const std::filesystem::path& AssetDatabase::GetSourceMetaRoot() const {
        return sourceMetaRoot_;
    }

    uint64_t AssetDatabase::GetContentRevision() const noexcept {
        return contentRevision_;
    }

    AssetImporterRegistry& AssetDatabase::GetImporterRegistry() {
        return importerRegistry_;
    }

    const AssetImporterRegistry& AssetDatabase::GetImporterRegistry() const {
        return importerRegistry_;
    }

    bool AssetDatabase::ScanAssets(bool createMissingMeta) {
        EnsureProjectDirectories();

        records_.clear();
        directories_.clear();
        recordsByGuid_.clear();
        guidByNormalizedPath_.clear();
        AddDirectoryToCache("Assets");

        std::error_code ec{};
        if (!std::filesystem::exists(assetsRoot_, ec)) {
            HIKARI_LOG_WARN("[AssetDatabase] Assets root missing: " + assetsRoot_.generic_string());
            return false;
        }

        auto appendRecordForSource = [&](const std::filesystem::path& sourcePath, bool allowCreateMissingMeta) {
            std::filesystem::path relativeSource = NormalizeProjectPath(sourcePath);
            if (ASSETS::SEMANTICS::IsAssetCompanionSource(relativeSource)) {
                return true;
            }
            const AssetType guessedType = GuessAssetTypeFromPath(relativeSource);
            std::error_code metaEc{};
            const bool hasExistingMeta = std::filesystem::exists(GetMetaPathForSource(relativeSource), metaEc);
            if (guessedType == AssetType::Unknown && !hasExistingMeta) {
                return true;
            }

            AssetRecord record = BuildRecordForSource(relativeSource, allowCreateMissingMeta);
            const std::string pathKey = MakePathKey(record.sourcePath);
            if (guidByNormalizedPath_.find(pathKey) != guidByNormalizedPath_.end()) {
                return true;
            }

            const size_t index = records_.size();
            AddDirectoryToCache(record.sourcePath.parent_path());

            if (record.guid.IsValid()) {
                const auto existing = recordsByGuid_.find(record.guid.value);
                if (existing != recordsByGuid_.end()) {
                    record.duplicateGuid = true;
                    record.lastImportMessage = "[AssetDatabase] duplicate GUID: " + record.guid.value;
                    HIKARI_LOG_ERROR(record.lastImportMessage + " source=" + record.sourcePath.generic_string());
                } else {
                    recordsByGuid_[record.guid.value] = index;
                }
            }

            guidByNormalizedPath_[pathKey] = index;
            records_.push_back(std::move(record));
            return true;
        };

        std::filesystem::recursive_directory_iterator it(
            assetsRoot_,
            std::filesystem::directory_options::skip_permission_denied,
            ec);
        const std::filesystem::recursive_directory_iterator end{};
        for (; !ec && it != end; it.increment(ec)) {
            const std::filesystem::directory_entry& entry = *it;
            if (entry.is_directory(ec)) {
                if (IsIgnoredDirectoryName(entry.path().filename().string())) {
                    it.disable_recursion_pending();
                    continue;
                }

                std::error_code relativeEc{};
                const std::filesystem::path relativeDirectory =
                    std::filesystem::relative(entry.path(), projectRoot_, relativeEc);
                if (!relativeEc) {
                    AddDirectoryToCache(relativeDirectory.lexically_normal());
                }
                continue;
            }

            if (!entry.is_regular_file(ec)) {
                continue;
            }

            const std::filesystem::path& absoluteSource = entry.path();
            if (IsMetaPath(absoluteSource)) {
                continue;
            }

            appendRecordForSource(absoluteSource, createMissingMeta);
        }

        if (ec) {
            HIKARI_LOG_ERROR("[AssetDatabase] scan failed: " + ec.message());
            return false;
        }

        SortAndUniqueDirectories();
        AdvanceContentRevision();
        HIKARI_LOG_INFO("[AssetDatabase] scanned assets. count=" + std::to_string(records_.size()));
        return true;
    }

    bool AssetDatabase::ImportAsset(const AssetGuid& guid) {
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
            result.message = "[AssetDatabase] source missing: " + record->sourcePath.generic_string();
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
        const AssetRecord sourceSnapshot = *record;
        const uint32_t importerVersion =
            importer->GetSemantics().importerVersion;

        std::error_code ec{};
        std::filesystem::create_directories(context.importedDirectory, ec);
        if (ec) {
            AssetImportResult result{};
            result.message = "[AssetDatabase] failed to create imported directory: " + context.importedDirectory.generic_string();
            WriteImportReport(*record, result);
            RefreshRecordState(*record);
            return false;
        }

        AssetImportResult result{};
        try {
            result = importer->Import(*record, context);
        } catch (const std::exception& ex) {
            result.success = false;
            result.message = std::string("[AssetDatabase] importer exception: ") + ex.what();
            HIKARI_LOG_ERROR(result.message + " source=" + record->sourcePath.generic_string() + " guid=" + record->guid.value);
        } catch (...) {
            result.success = false;
            result.message = "[AssetDatabase] importer exception: unknown";
            HIKARI_LOG_ERROR(result.message + " source=" + record->sourcePath.generic_string() + " guid=" + record->guid.value);
        }
        std::string commitMessage{};
        return CommitPreparedImport(
            sourceSnapshot,
            importerVersion,
            std::move(result),
            commitMessage);
    }

    bool AssetDatabase::CommitPreparedImport(
        const AssetRecord& sourceSnapshot,
        uint32_t importerVersion,
        AssetImportResult result,
        std::string& outMessage) {

        AssetRecord* record = FindByGuid(sourceSnapshot.guid);
        if (record == nullptr) {
            outMessage =
                "[AssetDatabase] import result discarded because the asset no longer exists";
            return false;
        }
        if (record->sourcePath != sourceSnapshot.sourcePath ||
            record->meta.importerId != sourceSnapshot.meta.importerId ||
            record->meta.importSettingsJson !=
                sourceSnapshot.meta.importSettingsJson) {
            outMessage =
                "[AssetDatabase] import result discarded because the source or import settings changed";
            return false;
        }

        if (result.success) {
            record->meta.importerVersion = importerVersion;
            record->meta.sourcePath = record->sourcePath.generic_string();
            record->meta.displayName = record->displayName.empty()
                ? record->sourcePath.stem().string()
                : record->displayName;
            record->artifactManifest.manifestVersion =
                kArtifactManifestVersion;
            record->artifactManifest.guid = record->guid;
            record->artifactManifest.sourcePath =
                record->sourcePath.generic_string();
            record->artifactManifest.importerId =
                record->meta.importerId;
            record->artifactManifest.importerVersion =
                record->meta.importerVersion;
            record->artifactManifest.lastImportSucceeded = true;
            record->artifactManifest.lastImportMessage = result.message;
            record->artifactManifest.diagnosticsJson =
                result.diagnosticsJson;
            record->artifactManifest.artifacts = result.artifacts;
            record->artifactManifest.dependencies =
                result.dependencies;
            if (!WriteMeta(*record)) {
                result.success = false;
                result.message =
                    "[AssetDatabase] import succeeded but meta write failed";
            } else if (!WriteArtifactManifest(*record, result)) {
                result.success = false;
                result.message =
                    "[AssetDatabase] import succeeded but artifact manifest write failed";
            }
        }

        WriteImportReport(*record, result);
        RefreshRecordState(*record);
        if (result.success) {
            AdvanceContentRevision();
        }
        outMessage = result.message;
        return result.success;
    }

    bool AssetDatabase::RebuildModelCollisionArtifact(
        const AssetGuid& guid,
        std::string& outMessage) {

        AssetRecord* record = FindByGuid(guid);
        if (record == nullptr || record->type != AssetType::Model) {
            outMessage = "model asset was not found";
            return false;
        }

        const ASSETS::COLLISION::ModelCollisionArtifactResult collision =
            ASSETS::COLLISION::BuildModelCollisionArtifact(
                *record,
                projectRoot_,
                GetImportedDirectory(guid));
        outMessage = collision.message;
        if (!collision.success) {
            return false;
        }

        std::vector<AssetArtifactDesc> artifacts =
            record->artifactManifest.artifacts;
        std::erase_if(
            artifacts,
            [](const AssetArtifactDesc& artifact) {
                return ASSETS::SEMANTICS::MatchesAssetArtifact(
                    artifact,
                    ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry,
                    true);
            });
        if (collision.ready) {
            std::error_code relativeEc{};
            const std::filesystem::path relative = std::filesystem::relative(
                collision.path,
                projectRoot_,
                relativeEc);
            artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
                ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry,
                relativeEc
                    ? collision.path.generic_string()
                    : relative.lexically_normal().generic_string()));
        }

          AssetImportResult manifestUpdate{};
          manifestUpdate.success = true;
        manifestUpdate.message =
            record->artifactManifest.lastImportMessage.empty()
                ? collision.message
                : record->artifactManifest.lastImportMessage;
          nlohmann::json diagnostics = nlohmann::json::parse(
              record->artifactManifest.diagnosticsJson,
              nullptr,
              false);
          if (!diagnostics.is_object()) {
              diagnostics = nlohmann::json::object();
          }
          diagnostics["collisionGeometry"] = {
              { "format", std::string(ASSETS::SEMANTICS::ToString(
                    CookedAssetFormat::HCOLLISION)) },
              { "ready", collision.ready },
              { "shapeCount", collision.shapeCount },
              { "message", collision.message },
              { "authoring", "Model Collision Workspace" },
          };
          manifestUpdate.diagnosticsJson = diagnostics.dump(2);
        manifestUpdate.dependencies =
            record->artifactManifest.dependencies;
        manifestUpdate.artifacts = std::move(artifacts);
        if (!WriteArtifactManifest(*record, manifestUpdate)) {
            outMessage = "failed to update model artifact manifest";
            return false;
        }
        if (collision.removeExisting) {
            std::error_code removeEc{};
            std::filesystem::remove(collision.path, removeEc);
            if (removeEc) {
                HIKARI_LOG_WARN(
                    "[AssetDatabase] obsolete collision artifact could not be removed: " +
                    removeEc.message());
            }
        }

        record->artifactManifest.manifestVersion =
            kArtifactManifestVersion;
        record->artifactManifest.guid = record->guid;
        record->artifactManifest.sourcePath =
            record->sourcePath.generic_string();
        record->artifactManifest.importerId = record->meta.importerId;
        record->artifactManifest.importerVersion =
            record->meta.importerVersion;
        record->artifactManifest.lastImportSucceeded = true;
        record->artifactManifest.lastImportMessage = manifestUpdate.message;
        record->artifactManifest.diagnosticsJson =
            manifestUpdate.diagnosticsJson;
        record->artifactManifest.dependencies =
            manifestUpdate.dependencies;
        record->artifactManifest.artifacts =
            manifestUpdate.artifacts;
        RefreshRecordState(*record);
        AdvanceContentRevision();
        return true;
    }

    void AssetDatabase::AdvanceContentRevision() noexcept {
        ++contentRevision_;
        if (contentRevision_ == 0u) {
            contentRevision_ = 1u;
        }
    }

    AssetImportBatchResult AssetDatabase::ImportAssets(const std::vector<AssetGuid>& guids) {
        AssetImportBatchResult batch{};
        std::vector<AssetGuid> orderedGuids;
        std::unordered_set<std::string> visited;

        orderedGuids.reserve(guids.size());
        for (const AssetGuid& guid : guids) {
            if (!guid.IsValid() || !visited.insert(guid.value).second) {
                continue;
            }

            const AssetRecord* record = FindByGuid(guid);
            if (!record ||
                record->duplicateGuid ||
                !record->sourceExists ||
                record->importerMissing ||
                !record->guid.IsValid()) {
                ++batch.failed;
                continue;
            }

            orderedGuids.push_back(guid);
        }

        std::stable_sort(orderedGuids.begin(), orderedGuids.end(), [this](const AssetGuid& lhs, const AssetGuid& rhs) {
            const AssetRecord* lhsRecord = FindByGuid(lhs);
            const AssetRecord* rhsRecord = FindByGuid(rhs);
            const int lhsPriority = lhsRecord ? ImportPriority(lhsRecord->type) : 100;
            const int rhsPriority = rhsRecord ? ImportPriority(rhsRecord->type) : 100;
            return lhsPriority < rhsPriority;
        });

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
        std::vector<AssetGuid> importGuids;
        importGuids.reserve(records_.size());

        for (const AssetRecord& record : records_) {
            if (!record.importOutdated ||
                record.duplicateGuid ||
                !record.sourceExists ||
                record.importerMissing ||
                !record.guid.IsValid()) {
                continue;
            }
            importGuids.push_back(record.guid);
        }

        return ImportAssets(importGuids);
    }

    AssetImportBatchResult AssetDatabase::ImportOutdatedInDirectory(
        const std::filesystem::path& directory,
        bool recursive) {

        std::vector<AssetGuid> importGuids;

        const std::vector<const AssetRecord*> records = CollectInDirectory(directory, recursive);
        importGuids.reserve(records.size());
        for (const AssetRecord* record : records) {
            if (!record ||
                !record->importOutdated ||
                record->duplicateGuid ||
                !record->sourceExists ||
                record->importerMissing ||
                !record->guid.IsValid()) {
                continue;
            }
            importGuids.push_back(record->guid);
        }

        return ImportAssets(importGuids);
    }

    AssetImportBatchResult AssetDatabase::ImportDependencies(const AssetGuid& guid, bool includeSelf) {
        AssetImportBatchResult batch{};
        const AssetRecord* rootRecord = FindByGuid(guid);
        if (!rootRecord) {
            HIKARI_LOG_ERROR("[AssetDatabase] ImportDependencies failed: GUID not found " + guid.value);
            return batch;
        }

        std::vector<AssetGuid> importGuids;
        std::unordered_set<std::string> visited;
        importGuids.reserve(rootRecord->artifactManifest.dependencies.size() + (includeSelf ? 1u : 0u));

        auto queueGuid = [&](const AssetGuid& candidateGuid) {
            if (!candidateGuid.IsValid() || !visited.insert(candidateGuid.value).second) {
                return;
            }
            importGuids.push_back(candidateGuid);
        };

        for (const AssetDependencyDesc& dependency : rootRecord->artifactManifest.dependencies) {
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
            queueGuid(rootRecord->guid);
        }

        for (const AssetGuid& dependencyGuid : importGuids) {
            AssetRecord* record = FindByGuid(dependencyGuid);
            if (!record ||
                record->duplicateGuid ||
                !record->sourceExists ||
                record->importerMissing ||
                !record->guid.IsValid()) {
                ++batch.failed;
                continue;
            }

            ++batch.attempted;
            if (ImportAsset(dependencyGuid)) {
                ++batch.succeeded;
            } else {
                ++batch.failed;
            }
        }

        return batch;
    }


    const AssetRecord* AssetDatabase::FindByGuid(const AssetGuid& guid) const {
        const auto it = recordsByGuid_.find(guid.value);
        if (it == recordsByGuid_.end() || it->second >= records_.size()) {
            return nullptr;
        }
        return &records_[it->second];
    }

    AssetRecord* AssetDatabase::FindByGuid(const AssetGuid& guid) {
        const auto it = recordsByGuid_.find(guid.value);
        if (it == recordsByGuid_.end() || it->second >= records_.size()) {
            return nullptr;
        }
        return &records_[it->second];
    }

    const AssetRecord* AssetDatabase::FindByPath(const std::filesystem::path& path) const {
        const auto it = guidByNormalizedPath_.find(MakePathKey(path));
        if (it == guidByNormalizedPath_.end() || it->second >= records_.size()) {
            return nullptr;
        }
        return &records_[it->second];
    }

    AssetRecord* AssetDatabase::FindByPath(const std::filesystem::path& path) {
        const auto it = guidByNormalizedPath_.find(MakePathKey(path));
        if (it == guidByNormalizedPath_.end() || it->second >= records_.size()) {
            return nullptr;
        }
        return &records_[it->second];
    }

    std::vector<const AssetRecord*> AssetDatabase::CollectByType(AssetType type) const {
        std::vector<const AssetRecord*> out;
        for (const AssetRecord& record : records_) {
            if (record.type == type) {
                out.push_back(&record);
            }
        }
        return out;
    }

    std::vector<const AssetRecord*> AssetDatabase::CollectAll() const {
        std::vector<const AssetRecord*> out;
        out.reserve(records_.size());
        for (const AssetRecord& record : records_) {
            out.push_back(&record);
        }
        return out;
    }

    std::vector<std::filesystem::path> AssetDatabase::CollectDirectories() const {
        return directories_;
    }

    std::vector<const AssetRecord*> AssetDatabase::CollectInDirectory(
        const std::filesystem::path& directory,
        bool recursive) const {

        std::vector<const AssetRecord*> out;
        for (const AssetRecord& record : records_) {
            const bool include = recursive
                ? IsPathUnderDirectory(record.sourcePath, directory)
                : MakePathKey(record.sourcePath.parent_path()) == MakePathKey(directory);
            if (include) {
                out.push_back(&record);
            }
        }
        return out;
    }

    bool AssetDatabase::WriteMeta(const AssetRecord& record) {
        if (record.metaPath.empty()) {
            return false;
        }

        nlohmann::json settings = nlohmann::json::parse(record.meta.importSettingsJson, nullptr, false);
        if (!settings.is_object()) {
            settings = nlohmann::json::object();
        }

        nlohmann::json root{
            { "metaVersion", record.meta.metaVersion },
            { "guid", record.meta.guid.value },
            { "type", std::string(ASSETS::SEMANTICS::ToString(record.meta.type)) },
            { "importerId", record.meta.importerId },
            { "importerVersion", record.meta.importerVersion },
            { "sourcePath", record.meta.sourcePath },
            { "displayName", record.meta.displayName },
            { "importSettings", settings },
        };

        std::string writeMessage{};
        if (!SERIALIZATION::JSON::WriteJsonFile(
            record.metaPath,
            root,
            &writeMessage)) {
            HIKARI_LOG_ERROR(
                "[AssetDatabase] failed to write meta: " +
                writeMessage);
            return false;
        }
        return true;
    }

    bool AssetDatabase::ReadMeta(const std::filesystem::path& metaPath, AssetMeta& outMeta) const {
        const std::filesystem::path absoluteMetaPath = PROJECT_PATHS::ResolveProjectPath(projectRoot_, metaPath);

        nlohmann::json root;
        if (!SERIALIZATION::JSON::ReadJsonFile(
                absoluteMetaPath,
                root) ||
            !root.is_object()) {
            HIKARI_LOG_ERROR("[AssetDatabase] meta JSON parse failed: " + absoluteMetaPath.generic_string());
            return false;
        }

        outMeta = AssetMeta{};
        outMeta.metaVersion = root.value("metaVersion", 1u);
        outMeta.guid.value = root.value("guid", "");
        outMeta.type = ASSETS::SEMANTICS::ParseAssetType(
            root.value("type", "Unknown"));
        outMeta.importerId = NormalizeImporterId(root.value("importerId", ""));
        outMeta.importerVersion = root.value("importerVersion", 1u);
        outMeta.sourcePath = root.value("sourcePath", "");
        outMeta.displayName = root.value("displayName", "");
        if (root.contains("importSettings") && root["importSettings"].is_object()) {
            outMeta.importSettingsJson = root["importSettings"].dump(2);
        } else {
            outMeta.importSettingsJson = "{}";
        }
        return true;
    }

    bool AssetDatabase::RegenerateMeta(const std::filesystem::path& sourcePath) {
        AssetRecord* record = FindByPath(sourcePath);
        if (!record) {
            HIKARI_LOG_ERROR("[AssetDatabase] RegenerateMeta failed: source not found " + sourcePath.generic_string());
            return false;
        }

        const AssetGuid guid = record->guid.IsValid() ? record->guid : GenerateAssetGuid();
        const AssetType type = GuessAssetTypeFromPath(record->sourcePath);
        const std::string importerId = SelectDefaultImporterId(type, record->sourcePath);

        AssetMeta meta{};
        if (const IAssetImporter* importer = importerRegistry_.FindById(importerId)) {
            meta = importer->CreateDefaultMeta(record->sourcePath, guid);
        } else {
            meta.guid = guid;
            meta.type = type;
            meta.importerId = importerId;
            meta.importerVersion = 1;
            meta.sourcePath = record->sourcePath.generic_string();
            meta.displayName = record->sourcePath.stem().string();
            meta.importSettingsJson = "{}";
        }

        record->guid = meta.guid;
        record->type = meta.type;
        record->meta = std::move(meta);
        record->displayName = record->meta.displayName;
        record->metaPath = GetMetaPathForSource(record->sourcePath);
        record->importedDirectory = GetImportedDirectory(record->guid);
        if (record->guid.IsValid()) {
            const size_t index = static_cast<size_t>(record - records_.data());
            recordsByGuid_[record->guid.value] = index;
            guidByNormalizedPath_[MakePathKey(record->sourcePath)] = index;
        }
        const bool wrote = WriteMeta(*record);
        RefreshRecordState(*record);
        record->importOutdated = true;
        record->lastImportMessage = wrote
            ? "[AssetDatabase] Meta regenerated; reimport required"
            : "[AssetDatabase] Meta regeneration failed";
        return wrote;
    }

    std::filesystem::path AssetDatabase::GetMetaPathForSource(const std::filesystem::path& sourcePath) const {
        const std::filesystem::path relativeSource = NormalizeProjectPath(sourcePath);
        return MakeSourceMetaPath(sourceMetaRoot_, relativeSource);
    }

    std::filesystem::path AssetDatabase::GetArtifactManifestPath(const AssetGuid& guid) const {
        if (!guid.IsValid()) {
            return {};
        }
        return libraryRoot_ / "AssetDatabase" / "Artifacts" / (guid.value + ".artifact.json");
    }

    std::filesystem::path AssetDatabase::GetImportedDirectory(const AssetGuid& guid) const {
        if (!guid.IsValid()) {
            return {};
        }
        return libraryRoot_ / "Imported" / guid.value;
    }

    ClusteredGeometryArtifactState AssetDatabase::GetClusteredGeometryArtifactState(const AssetRecord& record) const {
        return GetClusteredGeometryArtifactInfo(record).state;
    }

    ClusteredGeometryArtifactState AssetDatabase::GetClusteredGeometryArtifactState(const AssetGuid& guid) const {
        return GetClusteredGeometryArtifactInfo(guid).state;
    }

    ClusteredGeometryArtifactInfo AssetDatabase::GetClusteredGeometryArtifactInfo(const AssetRecord& record) const {
        ClusteredGeometryArtifactInfo info{};
        const AssetArtifactDesc* artifact =
            ASSETS::SEMANTICS::FindAssetArtifact(
                record,
                ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry);
        if (artifact == nullptr) {
            info.state = ClusteredGeometryArtifactState::Missing;
            info.message = "HCMESH artifact is not recorded in artifact manifest";
            return info;
        }

        const std::filesystem::path hcmeshPath = PROJECT_PATHS::ResolveProjectPath(projectRoot_, artifact->path);
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
        if (!hcmeshInfo.valid ||
            hcmeshInfo.surfaceCount == 0u ||
            hcmeshInfo.clusterCount == 0u ||
            hcmeshInfo.vertexCount == 0u ||
            hcmeshInfo.meshletPrimitiveCount == 0u ||
            hcmeshInfo.geometryByteSize == 0u ||
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
            info.message = sourceNewerThanArtifact
                ? "Source model is newer than HCMESH"
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
        record.displayName = normalizedSource.stem().string();

        if (record.metaExists) {
            AssetMeta meta{};
            if (ReadMeta(record.metaPath, meta)) {
                record.meta = std::move(meta);
                record.guid = record.meta.guid;
                record.type = record.meta.type == AssetType::Unknown ? record.type : record.meta.type;
                record.displayName = record.meta.displayName.empty() ? record.displayName : record.meta.displayName;
                if (record.meta.sourcePath.empty() || NormalizeProjectPath(record.meta.sourcePath) != normalizedSource) {
                    record.meta.sourcePath = normalizedSource.generic_string();
                    if (createMissingMeta) {
                        WriteMeta(record);
                    }
                }
            } else {
                record.meta = AssetMeta{};
                record.meta.sourcePath = normalizedSource.generic_string();
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
                record.meta.sourcePath = normalizedSource.generic_string();
                record.meta.displayName = record.displayName;
                record.meta.importSettingsJson = "{}";
            }
            record.guid = record.meta.guid;
            record.displayName = record.meta.displayName;
            WriteMeta(record);
            record.metaExists = true;
        } else {
            record.meta = AssetMeta{};
            record.meta.type = record.type;
            record.meta.sourcePath = normalizedSource.generic_string();
            record.meta.displayName = record.displayName;
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

    AssetType AssetDatabase::GuessAssetTypeFromPath(const std::filesystem::path& sourcePath) const {
        return ASSETS::SEMANTICS::ClassifyAssetTypeFromPath(sourcePath);
    }

    std::string AssetDatabase::SelectDefaultImporterId(AssetType type, const std::filesystem::path& sourcePath) const {
        if (const ASSETS::SEMANTICS::AssetImporterSemantics* semantics =
            ASSETS::SEMANTICS::FindDefaultAssetImporterSemantics(type)) {
            return std::string(semantics->importerId);
        }
        if (const ASSETS::SEMANTICS::AssetImporterSemantics* semantics =
            ASSETS::SEMANTICS::ResolveAssetSourceSemantics(sourcePath)) {
            return std::string(semantics->importerId);
        }
        return {};
    }

    void AssetDatabase::RegisterDefaultImporters() {
        importerRegistry_.Register(std::make_unique<SkyCubemapImporter>(
            std::make_unique<DirectXTexTextureImportBackend>()));
        importerRegistry_.Register(std::make_unique<TextureImporter>(
            std::make_unique<DirectXTexTextureImportBackend>()));
        importerRegistry_.Register(std::make_unique<ModelImporter>());
        importerRegistry_.Register(std::make_unique<SceneAssetImporter>());
        importerRegistry_.Register(std::make_unique<SequenceAssetImporter>());
        importerRegistry_.Register(
            std::make_unique<AnimationStateMachineAssetImporter>());
        importerRegistry_.Register(std::make_unique<MaterialImporter>());
        importerRegistry_.Register(std::make_unique<VfxAssetImporter>());
    }

    void AssetDatabase::EnsureProjectDirectories() const {
        std::error_code ec{};
        const std::filesystem::path roots[] = {
            assetsRoot_,
            assetsRoot_ / "Models",
            assetsRoot_ / "Textures",
            assetsRoot_ / "Materials",
            assetsRoot_ / "Skies",
            assetsRoot_ / "IBL",
            assetsRoot_ / "Vfx",
            assetsRoot_ / "Shaders",
            assetsRoot_ / "Scenes",
            assetsRoot_ / "AnimationStateMachines",
            assetsRoot_ / "Sequences",
            libraryRoot_,
            libraryRoot_ / "Imported",
            libraryRoot_ / "Thumbnails",
            libraryRoot_ / "AssetDatabase",
            libraryRoot_ / "AssetDatabase" / "Artifacts",
            libraryRoot_ / "ShaderCache",
            projectSettingsRoot_,
            sourceMetaRoot_,
        };

        for (const std::filesystem::path& root : roots) {
            std::filesystem::create_directories(root, ec);
            ec.clear();
        }

        const std::filesystem::path settingsPath = projectSettingsRoot_ / "asset_pipeline.json";
        if (!std::filesystem::exists(settingsPath, ec)) {
            nlohmann::json settings{
                { "version", 1 },
                { "assetsRoot", "Assets" },
                { "libraryRoot", "Library" },
                { "importedRoot", "Library/Imported" },
                { "thumbnailRoot", "Library/Thumbnails" },
                { "assetDatabaseRoot", "Library/AssetDatabase" },
                { "shaderCacheRoot", "Library/ShaderCache" },
                { "sourceMetaRoot", "ProjectSettings/AssetMeta" },
            };

            (void)SERIALIZATION::JSON::WriteJsonFile(
                settingsPath,
                settings);
        }
    }

    void AssetDatabase::AddDirectoryToCache(const std::filesystem::path& directory) {
        if (directory.empty()) {
            return;
        }

        std::filesystem::path normalized = NormalizeProjectPath(directory).lexically_normal();
        if (normalized.empty()) {
            return;
        }

        directories_.push_back(std::move(normalized));
    }

    void AssetDatabase::SortAndUniqueDirectories() {
        std::sort(directories_.begin(), directories_.end(), [this](const auto& lhs, const auto& rhs) {
            return MakePathKey(lhs) < MakePathKey(rhs);
        });
        directories_.erase(std::unique(directories_.begin(), directories_.end(), [this](const auto& lhs, const auto& rhs) {
            return MakePathKey(lhs) == MakePathKey(rhs);
        }), directories_.end());
    }

    void AssetDatabase::RefreshRecordState(AssetRecord& record) const {
        const std::filesystem::path absoluteSource = PROJECT_PATHS::ResolveProjectPath(projectRoot_, record.sourcePath);
        record.sourceExists = std::filesystem::exists(absoluteSource);
        record.metaExists = std::filesystem::exists(record.metaPath);
        record.importerMissing = !record.meta.importerId.empty() &&
            importerRegistry_.FindById(record.meta.importerId) == nullptr;

        if (record.guid.IsValid()) {
            record.importedDirectory = GetImportedDirectory(record.guid);
            record.artifactManifestPath = GetArtifactManifestPath(record.guid);
        }

        bool artifactMissing = false;
        std::filesystem::file_time_type oldestArtifactTime{};
        bool hasArtifactTime = false;
        for (const AssetArtifactDesc& artifact : record.artifactManifest.artifacts) {
            const std::filesystem::path artifactPath = PROJECT_PATHS::ResolveProjectPath(projectRoot_, artifact.path);
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
            for (const AssetDependencyDesc& dependency :
                 record.artifactManifest.dependencies) {
                if (!ASSETS::SEMANTICS::IsSourceOnlyAssetDependencyRole(
                        dependency.role) ||
                    dependency.path.empty()) {
                    continue;
                }
                const std::filesystem::path dependencyPath =
                    PROJECT_PATHS::ResolveProjectPath(
                        projectRoot_,
                        dependency.path);
                std::error_code dependencyEc{};
                if (!std::filesystem::exists(
                        dependencyPath,
                        dependencyEc)) {
                    sourceDependencyOutdated = true;
                    break;
                }
                const auto dependencyTime =
                    std::filesystem::last_write_time(
                        dependencyPath,
                        dependencyEc);
                if (dependencyEc ||
                    dependencyTime > oldestArtifactTime) {
                    sourceDependencyOutdated = true;
                    break;
                }
            }
        }

        const IAssetImporter* importer = importerRegistry_.FindById(record.meta.importerId);
        const bool importerVersionOutdated =
            importer &&
            record.meta.importerVersion !=
                importer->GetSemantics().importerVersion;
        const bool textureNeedsArtifact = record.type == AssetType::Texture && record.artifactManifest.artifacts.empty();
        const bool modelNeedsArtifact = record.type == AssetType::Model && record.artifactManifest.artifacts.empty();
        const bool skyNeedsArtifact = record.type == AssetType::Sky && record.artifactManifest.artifacts.empty();
        // Material の cook 設定が有効な場合は artifact 欠落も outdated として扱う。
        const bool materialNeedsArtifact =
            record.type == AssetType::Material &&
            ASSETS::IMPORT_POLICY::ResolveMaterialImportPolicy(
                record.meta.importSettingsJson).ShouldCookHmat() &&
            !ASSETS::SEMANTICS::HasAssetArtifact(
                record,
                ASSETS::SEMANTICS::AssetArtifactKind::Material);
        record.importOutdated = record.importerMissing ||
            record.artifactMissing ||
            sourceNewerThanArtifact ||
            sourceDependencyOutdated ||
            importerVersionOutdated ||
            textureNeedsArtifact ||
            modelNeedsArtifact ||
            skyNeedsArtifact ||
            materialNeedsArtifact;

        const std::filesystem::path reportPath = record.importedDirectory / "import_report.json";
        nlohmann::json report;
        if (!record.importedDirectory.empty() &&
            SERIALIZATION::JSON::ReadJsonFile(reportPath, report) &&
            report.is_object()) {
            record.lastImportSucceeded = report.value("success", false);
            record.lastImportMessage = report.value("message", record.lastImportMessage);
        }
    }

    bool AssetDatabase::ReadArtifactManifest(
        const std::filesystem::path& manifestPath,
        AssetArtifactManifest& outManifest) const {

        const std::filesystem::path absoluteManifestPath = PROJECT_PATHS::ResolveProjectPath(projectRoot_, manifestPath);

        nlohmann::json root;
        if (!SERIALIZATION::JSON::ReadJsonFile(
                absoluteManifestPath,
                root) ||
            !root.is_object()) {
            return false;
        }

        const uint32_t manifestVersion = root.value("manifestVersion", 0u);
        if (manifestVersion != kArtifactManifestVersion) {
            HIKARI_LOG_WARN("[AssetDatabase] unsupported artifact manifest version: " +
                absoluteManifestPath.generic_string());
            return false;
        }

        outManifest = AssetArtifactManifest{};
        outManifest.manifestVersion = manifestVersion;
        outManifest.guid.value = root.value("guid", "");
        outManifest.sourcePath = root.value("sourcePath", "");
        outManifest.importerId = root.value("importerId", "");
        outManifest.importerVersion = root.value("importerVersion", 1u);
        outManifest.lastImportSucceeded = root.value("success", false);
        outManifest.lastImportMessage = root.value("message", "");
        if (root.contains("diagnostics") && root["diagnostics"].is_object()) {
            outManifest.diagnosticsJson = root["diagnostics"].dump(2);
        } else {
            outManifest.diagnosticsJson = root.value("diagnosticsText", "");
        }
        ParseDependencies(root, outManifest.dependencies);
        ParseArtifacts(root, outManifest.artifacts);
        return true;
    }

    bool AssetDatabase::WriteArtifactManifest(
        const AssetRecord& record,
        const AssetImportResult& result) const {

        if (record.artifactManifestPath.empty()) {
            return false;
        }

        nlohmann::json root{
            { "manifestVersion", kArtifactManifestVersion },
            { "guid", record.guid.value },
            { "sourcePath", record.sourcePath.generic_string() },
            { "importerId", record.meta.importerId },
            { "importerVersion", record.meta.importerVersion },
            { "success", result.success },
            { "message", result.message },
            { "dependencies", SerializeDependencies(result.dependencies) },
            { "artifacts", SerializeArtifacts(result.artifacts) },
        };

        if (!result.diagnosticsJson.empty()) {
            nlohmann::json diagnostics = nlohmann::json::parse(result.diagnosticsJson, nullptr, false);
            if (diagnostics.is_discarded()) {
                root["diagnosticsText"] = result.diagnosticsJson;
            } else {
                root["diagnostics"] = std::move(diagnostics);
            }
        }

        std::string writeMessage{};
        if (!SERIALIZATION::JSON::WriteJsonFile(
            record.artifactManifestPath,
            root,
            &writeMessage)) {
            HIKARI_LOG_ERROR(
                "[AssetDatabase] failed to write artifact manifest: " +
                writeMessage);
            return false;
        }
        return true;
    }

    bool AssetDatabase::WriteImportReport(const AssetRecord& record, const AssetImportResult& result) const {
        if (record.importedDirectory.empty()) {
            return false;
        }

        nlohmann::json root{
            { "guid", record.guid.value },
            { "sourcePath", record.sourcePath.generic_string() },
            { "importerId", record.meta.importerId },
            { "importerVersion", record.meta.importerVersion },
            { "success", result.success },
            { "message", result.message },
            { "artifacts", SerializeArtifacts(result.success ? result.artifacts : record.artifactManifest.artifacts) },
            { "dependencies", SerializeDependencies(result.success ? result.dependencies : record.artifactManifest.dependencies) },
        };

        if (!result.diagnosticsJson.empty()) {
            nlohmann::json diagnostics = nlohmann::json::parse(result.diagnosticsJson, nullptr, false);
            if (diagnostics.is_discarded()) {
                root["diagnosticsText"] = result.diagnosticsJson;
            } else {
                root["diagnostics"] = std::move(diagnostics);
            }
        }

        const std::filesystem::path reportPath = record.importedDirectory / "import_report.json";
        std::string writeMessage{};
        if (!SERIALIZATION::JSON::WriteJsonFile(
            reportPath,
            root,
            &writeMessage)) {
            HIKARI_LOG_ERROR(
                "[AssetDatabase] failed to write import report: " +
                writeMessage);
            return false;
        }
        return true;
    }

    std::filesystem::path AssetDatabase::NormalizeProjectPath(const std::filesystem::path& path) const {
        if (path.empty()) {
            return {};
        }

        std::filesystem::path normalized = path.lexically_normal();
        if (normalized.is_absolute() && !projectRoot_.empty()) {
            std::error_code ec{};
            const std::filesystem::path relative = std::filesystem::relative(normalized, projectRoot_, ec);
            if (!ec && !relative.empty() && relative.native().find(L"..") != 0) {
                normalized = relative;
            }
        }
        return normalized.lexically_normal();
    }

    std::string AssetDatabase::MakePathKey(const std::filesystem::path& path) const {
        return TEXT::ToLowerAsciiCopy(NormalizeProjectPath(path).generic_string());
    }

    bool AssetDatabase::IsPathUnderDirectory(
        const std::filesystem::path& path,
        const std::filesystem::path& directory) const {

        const std::string pathKey = MakePathKey(path);
        const std::string directoryKey = MakePathKey(directory);
        if (directoryKey.empty()) {
            return false;
        }
        if (pathKey == directoryKey) {
            return true;
        }
        const std::string prefix = directoryKey.back() == '/'
            ? directoryKey
            : directoryKey + '/';
        return pathKey.rfind(prefix, 0) == 0;
    }

} // namespace HIKARI
