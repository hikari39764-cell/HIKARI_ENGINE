#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "HIKARI_AssetRecord.h"
#include "Importers/HIKARI_AssetImporterRegistry.h"
#include "Tasks/HIKARI_AssetTaskService.h"

namespace HIKARI {

    struct AssetImportBatchResult {
        int attempted = 0;
        int succeeded = 0;
        int failed = 0;
    };

    struct AssetImportBatchStatus {
        bool active = false;
        bool completed = false;
        bool canceled = false;
        bool cancellationRequested = false;
        int total = 0;
        int finished = 0;
        int attempted = 0;
        int succeeded = 0;
        int failed = 0;
        std::string label{};
        std::string message{};
        std::vector<AssetTaskId> activeTaskIds{};
    };

    enum class ClusteredGeometryArtifactState {
        Missing,
        Exists,
        Valid,
        Invalid,
        Outdated,
    };

    struct ClusteredGeometryArtifactInfo {
        ClusteredGeometryArtifactState state = ClusteredGeometryArtifactState::Missing;
        std::filesystem::path path{};
        std::string message{};
        bool validationValid = false;
        uint32_t invalidSurfaceCount = 0;
        uint32_t invalidClusterCount = 0;
        uint32_t invalidPageCount = 0;
        uint32_t invalidBoundsCount = 0;
        uint32_t invalidMaterialCount = 0;
        std::vector<std::string> validationMessages{};
    };

    class AssetDatabase {
    public:
        bool Initialize(const std::filesystem::path& projectRoot);

        const std::filesystem::path& GetProjectRoot() const;
        const std::filesystem::path& GetAssetsRoot() const;
        const std::filesystem::path& GetLibraryRoot() const;
        const std::filesystem::path& GetSourceMetaRoot() const;
        uint64_t GetContentRevision() const noexcept;

        AssetImporterRegistry& GetImporterRegistry();
        const AssetImporterRegistry& GetImporterRegistry() const;

        bool ScanAssets(bool createMissingMeta);
        bool ImportAsset(const AssetGuid& guid);
        bool RebuildModelCollisionArtifact(
            const AssetGuid& guid,
            std::string& outMessage);
        AssetImportBatchResult ImportAssets(const std::vector<AssetGuid>& guids);
        AssetImportBatchResult ImportAllOutdated();
        AssetImportBatchResult ImportOutdatedInDirectory(const std::filesystem::path& directory, bool recursive);
        AssetImportBatchResult ImportDependencies(const AssetGuid& guid, bool includeSelf = false);

        bool QueueImportAssets(
            const std::vector<AssetGuid>& guids,
            std::string label);
        bool QueueImportAllOutdated(std::string label = "Outdated assets");
        bool QueueImportOutdatedInDirectory(
            const std::filesystem::path& directory,
            bool recursive,
            std::string label = {});
        bool QueueImportDependencies(
            const AssetGuid& guid,
            bool includeSelf = false,
            std::string label = "Selected dependencies");
        bool RequestCancelQueuedImport();
        AssetImportBatchStatus GetQueuedImportStatus() const;
        bool ConsumeCompletedImportBatch(AssetImportBatchStatus& outStatus);
        void PumpAssetTasks();
        AssetTaskService& GetAssetTaskService() noexcept;
        const AssetTaskService& GetAssetTaskService() const noexcept;

        const AssetRecord* FindByGuid(const AssetGuid& guid) const;
        AssetRecord* FindByGuid(const AssetGuid& guid);

        const AssetRecord* FindByPath(const std::filesystem::path& path) const;
        AssetRecord* FindByPath(const std::filesystem::path& path);

        std::vector<const AssetRecord*> CollectByType(AssetType type) const;
        std::vector<const AssetRecord*> CollectAll() const;
        std::vector<std::filesystem::path> CollectDirectories() const;
        std::vector<const AssetRecord*> CollectInDirectory(const std::filesystem::path& directory, bool recursive) const;

        bool WriteMeta(const AssetRecord& record);
        bool ReadMeta(const std::filesystem::path& metaPath, AssetMeta& outMeta) const;
        bool RegenerateMeta(const std::filesystem::path& sourcePath);

        std::filesystem::path GetMetaPathForSource(const std::filesystem::path& sourcePath) const;
        std::filesystem::path GetArtifactManifestPath(const AssetGuid& guid) const;
        std::filesystem::path GetImportedDirectory(const AssetGuid& guid) const;
        ClusteredGeometryArtifactState GetClusteredGeometryArtifactState(const AssetRecord& record) const;
        ClusteredGeometryArtifactState GetClusteredGeometryArtifactState(const AssetGuid& guid) const;
        ClusteredGeometryArtifactInfo GetClusteredGeometryArtifactInfo(const AssetRecord& record) const;
        ClusteredGeometryArtifactInfo GetClusteredGeometryArtifactInfo(const AssetGuid& guid) const;

    private:
        AssetRecord BuildRecordForSource(const std::filesystem::path& sourcePath, bool createMissingMeta);
        AssetType GuessAssetTypeFromPath(const std::filesystem::path& sourcePath) const;
        std::string SelectDefaultImporterId(AssetType type, const std::filesystem::path& sourcePath) const;

        void RegisterDefaultImporters();
        void EnsureProjectDirectories() const;
        void AddDirectoryToCache(const std::filesystem::path& directory);
        void SortAndUniqueDirectories();
        void RefreshRecordState(AssetRecord& record) const;
        bool ReadArtifactManifest(
            const std::filesystem::path& manifestPath,
            AssetArtifactManifest& outManifest) const;
        bool WriteArtifactManifest(
            const AssetRecord& record,
            const AssetImportResult& result) const;
        bool WriteImportReport(const AssetRecord& record, const AssetImportResult& result) const;

        std::filesystem::path NormalizeProjectPath(const std::filesystem::path& path) const;
        std::string MakePathKey(const std::filesystem::path& path) const;
        bool IsPathUnderDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) const;
        void AdvanceContentRevision() noexcept;
        bool CommitPreparedImport(
            const AssetRecord& sourceSnapshot,
            uint32_t importerVersion,
            AssetImportResult result,
            std::string& outMessage);
        void ScheduleQueuedImportWork();

        struct QueuedImportBatch;

        std::filesystem::path projectRoot_{};
        std::filesystem::path assetsRoot_{};
        std::filesystem::path libraryRoot_{};
        std::filesystem::path projectSettingsRoot_{};
        std::filesystem::path sourceMetaRoot_{};

        AssetImporterRegistry importerRegistry_{};
        std::vector<AssetRecord> records_{};
        std::vector<std::filesystem::path> directories_{};
        std::unordered_map<std::string, size_t> recordsByGuid_{};
        std::unordered_map<std::string, size_t> guidByNormalizedPath_{};
        uint64_t contentRevision_ = 1u;
        std::shared_ptr<QueuedImportBatch> queuedImportBatch_{};
        AssetImportBatchStatus lastCompletedImportBatch_{};
        bool completedImportBatchPending_ = false;
        AssetTaskService assetTaskService_{};
    };

} // namespace HIKARI
