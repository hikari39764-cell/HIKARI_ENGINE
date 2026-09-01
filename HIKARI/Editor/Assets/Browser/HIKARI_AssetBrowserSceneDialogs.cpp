#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"
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

namespace HIKARI::EDITOR::ASSET_BROWSER {

#if defined(HIKARI_WITH_EDITOR)
void DrawSceneAssetModals(AssetDatabase &assetDatabase,
                          EditorSelection &selection,
                          std::string &lastOperationMessage,
                          const AssetBrowserContext *context,
                          std::string &renameSceneGuid,
                          std::string &deleteSceneGuid,
                          std::array<char, 128> &renameSceneNameBuffer) {

  bool renameOpen = true;
  if (ImGui::BeginPopupModal(
          (TEXT::Localize("ui.assetBrowser.renameTitle") +
              "###RenameSceneAsset").c_str(),
          &renameOpen,
          ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(
        TEXT::Localize("ui.assetBrowser.renameTitle").c_str());
    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputText(
        TEXT::Localize("ui.assetBrowser.name").c_str(),
        renameSceneNameBuffer.data(),
        renameSceneNameBuffer.size());
    ImGui::Separator();

    if (ImGui::Button(
            TEXT::Localize("ui.common.apply").c_str(),
            ImVec2(96.0f, 0.0f))) {
      AssetRecord *record =
          assetDatabase.FindByGuid(AssetGuid{renameSceneGuid});
      std::filesystem::path renamedPath{};
      std::string error{};
      if (record &&
          RenameSceneAsset(assetDatabase, *record, renameSceneNameBuffer.data(),
                           renamedPath, error)) {
        if (!assetDatabase.ScanAssets(true)) {
          lastOperationMessage =
              TEXT::Localize("ui.assetBrowser.assetDatabaseRefreshFailed");
        } else {
          if (const AssetRecord *renamed =
                  assetDatabase.FindByPath(renamedPath)) {
            SelectRecord(*renamed, selection);
          }
          lastOperationMessage =
              TEXT::Localize("ui.assetBrowser.sceneRenamed");
        }
      } else {
        lastOperationMessage =
            error.empty()
                ? TEXT::Localize("ui.assetBrowser.sceneRenameFailed")
                : error;
      }
      renameSceneGuid.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(
            TEXT::Localize("ui.common.cancel").c_str(),
            ImVec2(96.0f, 0.0f))) {
      renameSceneGuid.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  bool deleteOpen = true;
  if (ImGui::BeginPopupModal(
          (TEXT::Localize("ui.assetBrowser.deleteTitle") +
              "###DeleteSceneAsset").c_str(),
          &deleteOpen,
          ImGuiWindowFlags_AlwaysAutoResize)) {
    const AssetRecord *record =
        assetDatabase.FindByGuid(AssetGuid{deleteSceneGuid});
    ImGui::TextUnformatted(
        TEXT::Localize("ui.assetBrowser.deleteQuestion").c_str());
    ImGui::TextDisabled(
        "%s",
        TEXT::Localize("ui.assetBrowser.deleteHint").c_str());
    if (record) {
      ImGui::TextWrapped("%s", record->sourcePath.generic_string().c_str());
    }
    const bool deletingStartup =
        record && context && context->startupSceneGuid == record->guid;
    if (deletingStartup) {
      ImGui::TextColored(
          ImVec4(1.0f, 0.72f, 0.34f, 1.0f),
          "%s",
          TEXT::Localize(
              "ui.assetBrowser.deletingStartupWarning").c_str());
    }
    ImGui::Separator();

    const bool deletingCurrent =
        record && context && context->currentSceneGuid == record->guid;
    if (deletingCurrent) {
      ImGui::TextColored(
          ImVec4(1.0f, 0.46f, 0.34f, 1.0f),
          "%s",
          TEXT::Localize(
              "ui.assetBrowser.deleteCurrentSceneTooltip").c_str());
      ImGui::BeginDisabled();
    }
    if (ImGui::Button(
            TEXT::Localize("ui.common.delete").c_str(),
            ImVec2(96.0f, 0.0f))) {
      std::string error{};
      bool clearedStartupScene = false;
      if (record && DeleteSceneAssetToTrash(assetDatabase, *record,
                                            clearedStartupScene, error)) {
        if (!assetDatabase.ScanAssets(true)) {
          lastOperationMessage =
              TEXT::Localize("ui.assetBrowser.assetDatabaseRefreshFailed");
        } else {
          selection.selectedAssetGuid.clear();
          selection.selectedAssetPath.clear();
          lastOperationMessage =
              clearedStartupScene
                  ? TEXT::Localize(
                      "ui.assetBrowser.movedToTrashClearedStartup")
                  : TEXT::Localize("ui.assetBrowser.movedToTrash");
        }
      } else {
        lastOperationMessage =
            error.empty()
                ? TEXT::Localize("ui.assetBrowser.sceneDeleteFailed")
                : error;
      }
      deleteSceneGuid.clear();
      ImGui::CloseCurrentPopup();
    }
    if (deletingCurrent) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(
            TEXT::Localize("ui.common.cancel").c_str(),
            ImVec2(96.0f, 0.0f))) {
      deleteSceneGuid.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
#endif

} // namespace HIKARI::EDITOR::ASSET_BROWSER
