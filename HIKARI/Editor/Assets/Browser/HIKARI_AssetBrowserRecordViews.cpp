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
void DrawRecordList(
    AssetDatabase &assetDatabase,
    const std::vector<const AssetRecord *> &records, EditorSelection &selection,
    const AssetUsageSummary *usageSummary, std::string &lastOperationMessage,
    const AssetBrowserContext *context, std::string &activatedSceneGuid,
    std::string &activatedSequenceGuid,
    std::string &activatedAnimationStateMachineGuid,
    std::string &activatedModelCollisionGuid, std::string &saveSceneAsGuid,
    std::string &refreshRuntimeAssetGuid,
    std::string &reimportAndRefreshRuntimeAssetGuid,
    std::string &pendingReimportAndRefreshRuntimeAssetGuid,
    std::string &renameSceneGuid, std::string &deleteSceneGuid,
    std::array<char, 128> &renameSceneNameBuffer) {

  if (!ImGui::BeginTable("AssetBrowserTable", 6,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                             ImGuiTableFlags_Resizable |
                             ImGuiTableFlags_ScrollY)) {
    return;
  }

  ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
  ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed, 74.0f);
  ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 130.0f);
  ImGui::TableSetupColumn("Cooked", ImGuiTableColumnFlags_WidthFixed, 92.0f);
  ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableHeadersRow();

  for (const AssetRecord *record : records) {
    if (!record) {
      continue;
    }

    ImGui::PushID(record->guid.IsValid()
                      ? record->guid.value.c_str()
                      : record->sourcePath.generic_string().c_str());
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const bool isSelected = selection.selectedAssetGuid == record->guid.value;
    EDITOR::DrawAssetTypeGlyph(record->type);
    ImGui::SameLine();
    const std::string label = DisplayNameWithSceneBadges(*record, context) +
                              "##" + record->sourcePath.generic_string();
    if (ImGui::Selectable(label.c_str(), isSelected,
                          ImGuiSelectableFlags_SpanAllColumns)) {
      SelectRecord(*record, selection);
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        HandleRecordActivated(*record, lastOperationMessage, activatedSceneGuid,
                              activatedSequenceGuid,
                              activatedAnimationStateMachineGuid,
                              activatedModelCollisionGuid);
      }
    }
    const bool rowHovered = ImGui::IsItemHovered();
    DrawAssetDragSource(*record);
    if (rowHovered) {
      DrawRecordTooltip(*record);
    }

    if (ImGui::BeginPopupContextItem()) {
      DrawRecordContextMenu(
          assetDatabase, *record, selection, lastOperationMessage, context,
          activatedSceneGuid, activatedSequenceGuid,
          activatedAnimationStateMachineGuid, activatedModelCollisionGuid,
          saveSceneAsGuid, refreshRuntimeAssetGuid,
          reimportAndRefreshRuntimeAssetGuid,
          pendingReimportAndRefreshRuntimeAssetGuid, renameSceneGuid,
          deleteSceneGuid, renameSceneNameBuffer);
      ImGui::EndPopup();
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(ToAssetTypeText(record->type));
    ImGui::TableSetColumnIndex(2);
    ImGui::TextDisabled("%s", ToUsageBadge(*record, usageSummary));
    ImGui::TableSetColumnIndex(3);
    const AssetImportState state = GetImportState(*record);
    ImGui::TextColored(StateColor(state), "%s", ToString(state));
    ImGui::TableSetColumnIndex(4);
    ImGui::TextDisabled("%s", ToCookedBadge(*record));
    ImGui::TableSetColumnIndex(5);
    ImGui::TextUnformatted(record->sourcePath.generic_string().c_str());
    ImGui::PopID();
  }

  ImGui::EndTable();
}

void DrawRecordCompactRows(
    AssetDatabase &assetDatabase,
    const std::vector<const AssetRecord *> &records, EditorSelection &selection,
    const AssetUsageSummary *usageSummary, std::string &lastOperationMessage,
    const AssetBrowserContext *context, std::string &activatedSceneGuid,
    std::string &activatedSequenceGuid,
    std::string &activatedAnimationStateMachineGuid,
    std::string &activatedModelCollisionGuid, std::string &saveSceneAsGuid,
    std::string &refreshRuntimeAssetGuid,
    std::string &reimportAndRefreshRuntimeAssetGuid,
    std::string &pendingReimportAndRefreshRuntimeAssetGuid,
    std::string &renameSceneGuid, std::string &deleteSceneGuid,
    std::array<char, 128> &renameSceneNameBuffer) {

  if (!ImGui::BeginTable("AssetBrowserCompactRows", 2,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_SizingStretchProp)) {
    return;
  }

  ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Badges", ImGuiTableColumnFlags_WidthFixed, 230.0f);

  for (const AssetRecord *record : records) {
    if (!record) {
      continue;
    }

    ImGui::PushID(record->guid.IsValid()
                      ? record->guid.value.c_str()
                      : record->sourcePath.generic_string().c_str());

    ImGui::TableNextRow(0, ImGui::GetFrameHeightWithSpacing());
    ImGui::TableSetColumnIndex(0);
    const bool isSelected = selection.selectedAssetGuid == record->guid.value;
    const std::string displayName =
        DisplayNameWithSceneBadges(*record, context);
    EDITOR::DrawAssetTypeGlyph(record->type);
    ImGui::SameLine();
    const std::string label =
        displayName + "##compact_" + record->sourcePath.generic_string();
    if (ImGui::Selectable(label.c_str(), isSelected,
                          ImGuiSelectableFlags_SpanAllColumns |
                              ImGuiSelectableFlags_AllowDoubleClick)) {
      SelectRecord(*record, selection);
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        HandleRecordActivated(*record, lastOperationMessage, activatedSceneGuid,
                              activatedSequenceGuid,
                              activatedAnimationStateMachineGuid,
                              activatedModelCollisionGuid);
      }
    }
    const bool rowHovered = ImGui::IsItemHovered();
    DrawAssetDragSource(*record);
    if (rowHovered) {
      DrawRecordTooltip(*record);
    }
    if (ImGui::BeginPopupContextItem()) {
      DrawRecordContextMenu(
          assetDatabase, *record, selection, lastOperationMessage, context,
          activatedSceneGuid, activatedSequenceGuid,
          activatedAnimationStateMachineGuid, activatedModelCollisionGuid,
          saveSceneAsGuid, refreshRuntimeAssetGuid,
          reimportAndRefreshRuntimeAssetGuid,
          pendingReimportAndRefreshRuntimeAssetGuid, renameSceneGuid,
          deleteSceneGuid, renameSceneNameBuffer);
      ImGui::EndPopup();
    }

    const AssetImportState state = GetImportState(*record);
    const char *usageBadge = ToCompactUsageBadge(*record, usageSummary);
    const char *cookedBadge = ToCompactCookedBadge(*record);

    ImGui::TableSetColumnIndex(1);
    DrawCompactBadge(ToCompactTypeBadge(record->type),
                     TypeBadgeColor(record->type));
    ImGui::SameLine(0.0f, 6.0f);
    DrawCompactBadge(usageBadge, UsageBadgeColor(usageBadge));
    ImGui::SameLine(0.0f, 6.0f);
    DrawCompactBadge(ToCompactStateBadge(state), StateColor(state));
    ImGui::SameLine(0.0f, 6.0f);
    DrawCompactBadge(cookedBadge, CookedBadgeColor(cookedBadge));

    ImGui::PopID();
  }

  ImGui::EndTable();
}

void DrawRecordGrid(
    AssetDatabase &assetDatabase,
    const std::vector<const AssetRecord *> &records, EditorSelection &selection,
    const AssetUsageSummary *usageSummary, std::string &lastOperationMessage,
    const AssetBrowserContext *context, std::string &activatedSceneGuid,
    std::string &activatedSequenceGuid,
    std::string &activatedAnimationStateMachineGuid,
    std::string &activatedModelCollisionGuid, std::string &saveSceneAsGuid,
    std::string &refreshRuntimeAssetGuid,
    std::string &reimportAndRefreshRuntimeAssetGuid,
    std::string &pendingReimportAndRefreshRuntimeAssetGuid,
    std::string &renameSceneGuid, std::string &deleteSceneGuid,
    std::array<char, 128> &renameSceneNameBuffer) {

  (void)usageSummary;

  const float cardWidth = 132.0f;
  const float cardHeight = 116.0f;
  const float iconSize = 46.0f;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float availableWidth =
      (std::max)(cardWidth, ImGui::GetContentRegionAvail().x);
  const int columns =
      (std::max)(1, static_cast<int>(availableWidth / (cardWidth + spacing)));

  if (!ImGui::BeginTable("AssetBrowserGrid", columns,
                         ImGuiTableFlags_SizingFixedFit)) {
    return;
  }

  const EDITOR::EditorThemePalette &theme = EDITOR::GetEditorThemePalette();
  int column = 0;
  for (const AssetRecord *record : records) {
    if (!record) {
      continue;
    }

    if (column == 0) {
      ImGui::TableNextRow();
    }
    ImGui::TableSetColumnIndex(column);

    ImGui::PushID(record->guid.IsValid()
                      ? record->guid.value.c_str()
                      : record->sourcePath.generic_string().c_str());

    const bool isSelected = selection.selectedAssetGuid == record->guid.value;

    const ImVec2 cardMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##AssetGridCard", ImVec2(cardWidth, cardHeight),
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool cardHovered = ImGui::IsItemHovered();
    const bool cardClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool cardDoubleClicked =
        cardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const ImVec2 cardMax = ImGui::GetItemRectMax();
    const ImVec2 cursorAfterCard = ImGui::GetCursorScreenPos();

    if (cardClicked) {
      SelectRecord(*record, selection);
    }
    DrawAssetDragSource(*record);
    if (cardHovered) {
      DrawRecordTooltip(*record);
    }
    if (cardDoubleClicked) {
      SelectRecord(*record, selection);
      HandleRecordActivated(*record, lastOperationMessage, activatedSceneGuid,
                            activatedSequenceGuid,
                            activatedAnimationStateMachineGuid,
                            activatedModelCollisionGuid);
    }
    if (ImGui::BeginPopupContextItem()) {
      DrawRecordContextMenu(
          assetDatabase, *record, selection, lastOperationMessage, context,
          activatedSceneGuid, activatedSequenceGuid,
          activatedAnimationStateMachineGuid, activatedModelCollisionGuid,
          saveSceneAsGuid, refreshRuntimeAssetGuid,
          reimportAndRefreshRuntimeAssetGuid,
          pendingReimportAndRefreshRuntimeAssetGuid, renameSceneGuid,
          deleteSceneGuid, renameSceneNameBuffer);
      ImGui::EndPopup();
    }

    const ImVec4 baseColor =
        isSelected ? ImVec4(theme.accent.x * 0.42f, theme.accent.y * 0.42f,
                            theme.accent.z * 0.42f, 1.0f)
                   : (cardHovered ? theme.raisedHover : theme.raised);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(cardMin, cardMax, ImGui::GetColorU32(baseColor),
                            6.0f);
    drawList->AddRect(
        cardMin, cardMax,
        ImGui::GetColorU32(isSelected ? theme.accent : theme.border), 6.0f, 0,
        isSelected ? 2.0f : 1.0f);

    const AssetImportState state = GetImportState(*record);
    drawList->AddCircleFilled(ImVec2(cardMax.x - 13.0f, cardMin.y + 13.0f),
                              4.0f, ImGui::GetColorU32(StateColor(state)));

    const ImVec2 iconPos{cardMin.x + (cardWidth - iconSize) * 0.5f,
                         cardMin.y + 13.0f};
    ImGui::SetCursorScreenPos(iconPos);
    EDITOR::DrawAssetTypeGlyph(record->type, ImVec2(iconSize, iconSize));

    const std::string displayName = record->displayName.empty()
                                        ? record->sourcePath.stem().string()
                                        : record->displayName;
    const float labelWidth = cardWidth - 20.0f;
    const std::string gridLabel = BuildGridCardLabel(displayName, labelWidth);
    ImGui::SetCursorScreenPos(
        ImVec2(cardMin.x + 10.0f, cardMin.y + iconSize + 23.0f));
    ImGui::TextUnformatted(gridLabel.c_str());
    ImGui::SetCursorScreenPos(cursorAfterCard);

    ImGui::PopID();
    column = (column + 1) % columns;
  }

  ImGui::EndTable();
}
#endif

} // namespace HIKARI::EDITOR::ASSET_BROWSER
