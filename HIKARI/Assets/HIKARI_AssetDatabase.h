#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "HIKARI_AssetImportInputs.h"
#include "HIKARI_AssetRecord.h"
#include "Importers/HIKARI_AssetImporterRegistry.h"
#include "Tasks/HIKARI_AssetTaskService.h"

namespace HIKARI {

class DocumentSceneAssetBindings;

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
    // record の元資産の絶対パス。相対で記録されていればプロジェクト基準で解決し、字句的に正規化する。
    std::filesystem::path ResolveSourcePath(const AssetRecord& record) const;
    const std::filesystem::path& GetAssetsRoot() const;
    const std::filesystem::path& GetLibraryRoot() const;
    const std::filesystem::path& GetSourceMetaRoot() const;
    uint64_t GetContentRevision() const noexcept;
    bool IsCookedContent() const noexcept;

    AssetImporterRegistry& GetImporterRegistry();
    const AssetImporterRegistry& GetImporterRegistry() const;

    bool ScanAssets(bool createMissingMeta);
    bool ImportAsset(const AssetGuid& guid);
    bool RebuildModelCollisionArtifact(const AssetGuid& guid, std::string& outMessage);
    AssetImportBatchResult ImportAssets(const std::vector<AssetGuid>& guids);
    AssetImportBatchResult ImportAllOutdated();
    AssetImportBatchResult ImportDependencies(const AssetGuid& guid, bool includeSelf = false);

    bool QueueImportAssets(const std::vector<AssetGuid>& guids, std::string label);
    bool QueueImportAllOutdated(std::string label = "Outdated assets");
    bool QueueImportOutdatedInDirectory(const std::filesystem::path& directory, bool recursive, std::string label = {});
    bool QueueImportDependencies(const AssetGuid& guid, bool includeSelf = false,
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

    // Cooked artifact paths are not source paths. Authoring workflows that
    // need the owning source asset must resolve them through the manifest
    // owner instead of guessing a GUID from a Library path.
    const AssetRecord* FindByArtifactPath(const std::filesystem::path& path) const;
    AssetRecord* FindByArtifactPath(const std::filesystem::path& path);

    std::vector<const AssetRecord*> CollectByType(AssetType type) const;
    std::vector<const AssetRecord*> CollectAll() const;
    std::vector<std::filesystem::path> CollectDirectories() const;
    std::vector<const AssetRecord*> CollectInDirectory(const std::filesystem::path& directory, bool recursive) const;

    bool WriteMeta(const AssetRecord& record);
    bool UpdateImportSettings(const AssetGuid& guid, const AssetMeta& expected, const AssetMeta& edited,
                              std::string& error);
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
    friend class DocumentSceneAssetBindings;

    struct ContentSnapshot {
        std::vector<AssetRecord> records{};
        std::vector<std::filesystem::path> directories{};
        std::unordered_map<std::string, size_t> recordsByGuid{};
        std::unordered_map<std::string, size_t> guidByNormalizedPath{};
        uint64_t contentRevision = 1u;
    };

    ContentSnapshot TakeContentSnapshot() noexcept;
    void RestoreContentSnapshot(ContentSnapshot&& snapshot) noexcept;
    bool TryTakeContentSnapshot(ContentSnapshot& outSnapshot);
    bool CheckOwnerThread(const char* operation) const;
    bool CanReplaceContentSnapshot() const;
    AssetRecord BuildRecordForSource(const std::filesystem::path& sourcePath, bool createMissingMeta);
    AssetType GuessAssetTypeFromPath(const std::filesystem::path& sourcePath) const;
    std::string SelectDefaultImporterId(AssetType type, const std::filesystem::path& sourcePath) const;

    void RegisterDefaultImporters();
    bool ScanAssetsInPlace(bool createMissingMeta);
    void EnsureProjectDirectories() const;
    bool LoadCookedRecords();
    void AddDirectoryToCache(const std::filesystem::path& directory);
    void SortAndUniqueDirectories();
    void RefreshRecordState(AssetRecord& record);
    void RefreshImportDependents(const AssetGuid& guid);
    bool ReadArtifactManifest(const std::filesystem::path& manifestPath, AssetArtifactManifest& outManifest) const;
    bool WriteArtifactManifest(const AssetRecord& record, const AssetImportResult& result) const;
    bool WriteImportReport(const AssetRecord& record, const AssetImportResult& result);
    void LoadImportFailures();
    void SetImportFailed(const AssetGuid& guid, bool failed);
    void SaveImportFailures();

    std::filesystem::path NormalizeProjectPath(const std::filesystem::path& path) const;
    std::string MakePathKey(const std::filesystem::path& path) const;
    bool IsPathUnderDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) const;
    void AdvanceContentRevision() noexcept;
    static AssetImportResult PrepareImportResult(IAssetImporter& importer, const AssetRecord& source,
                                                 AssetImportContext& context);
    bool CommitPreparedImport(const AssetRecord& sourceSnapshot, uint32_t importerVersion, AssetImportResult result,
                              std::string& outMessage);
    std::vector<AssetGuid> PrepareImportGuids(const std::vector<AssetGuid>& guids, int& rejected,
                                              bool sortByType) const;
    std::vector<AssetGuid> CollectOutdatedImportGuids(const std::vector<const AssetRecord*>& records) const;
    std::vector<AssetGuid> CollectDependencyImportGuids(const AssetRecord& rootRecord, bool includeSelf) const;
    AssetImportBatchResult ImportAssetBatch(const std::vector<AssetGuid>& guids, bool sortByType);
    void ScheduleQueuedImportWork();

    struct QueuedImportBatch;
    // 最後の取り込みが失敗した GUID（Library/AssetDatabase/ImportFailures.json）。成功時は manifest と
    // import_report.json が同じ結果を持つので、record の状態はここにある資産と manifest のない資産だけ report から読む。
    // complete でない間（一覧がない・読めない）はすべての report を読み、次に成功した走査の終わりに作り直す。
    struct ImportFailureList {
        std::unordered_set<std::string> guids{};
        bool complete = false;
        bool dirty = false;
    };

    std::filesystem::path projectRoot_{};
    std::filesystem::path assetsRoot_{};
    std::filesystem::path libraryRoot_{};
    std::filesystem::path projectSettingsRoot_{};
    std::filesystem::path sourceMetaRoot_{};
    std::filesystem::path cookedManifestPath_{};
    bool cookedContent_ = false;

    AssetImporterRegistry importerRegistry_{};
    std::vector<AssetRecord> records_{};
    std::vector<std::filesystem::path> directories_{};
    std::unordered_map<std::string, size_t> recordsByGuid_{};
    std::unordered_map<std::string, size_t> guidByNormalizedPath_{};
    uint64_t contentRevision_ = 1u;
    AssetInputDigestCache inputDigests_{};
    ImportFailureList importFailures_{};
    std::shared_ptr<QueuedImportBatch> queuedImportBatch_{};
    AssetImportBatchStatus lastCompletedImportBatch_{};
    bool completedImportBatchPending_ = false;
    mutable std::mutex ownerThreadMutex_{};
    std::thread::id ownerThread_{};
    // worker が参照する record/importer/mutex より先に停止・join する。
    AssetTaskService assetTaskService_{};
};

} // namespace HIKARI
