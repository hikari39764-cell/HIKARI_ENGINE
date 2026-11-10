#pragma once

#include <optional>
#include <string>

#include "Editor/Assets/Browser/HIKARI_AssetBrowserPanel.h"
#include "Editor/Assets/Inspector/HIKARI_AssetInspectorPanel.h"

namespace HIKARI {

class AssetDatabase;
class AssetRegistry;
struct EditorSelection;
struct SceneDocument;

struct ResourceWorkspaceContext {
    AssetGuid currentSceneGuid{};
    bool currentSceneDirty = false;
    uint64_t documentRevision = 0u;
    uint64_t editRevision = 0u;
};

struct ResourceImportBatchMonitor {
    int attempted = 0;
    int succeeded = 0;
    int failed = 0;
    bool hasResult = false;
    std::string label{};
    double visibleUntilSeconds = 0.0;
};

class ResourceWorkspacePanel {
public:
    void DrawContents(AssetDatabase& assetDatabase, AssetRegistry& assetRegistry, const SceneDocument& sceneDocument,
                      EditorSelection& selection, const ResourceWorkspaceContext& context) const;
    std::optional<AssetWorkspaceActivation> ConsumeActivatedAsset() const;
    std::string ConsumeSaveSceneAsGuid() const;
    std::string ConsumeRefreshRuntimeAssetGuid() const;
    std::string ConsumeReimportAndRefreshRuntimeAssetGuid() const;
    bool ConsumeApplyRuntimeMaterialRequest(AssetGuid& outGuid, PbrMaterialAssetData& outData) const;
    std::string ConsumeRefreshRuntimeMaterialGuid() const;
    bool ConsumeRefreshCurrentSceneResourcesRequested() const;
    // 後台の取り込みが終わったら結果を受け取り、場面の資源の引き直しを頼む。面板が見えていなくても毎フレーム呼ぶ。
    void PollCompletedImports(AssetDatabase& assetDatabase) const;

private:
    mutable EDITOR::ASSET_BROWSER::AssetBrowserUsageCache usageCache_{};
    mutable EDITOR::ASSET_BROWSER::AssetBrowserProjectSettingsCache projectSettingsCache_{};
    mutable EDITOR::ASSET_BROWSER::AssetBrowserStatisticsCache statisticsCache_{};
    mutable AssetBrowserPanel assetBrowserPanel_{};
    mutable AssetInspectorPanel assetInspectorPanel_{};
    mutable bool showPreviewLog_ = false;
    mutable bool showInspector_ = false;
    mutable bool refreshCurrentSceneResourcesRequested_ = false;
    mutable ResourceImportBatchMonitor importMonitor_{};
    mutable AssetBrowserScope activeScope_ = AssetBrowserScope::Project;
};

} // namespace HIKARI
