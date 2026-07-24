#include "HIKARI_AssetBrowserPanel.h"
#include "Core/Text/HIKARI_AsciiCase.h"

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

namespace HIKARI {

using namespace EDITOR::ASSET_BROWSER;

void AssetBrowserPanel::Draw(AssetDatabase &assetDatabase,
                             EditorSelection &selection) const {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Asset Browser")) {
    ImGui::End();
    return;
  }
  DrawContents(assetDatabase, selection);
  ImGui::End();
#else
  (void)assetDatabase;
  (void)selection;
#endif
}

void AssetBrowserPanel::DrawContents(AssetDatabase &assetDatabase,
                                     EditorSelection &selection) const {
  DrawContents(assetDatabase, selection, nullptr, AssetBrowserScope::Project);
}

void AssetBrowserPanel::DrawContents(AssetDatabase &assetDatabase,
                                     EditorSelection &selection,
                                     const AssetUsageSummary *usageSummary,
                                     AssetBrowserScope scope,
                                     const AssetBrowserContext *context) const {
#if defined(HIKARI_WITH_EDITOR)
  if (currentDirectory_.empty()) {
    currentDirectory_ = "Assets";
  }
  if (!pendingReimportAndRefreshRuntimeAssetGuid_.empty()) {
    const AssetImportBatchStatus status = assetDatabase.GetQueuedImportStatus();
    if (!status.active && status.completed) {
      const AssetRecord *refreshed = assetDatabase.FindByGuid(
          AssetGuid{pendingReimportAndRefreshRuntimeAssetGuid_});
      if (!status.canceled && status.failed == 0 && refreshed != nullptr &&
          refreshed->lastImportSucceeded) {
        reimportAndRefreshRuntimeAssetGuid_ =
            pendingReimportAndRefreshRuntimeAssetGuid_;
        lastOperationMessage_ = "Reimport complete; runtime refresh queued";
      } else {
        lastOperationMessage_ =
            "Reimport did not complete; runtime refresh skipped";
      }
      pendingReimportAndRefreshRuntimeAssetGuid_.clear();
    }
  }
  ProcessDroppedFiles(assetDatabase, currentDirectory_, selection,
                      lastOperationMessage_);

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
  if (scope == AssetBrowserScope::Scenes) {
    ImGui::TextDisabled("Scenes are project assets. Open, duplicate, delete, "
                        "and set startup scene here.");
    if (context) {
      ImGui::TextDisabled(
          "Current: %s    Startup: %s",
          SceneDisplayNameByGuid(assetDatabase, context->currentSceneGuid)
              .c_str(),
          SceneDisplayNameByGuid(assetDatabase, context->startupSceneGuid)
              .c_str());
    }
    ImGui::Separator();
  }

  if (EDITOR::IconTextButton(
          EDITOR::EditorGlyph::Add, "New", "AssetBrowserNew",
          EDITOR::EditorButtonTone::Neutral, ImVec2(0.0f, 26.0f),
          "Create an asset or folder in the current directory")) {
    ImGui::OpenPopup("AssetBrowserCreateMenu");
  }
  if (ImGui::BeginPopup("AssetBrowserCreateMenu")) {
    // 髣厄ｽｴ隲帛現繝ｻ鬩堺ｼ夲ｽｽ・ｻ驍ｵ・ｺ繝ｻ・ｯ驛｢譎｢・ｽ・｡驛｢・ｧ繝ｻ・､驛｢譎｢・ｽ・ｳ驛｢譎√・郢晢ｽｻ驍ｵ・ｺ闕ｵ譎｢・ｽ陋ｾ・ｨ・ｾ郢晢ｽｻ遯ｶ・ｲ驍ｵ・ｺ陷会ｽｱ・つ遶丞｣ｹ・・Δ譎｢・ｽ・ｳ驛｢譏ｴ繝ｻ・趣ｽｦ驛｢譏ｴ繝ｻ繝ｻ・ｰ闔ｨ諛茨ｽｲ・ｺ驍ｵ・ｺ繝ｻ・ｮ髫ｴ竏壹・・代・・ｬ・ｫ陜｣・ｺ繝ｻ・ｽ隲帛ｲｩ繝ｻ驍ｵ・ｺ陷会ｽｱ遯ｶ・ｻ髫ｰ繝ｻ・ｽ・ｱ驍ｵ・ｺ郢晢ｽｻ・つ郢晢ｽｻ
    if (ImGui::MenuItem("Folder")) {
      CreateFolderFromBrowser(assetDatabase, currentDirectory_,
                              lastOperationMessage_);
    }
    if (ImGui::MenuItem("Scene Asset")) {
      CreateSceneFromBrowser(assetDatabase, currentDirectory_, selection,
                             lastOperationMessage_);
    }
    if (ImGui::MenuItem("Material Asset")) {
      CreateMaterialFromBrowser(assetDatabase, currentDirectory_, selection,
                                lastOperationMessage_);
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (EDITOR::IconToggleButton(EDITOR::EditorGlyph::Compact, "AssetViewCompact",
                               viewMode_ == 0, ImVec2(26.0f, 26.0f),
                               "Compact view")) {
    viewMode_ = 0;
  }
  ImGui::SameLine();
  if (EDITOR::IconToggleButton(EDITOR::EditorGlyph::List, "AssetViewList",
                               viewMode_ == 1, ImVec2(26.0f, 26.0f),
                               "List view")) {
    viewMode_ = 1;
  }
  ImGui::SameLine();
  if (EDITOR::IconToggleButton(EDITOR::EditorGlyph::Grid, "AssetViewGrid",
                               viewMode_ == 2, ImVec2(26.0f, 26.0f),
                               "Grid view")) {
    viewMode_ = 2;
  }
  ImGui::SameLine();
  const int activeFilterCount = (searchBuffer_[0] != '\0' ? 1 : 0) +
                                (typeFilter_ != 0 ? 1 : 0) +
                                (stateFilter_ != 0 ? 1 : 0);
  const std::string filterButtonLabel =
      filtersExpanded_
          ? "Hide Filters"
          : (activeFilterCount > 0
                 ? "Filters (" + std::to_string(activeFilterCount) + ")"
                 : "Filters");
  if (EDITOR::IconTextButton(
          EDITOR::EditorGlyph::Filter, filterButtonLabel.c_str(),
          "AssetBrowserFilters",
          filtersExpanded_ ? EDITOR::EditorButtonTone::Primary
                           : EDITOR::EditorButtonTone::Quiet,
          ImVec2(0.0f, 26.0f),
          filtersExpanded_ ? "Hide asset filters" : "Show asset filters")) {
    filtersExpanded_ = !filtersExpanded_;
  }
  if (activeFilterCount > 0) {
    ImGui::SameLine();
    if (EDITOR::IconButton(EDITOR::EditorGlyph::Close, "AssetFiltersClear",
                           EDITOR::EditorButtonTone::Quiet,
                           ImVec2(26.0f, 26.0f), "Clear all asset filters")) {
      searchBuffer_.fill('\0');
      typeFilter_ = 0;
      stateFilter_ = 0;
    }
  }
  ImGui::SameLine();
  ImGui::Checkbox("Recursive", &recursive_);

  if (filtersExpanded_) {
    EDITOR::SearchField(
        "AssetSearch", "Search assets...", searchBuffer_.data(),
        searchBuffer_.size(),
        (std::max)(220.0f, ImGui::GetContentRegionAvail().x * 0.42f));
    ImGui::SameLine();
    static const char *TypeFilterItems[] = {
        "All",   "Texture",  "Model",
        "Scene", "Sky",      "Material",
        "VFX",   "Sequence", "Animation State Machine"};
    ImGui::TextUnformatted("Type");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##AssetTypeFilter", &typeFilter_, TypeFilterItems,
                 IM_ARRAYSIZE(TypeFilterItems));
    ImGui::SameLine();
    static const char *StateFilterItems[] = {
        "All", "Imported", "Outdated", "Missing", "Error", "Meta Only"};
    ImGui::TextUnformatted("State");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    ImGui::Combo("##AssetStateFilter", &stateFilter_, StateFilterItems,
                 IM_ARRAYSIZE(StateFilterItems));
  }
  ImGui::PopStyleVar();

  if (lastOperationMessage_ != observedOperationMessage_) {
    observedOperationMessage_ = lastOperationMessage_;
    operationMessageVisibleUntil_ = ImGui::GetTime() + 3.5;
  }
  if (!lastOperationMessage_.empty() &&
      ImGui::GetTime() <= operationMessageVisibleUntil_) {
    ImGui::TextDisabled("%s", lastOperationMessage_.c_str());
  }

  ImGui::Separator();

  const ImVec2 available = ImGui::GetContentRegionAvail();
  const bool showFolderTree = scope == AssetBrowserScope::Project;
  std::vector<const AssetRecord *> records =
      showFolderTree
          ? assetDatabase.CollectInDirectory(currentDirectory_, recursive_)
          : assetDatabase.CollectAll();
  records.erase(
      std::remove_if(records.begin(), records.end(),
                     [&](const AssetRecord *record) {
                       return !record ||
                              !MatchesScope(*record, usageSummary, scope) ||
                              !MatchesTypeFilter(*record, typeFilter_) ||
                              !MatchesStateFilter(*record, stateFilter_) ||
                              !MatchesSearch(*record, searchBuffer_.data());
                     }),
      records.end());

  std::sort(records.begin(), records.end(),
            [](const AssetRecord *lhs, const AssetRecord *rhs) {
              if (!lhs || !rhs) {
                return lhs < rhs;
              }
              return TEXT::ToLowerAsciiCopy(lhs->displayName) <
                     TEXT::ToLowerAsciiCopy(rhs->displayName);
            });

  if (showFolderTree) {
    const float treeWidth =
        (std::min)(240.0f, (std::max)(160.0f, available.x * 0.20f));
    ImGui::BeginChild("##AssetFolderTree", ImVec2(treeWidth, 0.0f), true);
    ImGui::TextDisabled("Folders");
    ImGui::Separator();
    for (const std::filesystem::path &directory :
         assetDatabase.CollectDirectories()) {
      const bool selected =
          directory.lexically_normal().generic_string() ==
          currentDirectory_.lexically_normal().generic_string();
      const std::filesystem::path relative =
          directory.lexically_relative("Assets");
      int depth = 0;
      if (!relative.empty() && relative != ".") {
        depth =
            static_cast<int>(std::distance(relative.begin(), relative.end()));
      }
      if (depth > 0) {
        ImGui::Indent(static_cast<float>(depth) * 11.0f);
      }
      EDITOR::DrawFolderGlyph(ImVec2(15.0f, 15.0f));
      ImGui::SameLine();
      const std::string folderLabel =
          directory == std::filesystem::path("Assets")
              ? std::string("Assets")
              : directory.filename().generic_string();
      const std::string selectableLabel =
          folderLabel + "##" + directory.generic_string();
      if (ImGui::Selectable(selectableLabel.c_str(), selected)) {
        currentDirectory_ = directory;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", directory.generic_string().c_str());
      }
      if (depth > 0) {
        ImGui::Unindent(static_cast<float>(depth) * 11.0f);
      }
    }
    ImGui::EndChild();
    ImGui::SameLine();
  }

  ImGui::BeginChild("##AssetList", ImVec2(0.0f, 0.0f), true);
  if (scope == AssetBrowserScope::Project) {
    DrawDirectoryBreadcrumbs(currentDirectory_);
  } else {
    ImGui::TextUnformatted(ToScopeTitle(scope));
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%d assets", static_cast<int>(records.size()));
  ImGui::Separator();
  if (ImGui::BeginPopupContextWindow("AssetBrowserEmptyContext",
                                     ImGuiPopupFlags_MouseButtonRight |
                                         ImGuiPopupFlags_NoOpenOverItems)) {
    if (ImGui::MenuItem("New Folder")) {
      CreateFolderFromBrowser(assetDatabase, currentDirectory_,
                              lastOperationMessage_);
    }
    if (ImGui::MenuItem("New Scene Asset")) {
      CreateSceneFromBrowser(assetDatabase, currentDirectory_, selection,
                             lastOperationMessage_);
    }
    if (ImGui::MenuItem("New Material Asset")) {
      CreateMaterialFromBrowser(assetDatabase, currentDirectory_, selection,
                                lastOperationMessage_);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Refresh Assets")) {
      const bool ok = assetDatabase.ScanAssets(true);
      lastOperationMessage_ =
          ok ? "AssetDatabase refreshed" : "AssetDatabase refresh failed";
    }
    ImGui::EndPopup();
  }

  if (records.empty()) {
    EDITOR::EmptyState("No assets in this folder",
                       "Right-click to create an asset or folder.");
  } else if (viewMode_ == 1) {
    DrawRecordList(assetDatabase, records, selection, usageSummary,
                   lastOperationMessage_, context, activatedSceneGuid_,
                   activatedSequenceGuid_, activatedAnimationStateMachineGuid_,
                   activatedModelCollisionGuid_, saveSceneAsGuid_,
                   refreshRuntimeAssetGuid_,
                   reimportAndRefreshRuntimeAssetGuid_,
                   pendingReimportAndRefreshRuntimeAssetGuid_, renameSceneGuid_,
                   deleteSceneGuid_, renameSceneNameBuffer_);
  } else if (viewMode_ == 2) {
    DrawRecordGrid(assetDatabase, records, selection, usageSummary,
                   lastOperationMessage_, context, activatedSceneGuid_,
                   activatedSequenceGuid_, activatedAnimationStateMachineGuid_,
                   activatedModelCollisionGuid_, saveSceneAsGuid_,
                   refreshRuntimeAssetGuid_,
                   reimportAndRefreshRuntimeAssetGuid_,
                   pendingReimportAndRefreshRuntimeAssetGuid_, renameSceneGuid_,
                   deleteSceneGuid_, renameSceneNameBuffer_);
  } else {
    DrawRecordCompactRows(
        assetDatabase, records, selection, usageSummary, lastOperationMessage_,
        context, activatedSceneGuid_, activatedSequenceGuid_,
        activatedAnimationStateMachineGuid_, activatedModelCollisionGuid_,
        saveSceneAsGuid_, refreshRuntimeAssetGuid_,
        reimportAndRefreshRuntimeAssetGuid_,
        pendingReimportAndRefreshRuntimeAssetGuid_, renameSceneGuid_,
        deleteSceneGuid_, renameSceneNameBuffer_);
  }

  ImGui::EndChild();
  DrawSceneAssetModals(assetDatabase, selection, lastOperationMessage_, context,
                       renameSceneGuid_, deleteSceneGuid_,
                       renameSceneNameBuffer_);
#else
  (void)assetDatabase;
  (void)selection;
  (void)usageSummary;
  (void)scope;
  (void)context;
#endif
}

std::string AssetBrowserPanel::ConsumeActivatedSceneGuid() const {
#if defined(HIKARI_WITH_EDITOR)
  std::string value = std::move(activatedSceneGuid_);
  activatedSceneGuid_.clear();
  return value;
#else
  return {};
#endif
}

std::string AssetBrowserPanel::ConsumeActivatedSequenceGuid() const {
#if defined(HIKARI_WITH_EDITOR)
  std::string value = std::move(activatedSequenceGuid_);
  activatedSequenceGuid_.clear();
  return value;
#else
  return {};
#endif
}

std::string
AssetBrowserPanel::ConsumeActivatedAnimationStateMachineGuid() const {
#if defined(HIKARI_WITH_EDITOR)
  std::string value = std::move(activatedAnimationStateMachineGuid_);
  activatedAnimationStateMachineGuid_.clear();
  return value;
#else
  return {};
#endif
}

std::string AssetBrowserPanel::ConsumeActivatedModelCollisionGuid() const {
#if defined(HIKARI_WITH_EDITOR)
  std::string value = std::move(activatedModelCollisionGuid_);
  activatedModelCollisionGuid_.clear();
  return value;
#else
  return {};
#endif
}

std::string AssetBrowserPanel::ConsumeSaveSceneAsGuid() const {
#if defined(HIKARI_WITH_EDITOR)
  std::string value = std::move(saveSceneAsGuid_);
  saveSceneAsGuid_.clear();
  return value;
#else
  return {};
#endif
}

std::string AssetBrowserPanel::ConsumeRefreshRuntimeAssetGuid() const {
#if defined(HIKARI_WITH_EDITOR)
  std::string value = std::move(refreshRuntimeAssetGuid_);
  refreshRuntimeAssetGuid_.clear();
  return value;
#else
  return {};
#endif
}

std::string
AssetBrowserPanel::ConsumeReimportAndRefreshRuntimeAssetGuid() const {
#if defined(HIKARI_WITH_EDITOR)
  std::string value = std::move(reimportAndRefreshRuntimeAssetGuid_);
  reimportAndRefreshRuntimeAssetGuid_.clear();
  return value;
#else
  return {};
#endif
}

std::filesystem::path AssetBrowserPanel::CurrentDirectory() const {
  return currentDirectory_.empty() ? std::filesystem::path("Assets")
                                   : currentDirectory_;
}

bool AssetBrowserPanel::IsRecursiveEnabled() const { return recursive_; }

} // namespace HIKARI
