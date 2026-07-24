#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorAssetIcons.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Platform/HIKARI_Win32Window.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"

namespace HIKARI::EDITOR::ASSET_BROWSER {

#if defined(HIKARI_WITH_EDITOR)
void HandleRecordActivated(const AssetRecord &record,
                           std::string &lastOperationMessage,
                           std::string &activatedSceneGuid,
                           std::string &activatedSequenceGuid,
                           std::string &activatedAnimationStateMachineGuid,
                           std::string &activatedModelCollisionGuid) {
  if (record.type == AssetType::Scene) {
    activatedSceneGuid = record.guid.value;
    lastOperationMessage = "Scene open requested: " + record.displayName;
    LogSceneAssetInfo("open requested: " + record.sourcePath.generic_string());
    return;
  }
  if (record.type == AssetType::Sequence) {
    activatedSequenceGuid = record.guid.value;
    lastOperationMessage = "Sequence open requested: " + record.displayName;
    return;
  }
  if (record.type == AssetType::AnimationStateMachine) {
    activatedAnimationStateMachineGuid = record.guid.value;
    lastOperationMessage =
        "Animation State Machine workspace requested: " + record.displayName;
    return;
  }
  if (record.type == AssetType::Model) {
    activatedModelCollisionGuid = record.guid.value;
    lastOperationMessage =
        "Model Collision workspace requested: " + record.displayName;
    return;
  }
  lastOperationMessage = "Asset selected: " + record.displayName;
}

std::string SceneBadges(const AssetRecord &record,
                        const AssetBrowserContext *context) {
  if (!context || record.type != AssetType::Scene || !record.guid.IsValid()) {
    return {};
  }

  std::string badges{};
  if (context->currentSceneGuid == record.guid) {
    badges += "  [Current";
    if (context->currentSceneDirty) {
      badges += "*";
    }
    badges += "]";
  }
  if (context->startupSceneGuid == record.guid) {
    badges += "  [Startup]";
  }
  return badges;
}

std::string DisplayNameWithSceneBadges(const AssetRecord &record,
                                       const AssetBrowserContext *context) {
  const std::string displayName = record.displayName.empty()
                                      ? record.sourcePath.filename().string()
                                      : record.displayName;
  return displayName + SceneBadges(record, context);
}

std::string SceneDisplayNameByGuid(const AssetDatabase &assetDatabase,
                                   const AssetGuid &guid) {
  if (!guid.IsValid()) {
    return "<none>";
  }
  const AssetRecord *record = assetDatabase.FindByGuid(guid);
  if (!record) {
    return "<missing>";
  }
  return record->displayName.empty() ? record->sourcePath.filename().string()
                                     : record->displayName;
}

void QueueRenameSceneAsset(const AssetRecord &record,
                           std::string &renameSceneGuid,
                           std::array<char, 128> &renameSceneNameBuffer) {

  renameSceneGuid = record.guid.value;
  const std::string name = record.displayName.empty()
                               ? GetSceneAssetBaseName(record.sourcePath)
                               : record.displayName;
  renameSceneNameBuffer.fill('\0');
  std::snprintf(renameSceneNameBuffer.data(), renameSceneNameBuffer.size(),
                "%s", name.c_str());
  ImGui::OpenPopup("Rename Scene Asset");
}

void QueueDeleteSceneAsset(const AssetRecord &record,
                           std::string &deleteSceneGuid) {

  deleteSceneGuid = record.guid.value;
  ImGui::OpenPopup("Delete Scene Asset");
}

void DrawAssetDragSource(const AssetRecord &record) {
  EDITOR::BeginAssetDragSource(record);
}

void DrawRecordContextMenu(
    AssetDatabase &assetDatabase, const AssetRecord &record,
    EditorSelection &selection, std::string &lastOperationMessage,
    const AssetBrowserContext *context, std::string &activatedSceneGuid,
    std::string &activatedSequenceGuid,
    std::string &activatedAnimationStateMachineGuid,
    std::string &activatedModelCollisionGuid, std::string &saveSceneAsGuid,
    std::string &refreshRuntimeAssetGuid,
    std::string &reimportAndRefreshRuntimeAssetGuid,
    std::string &pendingReimportAndRefreshRuntimeAssetGuid,
    std::string &renameSceneGuid, std::string &deleteSceneGuid,
    std::array<char, 128> &renameSceneNameBuffer) {

  if (record.type == AssetType::Scene) {
    if (ImGui::MenuItem("Open Scene")) {
      SelectRecord(record, selection);
      activatedSceneGuid = record.guid.value;
      lastOperationMessage = "Scene open requested: " + record.displayName;
      LogSceneAssetInfo("open requested: " +
                        record.sourcePath.generic_string());
    }
    if (ImGui::MenuItem("Save Current Scene Here")) {
      saveSceneAsGuid = record.guid.value;
      lastOperationMessage = "Scene save requested: " + record.displayName;
      LogSceneAssetInfo("save requested: " +
                        record.sourcePath.generic_string());
    }
    if (ImGui::MenuItem("Duplicate Scene")) {
      std::filesystem::path duplicatedPath{};
      std::string error{};
      if (DuplicateSceneAsset(assetDatabase, record, duplicatedPath, error)) {
        assetDatabase.ScanAssets(true);
        if (const AssetRecord *duplicated =
                assetDatabase.FindByPath(duplicatedPath)) {
          SelectRecord(*duplicated, selection);
        }
        lastOperationMessage = "Scene duplicated";
      } else {
        lastOperationMessage = error.empty() ? "Scene duplicate failed" : error;
      }
    }
    if (ImGui::MenuItem("Rename Scene")) {
      QueueRenameSceneAsset(record, renameSceneGuid, renameSceneNameBuffer);
    }
    const bool isCurrentScene =
        context && context->currentSceneGuid == record.guid;
    if (ImGui::MenuItem("Delete Scene", nullptr, false, !isCurrentScene)) {
      QueueDeleteSceneAsset(record, deleteSceneGuid);
    }
    if (isCurrentScene &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Open another scene before deleting this one.");
    }
    if (ImGui::MenuItem("Set as Startup Scene")) {
      ProjectSettingsService settings{};
      settings.Load(assetDatabase.GetProjectRoot());
      if (settings.SetStartupSceneGuid(record.guid) && settings.Save()) {
        lastOperationMessage = "Startup scene set: " + record.displayName;
        LogSceneAssetInfo("set startup scene: " +
                          record.sourcePath.generic_string());
      } else {
        lastOperationMessage = "Startup scene update failed";
        LogSceneAssetWarn("set startup scene failed: " +
                          record.sourcePath.generic_string());
      }
    }
    ImGui::Separator();
  }

  if (record.type == AssetType::Model) {
    if (ImGui::MenuItem("Edit Collision...")) {
      SelectRecord(record, selection);
      activatedModelCollisionGuid = record.guid.value;
      lastOperationMessage =
          "Model Collision workspace requested: " + record.displayName;
    }
    ImGui::Separator();
  }

  if (record.type == AssetType::Sequence) {
    if (ImGui::MenuItem("Open in Cinematics")) {
      SelectRecord(record, selection);
      activatedSequenceGuid = record.guid.value;
      lastOperationMessage = "Sequence open requested: " + record.displayName;
    }
    ImGui::Separator();
  }

  if (record.type == AssetType::AnimationStateMachine) {
    if (ImGui::MenuItem("Open State Machine")) {
      SelectRecord(record, selection);
      activatedAnimationStateMachineGuid = record.guid.value;
      lastOperationMessage =
          "Animation State Machine workspace requested: " + record.displayName;
    }
    ImGui::Separator();
  }

  if (ImGui::MenuItem("Reimport")) {
    SelectRecord(record, selection);
    const bool queued = assetDatabase.QueueImportAssets(
        {record.guid}, "Reimport " + record.displayName);
    lastOperationMessage =
        queued ? "Reimport queued" : "Another import batch is already running";
  }
  if (ImGui::MenuItem("Reimport + Refresh Runtime")) {
    SelectRecord(record, selection);
    const bool queued = assetDatabase.QueueImportAssets(
        {record.guid}, "Reimport " + record.displayName);
    if (queued) {
      pendingReimportAndRefreshRuntimeAssetGuid = record.guid.value;
    }
    lastOperationMessage = queued
                               ? "Reimport queued; runtime refresh will follow"
                               : "Another import batch is already running";
  }
  if (ImGui::MenuItem("Refresh Runtime Only")) {
    SelectRecord(record, selection);
    refreshRuntimeAssetGuid = record.guid.value;
    lastOperationMessage = "Runtime refresh queued";
  }
  if (ImGui::MenuItem("Reimport Dependencies")) {
    const bool queued =
        assetDatabase.QueueImportDependencies(record.guid, false);
    lastOperationMessage = queued ? "Dependency import queued"
                                  : "Another import batch is already running";
  }
  if (ImGui::MenuItem("Show in Explorer")) {
    EDITOR::SHELL::ShowFileInExplorer(assetDatabase.GetProjectRoot() /
                                      record.sourcePath);
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Copy GUID")) {
    ImGui::SetClipboardText(record.guid.value.c_str());
    lastOperationMessage = "Asset GUID copied";
  }
  if (ImGui::MenuItem("Copy Source Path")) {
    const std::string path = record.sourcePath.generic_string();
    ImGui::SetClipboardText(path.c_str());
    lastOperationMessage = "Source path copied";
  }
  const std::string artifactPath = FirstArtifactPath(record);
  if (ImGui::MenuItem("Copy Artifact Path", nullptr, false,
                      !artifactPath.empty())) {
    ImGui::SetClipboardText(artifactPath.c_str());
    lastOperationMessage = "Artifact path copied";
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Regenerate Meta")) {
    SelectRecord(record, selection);
    const bool ok = assetDatabase.RegenerateMeta(record.sourcePath);
    lastOperationMessage = ok ? "Meta regenerated" : "Meta regeneration failed";
  }
}
#endif

} // namespace HIKARI::EDITOR::ASSET_BROWSER
