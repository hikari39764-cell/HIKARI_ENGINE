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
  if (ImGui::BeginPopupModal("Rename Scene Asset", &renameOpen,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Rename Scene Asset");
    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputText("Name", renameSceneNameBuffer.data(),
                     renameSceneNameBuffer.size());
    ImGui::Separator();

    if (ImGui::Button("Apply", ImVec2(96.0f, 0.0f))) {
      AssetRecord *record =
          assetDatabase.FindByGuid(AssetGuid{renameSceneGuid});
      std::filesystem::path renamedPath{};
      std::string error{};
      if (record &&
          RenameSceneAsset(assetDatabase, *record, renameSceneNameBuffer.data(),
                           renamedPath, error)) {
        assetDatabase.ScanAssets(true);
        if (const AssetRecord *renamed =
                assetDatabase.FindByPath(renamedPath)) {
          SelectRecord(*renamed, selection);
        }
        lastOperationMessage = "Scene renamed";
      } else {
        lastOperationMessage = error.empty() ? "Scene rename failed" : error;
      }
      renameSceneGuid.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
      renameSceneGuid.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  bool deleteOpen = true;
  if (ImGui::BeginPopupModal("Delete Scene Asset", &deleteOpen,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    const AssetRecord *record =
        assetDatabase.FindByGuid(AssetGuid{deleteSceneGuid});
    ImGui::TextUnformatted("Delete Scene Asset?");
    ImGui::TextDisabled(
        "The source and meta file will be moved to Library/Trash.");
    if (record) {
      ImGui::TextWrapped("%s", record->sourcePath.generic_string().c_str());
    }
    const bool deletingStartup =
        record && context && context->startupSceneGuid == record->guid;
    if (deletingStartup) {
      ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.34f, 1.0f),
                         "This scene is the startup scene. Deleting it will "
                         "clear startupSceneGuid.");
    }
    ImGui::Separator();

    const bool deletingCurrent =
        record && context && context->currentSceneGuid == record->guid;
    if (deletingCurrent) {
      ImGui::TextColored(ImVec4(1.0f, 0.46f, 0.34f, 1.0f),
                         "Open another scene before deleting this one.");
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete", ImVec2(96.0f, 0.0f))) {
      std::string error{};
      bool clearedStartupScene = false;
      if (record && DeleteSceneAssetToTrash(assetDatabase, *record,
                                            clearedStartupScene, error)) {
        assetDatabase.ScanAssets(true);
        selection.selectedAssetGuid.clear();
        selection.selectedAssetPath.clear();
        lastOperationMessage =
            clearedStartupScene
                ? "Scene moved to Library/Trash; startup scene cleared"
                : "Scene moved to Library/Trash";
      } else {
        lastOperationMessage = error.empty() ? "Scene delete failed" : error;
      }
      deleteSceneGuid.clear();
      ImGui::CloseCurrentPopup();
    }
    if (deletingCurrent) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
      deleteSceneGuid.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
#endif

} // namespace HIKARI::EDITOR::ASSET_BROWSER
