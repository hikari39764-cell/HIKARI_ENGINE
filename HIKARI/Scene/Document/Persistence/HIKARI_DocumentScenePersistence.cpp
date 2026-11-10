#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <chrono>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <utility>

#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

namespace HIKARI {
namespace {

// 場景の隣の格子フォルダへ、変わった格子の実例集を書く。
bool SaveSceneInstances(WorldInstanceStore& instances, const std::filesystem::path& scenePath) {
    WorldInstanceStore::SaveReport report{};
    std::string error{};
    if (!instances.Save(ASSETS::SEMANTICS::MakeSceneCellDirectoryPath(scenePath), report, error)) {
        HIKARI_LOG_ERROR("[SceneAsset] scene instances could not be saved: " + error);
        return false;
    }
    if (report.writtenCells != 0u || report.removedCells != 0u) {
        HIKARI_LOG_INFO("[SceneAsset] saved scene instances: cells written=" + std::to_string(report.writtenCells) +
                        " removed=" + std::to_string(report.removedCells));
    }
    return true;
}
// 分区場景は何を書いたかを一行残す（保存が変わった格子だけに留まっているかを後から確かめられるように）。
void LogPartitionedSave(const SceneDocument& document, const SceneSaveReport& report) {
    if (!document.partition.enabled) {
        return;
    }
    HIKARI_LOG_INFO("[SceneAsset] saved partitioned scene: cells written=" +
                    std::to_string(report.writtenCells.size()) + " removed=" + std::to_string(report.removedCellFiles) +
                    " main=" + (report.mainWritten ? "written" : "unchanged"));
}
} // namespace

bool DocumentSceneBase::OpenSceneAssetNow(const AssetGuid& sceneGuid) {
    if (!IsValidAssetGuid(sceneGuid.value)) {
        return false;
    }
    if (!SERVICES::IsRuntimeSceneGuidAllowed(sceneGuid.value)) {
        HIKARI_LOG_WARN("[SceneAsset] blocked scene outside runtime export set: " + sceneGuid.value);
        return false;
    }
    // ReloadAssets は Database のレコードを置き換えるため、借用した GUID を保持しない。
    const AssetGuid requestedGuid = sceneGuid;
    return ReloadAssets() && OpenSceneAssetWithCurrentAssets(requestedGuid);
}

bool DocumentSceneBase::OpenSceneAssetWithCurrentAssets(const AssetGuid& sceneGuid,
                                                        const std::filesystem::path& contentPath) {
    state_->identity.loadError.clear();
    if (!IsValidAssetGuid(sceneGuid.value) || !SERVICES::IsRuntimeSceneGuidAllowed(sceneGuid.value)) {
        return false;
    }
    if (state_->assets.Database().GetProjectRoot().empty()) {
        return false;
    }

    const AssetRecord* record = state_->assets.Database().FindByGuid(sceneGuid);
    if (!record || record->duplicateGuid || record->type != AssetType::Scene || record->sourcePath.empty()) {
        state_->identity.loadError = "Scene asset unavailable: " + sceneGuid.value;
        HIKARI_LOG_ERROR(state_->identity.loadError);
        return false;
    }

    const std::filesystem::path scenePath = state_->assets.Database().ResolveSourcePath(*record);
    SceneDocument loaded{};
    SceneCellFileState loadedCells{};
    const std::filesystem::path contentSource = contentPath.empty() ? scenePath : contentPath;
    if (!state_->identity.serializer.LoadFromFile(contentSource.generic_string(), loaded, &loadedCells)) {
        state_->identity.loadError = "Scene could not be read: " + contentSource.generic_string();
        HIKARI_LOG_ERROR(state_->identity.loadError);
        return false;
    }

    WorldInstanceStore loadedInstances{};
    std::string instanceError{};
    const auto instancesStarted = std::chrono::steady_clock::now();
    if (!loadedInstances.Load(ASSETS::SEMANTICS::MakeSceneCellDirectoryPath(scenePath), instanceError)) {
        state_->identity.loadError = "Scene instances could not be read: " + instanceError;
        HIKARI_LOG_ERROR(state_->identity.loadError);
        return false;
    }
    if (loadedInstances.TotalCount() != 0u) {
        const auto elapsed = std::chrono::steady_clock::now() - instancesStarted;
        HIKARI_LOG_INFO("[SceneAsset] loaded scene instances: " + std::to_string(loadedInstances.TotalCount()) +
                        " in " +
                        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) + " ms");
    }

    SceneDocument previousDocument = state_->identity.document;
    SceneCellFileState previousCells = state_->identity.cells;
    WorldInstanceStore previousInstances = std::move(state_->identity.instances);
    const uint64_t previousDocumentRevision = state_->identity.documentRevision;
    std::string previousScenePath = state_->identity.scenePath;
    std::string previousSceneId = state_->identity.sceneId;
    AssetGuid previousSceneAssetGuid = state_->identity.currentSceneAssetGuid;
    const bool previousDocumentDirty = state_->identity.documentDirty;
    if (!state_->runtime.playActive) {
        EndEditorCameraPreview();
    }
    state_->identity.document = std::move(loaded);
    state_->identity.cells = std::move(loadedCells);
    state_->identity.instances = std::move(loadedInstances);
    ++state_->identity.documentRevision;
    state_->identity.scenePath = scenePath.generic_string();
    state_->identity.sceneId = sceneGuid.value;
    state_->identity.currentSceneAssetGuid = sceneGuid;
    state_->identity.documentDirty = false;

    bool oldWorldDetached = false;
    if (RebuildRuntimeWorld(false, true, &oldWorldDetached)) {
        state_->identity.loadError.clear();
        if (!contentPath.empty())
            HIKARI_LOG_INFO("[SceneAsset] opened " + sceneGuid.value + " from snapshot " +
                            contentSource.generic_string());
        return true;
    }

    const std::string loadError = state_->identity.loadError.empty()
                                      ? "Scene runtime could not be created: " + scenePath.generic_string()
                                      : state_->identity.loadError;

    state_->identity.document = std::move(previousDocument);
    state_->identity.cells = std::move(previousCells);
    state_->identity.instances = std::move(previousInstances);
    state_->identity.documentRevision = previousDocumentRevision;
    state_->identity.scenePath = std::move(previousScenePath);
    state_->identity.sceneId = std::move(previousSceneId);
    state_->identity.currentSceneAssetGuid = std::move(previousSceneAssetGuid);
    state_->identity.documentDirty = previousDocumentDirty;
    if (oldWorldDetached && !RebuildRuntimeWorld(false, true)) {
        HIKARI_LOG_ERROR("[SceneAsset] failed to restore the previous runtime World "
                         "after Scene open failed.");
        if (state_->runtime.playActive && !EndRuntimePlay()) {
            HIKARI_LOG_ERROR("[RuntimeLifecycle] Play was stopped after runtime Scene restoration failed.");
        }
    }
    state_->identity.loadError = loadError;
    return false;
}
bool DocumentSceneBase::OpenStartupSceneAsset() { return ReloadAssets() && OpenStartupSceneAssetWithCurrentAssets(); }

bool DocumentSceneBase::OpenStartupSceneAssetWithCurrentAssets() {
    if (state_->assets.Database().GetProjectRoot().empty()) {
        return false;
    }

    // 実行時の明示的な開始シーンを優先する。
    const std::string& runtimeStartupSceneGuid = SERVICES::GetRuntimeStartupSceneGuid();
    if (!runtimeStartupSceneGuid.empty()) {
        return OpenSceneAssetWithCurrentAssets(AssetGuid{runtimeStartupSceneGuid},
                                               TEXT::PathFromUtf8(SERVICES::GetRuntimeStartupSceneSnapshot()));
    }

    ProjectSettingsService settings{};
    settings.Load(state_->assets.Database().GetProjectRoot());

    const AssetGuid startupGuid = settings.GetSettings().startupSceneGuid;
    // 指定した開始シーンの故障を、別シーンへの切替と設定保存で隠さない。
    if (IsValidAssetGuid(startupGuid.value)) {
        return OpenSceneAssetWithCurrentAssets(startupGuid);
    }

    std::vector<const AssetRecord*> sceneRecords = state_->assets.Database().CollectByType(AssetType::Scene);
    std::sort(sceneRecords.begin(), sceneRecords.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
        if (!lhs || !rhs) {
            return lhs < rhs;
        }
        return lhs->sourcePath.generic_string() < rhs->sourcePath.generic_string();
    });

    std::vector<AssetGuid> sceneGuids;
    sceneGuids.reserve(sceneRecords.size());
    for (const AssetRecord* record : sceneRecords) {
        if (!record || !IsValidAssetGuid(record->guid.value)) {
            continue;
        }
        sceneGuids.push_back(record->guid);
    }
    // シーン依存のインポート中に Database が更新されても候補は有効に保つ。
    for (const AssetGuid& sceneGuid : sceneGuids) {
        if (!SERVICES::IsRuntimeSceneGuidAllowed(sceneGuid.value)) {
            continue;
        }
        if (!OpenSceneAssetWithCurrentAssets(sceneGuid)) {
            continue;
        }
        if (SERVICES::IsEditorHost()) {
            (void)settings.SetStartupSceneGuid(sceneGuid);
            (void)settings.Save();
        }
        return true;
    }

    return false;
}
bool DocumentSceneBase::CreateTransientEmptySceneDocument() {
    EndEditorCameraPreview();
    state_->camera.sequencePlayback.Reset();
    state_->camera.currentCameraSequenceHandle = {};
    state_->camera.director.Reset();
    state_->camera.rigService.Clear();
    state_->identity.document = SceneDocument{};
    ++state_->identity.documentRevision;
    state_->identity.document.sceneName = "Untitled Scene";
    // デフォルトの未保存シーンには実体のグリッド床を置く。
    if (!state_->assets.Database().GetProjectRoot().empty()) {
        state_->identity.document.objects.push_back(BuildDefaultGridFloorSceneObject(state_->identity.document));
    }
    state_->identity.document.systems = state_->runtime.featureCatalog.CreateDefaultSceneSystems();
    ResetEnvironmentRuntime();
    state_->identity.scenePath.clear();
    state_->identity.sceneId = "TransientScene";
    state_->identity.currentSceneAssetGuid = {};
    state_->identity.documentDirty = false;
    RenderSubmissionSystem::InvalidateSceneResources(true);
    state_->camera.director.SetBaseCamera({});
    return true;
}
bool DocumentSceneBase::HasUnsavedSceneChanges() const {
    return state_->identity.documentDirty || state_->identity.instances.HasUnsavedChanges();
}
void DocumentSceneBase::SetUnsavedSceneChanges(bool dirty) { state_->identity.documentDirty = dirty; }
bool DocumentSceneBase::SaveCurrentSceneDocument() {
    if (!state_->identity.currentSceneAssetGuid.IsValid() || state_->identity.scenePath.empty()) {
        return false;
    }

    SceneSaveReport report{};
    const bool saved = state_->identity.serializer.SaveToFile(state_->identity.scenePath, state_->identity.document,
                                                              &state_->identity.cells, &report);
    if (saved) {
        state_->identity.documentDirty = false;
        LogPartitionedSave(state_->identity.document, report);
    }
    return saved && SaveSceneInstances(state_->identity.instances, state_->identity.scenePath);
}
bool DocumentSceneBase::SaveCurrentSceneDocumentAs(const AssetGuid& sceneGuid) {
    if (!IsValidAssetGuid(sceneGuid.value)) {
        return false;
    }
    if (state_->assets.Database().GetProjectRoot().empty()) {
        if (!state_->assets.Database().Initialize({})) {
            return false;
        }
    }

    const AssetRecord* record = state_->assets.Database().FindByGuid(sceneGuid);
    if (!record) {
        if (!state_->assets.Database().ScanAssets(false)) {
            return false;
        }
        record = state_->assets.Database().FindByGuid(sceneGuid);
    }
    if (!record || record->duplicateGuid || record->type != AssetType::Scene || record->sourcePath.empty()) {
        return false;
    }

    const std::filesystem::path absolutePath = state_->assets.Database().ResolveSourcePath(*record);
    SceneSaveReport report{};
    if (!state_->identity.serializer.SaveToFile(absolutePath.generic_string(), state_->identity.document,
                                                &state_->identity.cells, &report)) {
        return false;
    }
    LogPartitionedSave(state_->identity.document, report);
    if (!SaveSceneInstances(state_->identity.instances, absolutePath)) {
        return false;
    }

    state_->identity.scenePath = absolutePath.generic_string();
    RuntimeSceneContext::SetCurrentSceneFilePath(state_->identity.scenePath);
    state_->identity.sceneId = sceneGuid.value;
    state_->identity.currentSceneAssetGuid = sceneGuid;
    state_->identity.documentDirty = false;
    return true;
}
const AssetGuid& DocumentSceneBase::GetCurrentSceneAssetGuid() const { return state_->identity.currentSceneAssetGuid; }
std::string DocumentSceneBase::GetCurrentSceneDisplayName() const {
    if (state_->identity.currentSceneAssetGuid.IsValid()) {
        if (const AssetRecord* record = state_->assets.Database().FindByGuid(state_->identity.currentSceneAssetGuid)) {
            if (!record->displayName.empty()) {
                return record->displayName;
            }
        }
    }
    return state_->identity.document.sceneName;
}

} // namespace HIKARI
