#pragma once

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
  AssetGuid startupSceneGuid{};
  bool currentSceneDirty = false;
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
  void Draw(AssetDatabase &assetDatabase, AssetRegistry &assetRegistry,
            const SceneDocument &sceneDocument, EditorSelection &selection,
            const ResourceWorkspaceContext &context) const;
  std::string ConsumeActivatedSceneGuid() const;
  std::string ConsumeActivatedSequenceGuid() const;
  std::string ConsumeActivatedAnimationStateMachineGuid() const;
  std::string ConsumeActivatedModelCollisionGuid() const;
  std::string ConsumeSaveSceneAsGuid() const;
  std::string ConsumeRefreshRuntimeAssetGuid() const;
  std::string ConsumeReimportAndRefreshRuntimeAssetGuid() const;
  bool ConsumeApplyRuntimeMaterialRequest(AssetGuid &outGuid,
                                          PbrMaterialAssetData &outData) const;
  std::string ConsumeRefreshRuntimeMaterialGuid() const;
  bool ConsumeRefreshCurrentSceneResourcesRequested() const;

private:
  mutable AssetBrowserPanel assetBrowserPanel_{};
  mutable AssetInspectorPanel assetInspectorPanel_{};
  mutable bool showPreviewLog_ = false;
  mutable bool showInspector_ = false;
  mutable bool refreshCurrentSceneResourcesRequested_ = false;
  mutable ResourceImportBatchMonitor importMonitor_{};
  mutable AssetBrowserScope activeScope_ = AssetBrowserScope::Project;
};

} // namespace HIKARI
