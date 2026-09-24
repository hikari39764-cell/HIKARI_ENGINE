#include "Assets/HIKARI_AssetDatabase.h"

#include <Windows.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Core/HIKARI_Logger.h"

namespace HIKARI {

struct AssetDatabase::QueuedImportBatch {
    struct WorkItem {
        AssetRecord sourceSnapshot{};
        IAssetImporter* importer = nullptr;
        uint32_t importerVersion = 0u;
        AssetImportContext context{};
        std::filesystem::file_time_type sourceWriteTime{};
        bool hasSourceWriteTime = false;
    };

    std::vector<WorkItem> textureItems{};
    std::vector<WorkItem> serialItems{};
    size_t nextTextureIndex = 0u;
    size_t nextSerialIndex = 0u;
    uint32_t activeTextureTasks = 0u;
    bool serialTaskActive = false;
    AssetImportBatchStatus status{};
};

namespace {
class ScopedComInitialization {
public:
    ScopedComInitialization() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = SUCCEEDED(hr);
    }

    ~ScopedComInitialization() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    ScopedComInitialization(const ScopedComInitialization&) = delete;
    ScopedComInitialization& operator=(const ScopedComInitialization&) = delete;

private:
    bool initialized_ = false;
};

} // namespace

bool AssetDatabase::QueueImportAssets(const std::vector<AssetGuid>& guids, std::string label) {
    if (!CheckOwnerThread("queued asset import")) {
        return false;
    }
    if (cookedContent_) {
        HIKARI_LOG_ERROR("[AssetDatabase] cooked content is read-only; "
                         "queued import was rejected.");
        return false;
    }

    if (queuedImportBatch_ && queuedImportBatch_->status.active) {
        return false;
    }

    auto operation = std::make_shared<QueuedImportBatch>();
    operation->status.active = true;
    operation->status.label = label.empty() ? "Asset import" : std::move(label);

    const auto orderedGuids = PrepareImportGuids(guids, operation->status.failed, true);
    operation->status.finished = operation->status.failed;
    operation->status.total = operation->status.failed + static_cast<int>(orderedGuids.size());

    for (const AssetGuid& guid : orderedGuids) {
        const AssetRecord* record = FindByGuid(guid);
        if (record == nullptr) {
            ++operation->status.failed;
            ++operation->status.finished;
            continue;
        }
        IAssetImporter* importer = importerRegistry_.FindById(record->meta.importerId);
        if (importer == nullptr) {
            ++operation->status.failed;
            ++operation->status.finished;
            continue;
        }

        QueuedImportBatch::WorkItem item{};
        item.sourceSnapshot = *record;
        item.importer = importer;
        item.importerVersion = importer->GetSemantics().importerVersion;
        item.context.projectRoot = projectRoot_;
        item.context.assetsRoot = assetsRoot_;
        item.context.libraryRoot = libraryRoot_;
        item.context.sourceMetaRoot = sourceMetaRoot_;
        item.context.importedDirectory = GetImportedDirectory(record->guid);
        item.context.assetDatabase = this;

        std::error_code timeEc{};
        item.sourceWriteTime = std::filesystem::last_write_time(projectRoot_ / record->sourcePath, timeEc);
        item.hasSourceWriteTime = !timeEc;

        if (record->type == AssetType::Texture) {
            operation->textureItems.push_back(std::move(item));
        } else {
            operation->serialItems.push_back(std::move(item));
        }
    }

    queuedImportBatch_ = operation;
    ScheduleQueuedImportWork();
    return true;
}

bool AssetDatabase::QueueImportAllOutdated(std::string label) {
    if (!CheckOwnerThread("queued outdated asset import")) {
        return false;
    }
    const auto guids = CollectOutdatedImportGuids(CollectAll());
    return QueueImportAssets(guids, std::move(label));
}

bool AssetDatabase::QueueImportOutdatedInDirectory(const std::filesystem::path& directory, bool recursive,
                                                   std::string label) {
    if (!CheckOwnerThread("queued directory asset import")) {
        return false;
    }
    const auto guids = CollectOutdatedImportGuids(CollectInDirectory(directory, recursive));
    if (label.empty()) {
        label = "Current folder " + directory.generic_string();
    }
    return QueueImportAssets(guids, std::move(label));
}

bool AssetDatabase::QueueImportDependencies(const AssetGuid& guid, bool includeSelf, std::string label) {
    if (!CheckOwnerThread("queued dependency asset import")) {
        return false;
    }
    const AssetRecord* rootRecord = FindByGuid(guid);
    if (rootRecord == nullptr) {
        return false;
    }

    const auto guids = CollectDependencyImportGuids(*rootRecord, includeSelf);
    return QueueImportAssets(guids, std::move(label));
}

void AssetDatabase::ScheduleQueuedImportWork() {
    const std::shared_ptr<QueuedImportBatch> operation = queuedImportBatch_;
    if (!operation || !operation->status.active) {
        return;
    }

    const auto finishOperationIfReady = [this, &operation]() {
        if (operation->activeTextureTasks != 0u || operation->serialTaskActive) {
            return false;
        }

        const bool texturesRemaining = operation->nextTextureIndex < operation->textureItems.size();
        const bool serialRemaining = operation->nextSerialIndex < operation->serialItems.size();
        if (!operation->status.cancellationRequested && (texturesRemaining || serialRemaining)) {
            return false;
        }

        operation->status.active = false;
        operation->status.completed = true;
        operation->status.canceled = operation->status.cancellationRequested;
        if (operation->status.canceled) {
            operation->status.message = "Import canceled after " + std::to_string(operation->status.finished) + " of " +
                                        std::to_string(operation->status.total) + " asset(s)";
        } else if (operation->status.failed > 0) {
            operation->status.message =
                "Import finished with " + std::to_string(operation->status.failed) + " failure(s)";
        } else if (operation->status.attempted == 0) {
            operation->status.message = "All selected assets are up to date";
        } else {
            operation->status.message = "Imported " + std::to_string(operation->status.succeeded) + " asset(s)";
        }
        lastCompletedImportBatch_ = operation->status;
        completedImportBatchPending_ = true;
        if (queuedImportBatch_ == operation) {
            queuedImportBatch_.reset();
        }
        return true;
    };

    if (finishOperationIfReady()) {
        return;
    }
    if (operation->status.cancellationRequested) {
        return;
    }

    const auto submitItem = [this, operation](QueuedImportBatch::WorkItem item, bool textureTask) {
        auto preparedResult = std::make_shared<AssetImportResult>();
        auto taskId = std::make_shared<AssetTaskId>(0u);

        AssetTaskRequest request{};
        request.category = "Asset Import";
        request.label = item.sourceSnapshot.displayName.empty() ? item.sourceSnapshot.sourcePath.filename().string()
                                                                : item.sourceSnapshot.displayName;
        request.initialItem = item.sourceSnapshot.sourcePath.generic_string();
        request.cancelable = true;
        request.work = [item, preparedResult](AssetTaskContext& task) mutable {
            const ScopedComInitialization comInitialization{};
            if (task.IsCancellationRequested()) {
                return AssetTaskOutcome::Canceled();
            }

            AssetImportContext context = item.context;
            context.task = &task;
            task.ReportStage("Preparing import directory", 0.01f, true,
                             item.sourceSnapshot.sourcePath.generic_string());

            std::error_code directoryEc{};
            std::filesystem::create_directories(context.importedDirectory, directoryEc);
            if (directoryEc) {
                preparedResult->message = "[AssetDatabase] failed to create imported directory: " +
                                          context.importedDirectory.generic_string();
                return AssetTaskOutcome::Failed(preparedResult->message);
            }

            try {
                context.publication = std::make_shared<AssetImportPublication>(context.projectRoot, context.libraryRoot,
                                                                               context.sourceMetaRoot);
                *preparedResult = item.importer->Import(item.sourceSnapshot, context);
            } catch (const std::exception& error) {
                preparedResult->success = false;
                preparedResult->message = std::string("[AssetDatabase] importer exception: ") + error.what();
            } catch (...) {
                preparedResult->success = false;
                preparedResult->message = "[AssetDatabase] importer exception: unknown";
            }
            preparedResult->publication = context.publication;

            if (task.IsCancellationRequested() && !preparedResult->success) {
                return AssetTaskOutcome::Canceled(preparedResult->message.empty() ? "asset import canceled"
                                                                                  : preparedResult->message);
            }
            return preparedResult->success
                       ? AssetTaskOutcome::Succeeded(preparedResult->message)
                       : AssetTaskOutcome::Failed(preparedResult->message.empty() ? "asset import failed"
                                                                                  : preparedResult->message);
        };
        request.finalize = [this, operation, item = std::move(item), preparedResult, taskId,
                            textureTask](const AssetTaskOutcome& workOutcome) {
            std::erase(operation->status.activeTaskIds, *taskId);
            if (textureTask) {
                if (operation->activeTextureTasks > 0u) {
                    --operation->activeTextureTasks;
                }
            } else {
                operation->serialTaskActive = false;
            }

            ++operation->status.finished;
            AssetTaskOutcome finalOutcome = workOutcome;
            if (workOutcome.canceled) {
                operation->status.canceled = true;
            } else {
                if (preparedResult->message.empty()) {
                    preparedResult->message = workOutcome.message;
                }

                bool sourceUnchanged = true;
                if (item.hasSourceWriteTime) {
                    std::error_code timeEc{};
                    const auto currentWriteTime =
                        std::filesystem::last_write_time(projectRoot_ / item.sourceSnapshot.sourcePath, timeEc);
                    sourceUnchanged = !timeEc && currentWriteTime == item.sourceWriteTime;
                }

                std::string commitMessage{};
                const bool committed =
                    sourceUnchanged && CommitPreparedImport(item.sourceSnapshot, item.importerVersion,
                                                            std::move(*preparedResult), commitMessage);
                if (committed) {
                    ++operation->status.succeeded;
                    finalOutcome = AssetTaskOutcome::Succeeded(std::move(commitMessage));
                } else {
                    ++operation->status.failed;
                    if (!sourceUnchanged) {
                        commitMessage = "Import result discarded because the source changed while it was processing";
                    }
                    finalOutcome = AssetTaskOutcome::Failed(commitMessage.empty() ? workOutcome.message
                                                                                  : std::move(commitMessage));
                }
            }

            ScheduleQueuedImportWork();
            return finalOutcome;
        };

        *taskId = assetTaskService_.Submit(std::move(request));
        if (*taskId == 0u) {
            ++operation->status.failed;
            ++operation->status.finished;
            if (textureTask && operation->activeTextureTasks > 0u) {
                --operation->activeTextureTasks;
            }
            if (!textureTask) {
                operation->serialTaskActive = false;
            }
            return;
        }
        operation->status.activeTaskIds.push_back(*taskId);
        ++operation->status.attempted;
    };

    const uint32_t textureConcurrency = (std::max)(1u, assetTaskService_.GetWorkerCount());
    while (operation->nextTextureIndex < operation->textureItems.size() &&
           operation->activeTextureTasks < textureConcurrency) {
        QueuedImportBatch::WorkItem item = operation->textureItems[operation->nextTextureIndex++];
        ++operation->activeTextureTasks;
        submitItem(std::move(item), true);
    }

    const bool texturesFinished =
        operation->nextTextureIndex >= operation->textureItems.size() && operation->activeTextureTasks == 0u;
    if (texturesFinished && !operation->serialTaskActive &&
        operation->nextSerialIndex < operation->serialItems.size()) {
        QueuedImportBatch::WorkItem item = operation->serialItems[operation->nextSerialIndex++];
        operation->serialTaskActive = true;
        submitItem(std::move(item), false);
    }

    (void)finishOperationIfReady();
}

bool AssetDatabase::RequestCancelQueuedImport() {
    if (!CheckOwnerThread("queued asset import cancellation")) {
        return false;
    }
    const std::shared_ptr<QueuedImportBatch> operation = queuedImportBatch_;
    if (!operation || !operation->status.active) {
        return false;
    }
    operation->status.cancellationRequested = true;
    bool requested = false;
    for (AssetTaskId taskId : operation->status.activeTaskIds) {
        requested = assetTaskService_.RequestCancel(taskId) || requested;
    }
    ScheduleQueuedImportWork();
    return requested || operation->status.activeTaskIds.empty();
}

AssetImportBatchStatus AssetDatabase::GetQueuedImportStatus() const {
    if (!CheckOwnerThread("queued asset import status query")) {
        return {};
    }
    if (queuedImportBatch_) {
        return queuedImportBatch_->status;
    }
    return lastCompletedImportBatch_;
}

bool AssetDatabase::ConsumeCompletedImportBatch(AssetImportBatchStatus& outStatus) {
    if (!CheckOwnerThread("completed asset import consumption")) {
        return false;
    }
    if (!completedImportBatchPending_) {
        return false;
    }
    outStatus = lastCompletedImportBatch_;
    completedImportBatchPending_ = false;
    return true;
}

void AssetDatabase::PumpAssetTasks() {
    if (!CheckOwnerThread("asset task pump")) {
        return;
    }
    assetTaskService_.PumpMainThreadCompletions();
}

AssetTaskService& AssetDatabase::GetAssetTaskService() noexcept { return assetTaskService_; }

const AssetTaskService& AssetDatabase::GetAssetTaskService() const noexcept { return assetTaskService_; }

} // namespace HIKARI
