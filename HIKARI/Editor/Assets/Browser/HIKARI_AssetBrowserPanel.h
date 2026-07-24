#pragma once

#include <array>
#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

class AssetDatabase;
struct AssetUsageSummary;
struct EditorSelection;

enum class AssetBrowserScope {
  Project,
  CurrentScene,
  UnusedInScene,
  Broken,
  Textures,
  Models,
  Scenes,
  Materials,
  Skies,
  Vfx,
  Sequences,
};

struct AssetBrowserContext {
  AssetGuid currentSceneGuid{};
  AssetGuid startupSceneGuid{};
  bool currentSceneDirty = false;
};

class AssetBrowserPanel {
public:
  void Draw(AssetDatabase &assetDatabase, EditorSelection &selection) const;
  void DrawContents(AssetDatabase &assetDatabase,
                    EditorSelection &selection) const;
  void DrawContents(AssetDatabase &assetDatabase, EditorSelection &selection,
                    const AssetUsageSummary *usageSummary,
                    AssetBrowserScope scope,
                    const AssetBrowserContext *context = nullptr) const;
  std::string ConsumeActivatedSceneGuid() const;
  std::string ConsumeActivatedSequenceGuid() const;
  std::string ConsumeActivatedAnimationStateMachineGuid() const;
  std::string ConsumeActivatedModelCollisionGuid() const;
  std::string ConsumeSaveSceneAsGuid() const;
  std::string ConsumeRefreshRuntimeAssetGuid() const;
  std::string ConsumeReimportAndRefreshRuntimeAssetGuid() const;
  std::filesystem::path CurrentDirectory() const;
  bool IsRecursiveEnabled() const;

private:
  mutable std::filesystem::path currentDirectory_{"Assets"};
  mutable std::array<char, 128> searchBuffer_{};
  mutable int typeFilter_ = 0;
  mutable int stateFilter_ = 0;
  mutable int viewMode_ = 2;
  mutable bool recursive_ = false;
  mutable bool filtersExpanded_ = false;
  mutable std::string lastOperationMessage_{};
  mutable std::string observedOperationMessage_{};
  mutable double operationMessageVisibleUntil_ = 0.0;
  mutable std::string activatedSceneGuid_{};
  mutable std::string activatedSequenceGuid_{};
  mutable std::string activatedAnimationStateMachineGuid_{};
  mutable std::string activatedModelCollisionGuid_{};
  mutable std::string saveSceneAsGuid_{};
  mutable std::string refreshRuntimeAssetGuid_{};
  mutable std::string reimportAndRefreshRuntimeAssetGuid_{};
  mutable std::string pendingReimportAndRefreshRuntimeAssetGuid_{};
  mutable std::string renameSceneGuid_{};
  mutable std::string deleteSceneGuid_{};
  mutable std::array<char, 128> renameSceneNameBuffer_{};
};

} // namespace HIKARI
