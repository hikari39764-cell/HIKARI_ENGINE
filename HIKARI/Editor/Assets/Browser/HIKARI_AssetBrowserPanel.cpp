#include "HIKARI_AssetBrowserPanel.h"
#include "Core/Text/HIKARI_Localization.h"
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
  if (!ImGui::Begin(
          TEXT::LocalizeWindow(
              "ui.window.assetBrowser",
              "Asset Browser").c_str())) {
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
        lastOperationMessage_ =
            TEXT::Localize("ui.assetBrowser.reimportComplete");
      } else {
        lastOperationMessage_ = TEXT::Localize(
            "ui.assetBrowser.reimportSkipped");
      }
      pendingReimportAndRefreshRuntimeAssetGuid_.clear();
    }
  }
  const ImVec2 dropTargetMin = ImGui::GetWindowPos();
  const ImVec2 dropTargetMax{
      dropTargetMin.x + ImGui::GetWindowSize().x,
      dropTargetMin.y + ImGui::GetWindowSize().y,
  };
  ProcessDroppedFiles(
      assetDatabase,
      currentDirectory_,
      selection,
      lastOperationMessage_,
      dropTargetMin.x,
      dropTargetMin.y,
      dropTargetMax.x,
      dropTargetMax.y);

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
  if (scope == AssetBrowserScope::Scenes) {
    ImGui::TextDisabled(
        "%s",
        TEXT::Localize("ui.assetBrowser.scenesHint").c_str());
    if (context) {
      ImGui::TextDisabled(
          TEXT::Localize(
              "ui.assetBrowser.currentStartupFormat").c_str(),
          SceneDisplayNameByGuid(assetDatabase, context->currentSceneGuid)
              .c_str(),
          SceneDisplayNameByGuid(assetDatabase, context->startupSceneGuid)
              .c_str());
    }
    ImGui::Separator();
  }

  if (EDITOR::IconTextButton(
          EDITOR::EditorGlyph::Add,
          TEXT::Localize("ui.assetBrowser.new").c_str(),
          "AssetBrowserNew",
          EDITOR::EditorButtonTone::Neutral, ImVec2(0.0f, 26.0f),
          TEXT::Localize("ui.assetBrowser.newTooltip").c_str())) {
    ImGui::OpenPopup("AssetBrowserCreateMenu");
  }
  if (ImGui::BeginPopup("AssetBrowserCreateMenu")) {
    // 髣厄ｽｴ隲帛現繝ｻ鬩堺ｼ夲ｽｽ・ｻ驍ｵ・ｺ繝ｻ・ｯ驛｢譎｢・ｽ・｡驛｢・ｧ繝ｻ・､驛｢譎｢・ｽ・ｳ驛｢譎√・郢晢ｽｻ驍ｵ・ｺ闕ｵ譎｢・ｽ陋ｾ・ｨ・ｾ郢晢ｽｻ遯ｶ・ｲ驍ｵ・ｺ陷会ｽｱ・つ遶丞｣ｹ・・Δ譎｢・ｽ・ｳ驛｢譏ｴ繝ｻ・趣ｽｦ驛｢譏ｴ繝ｻ繝ｻ・ｰ闔ｨ諛茨ｽｲ・ｺ驍ｵ・ｺ繝ｻ・ｮ髫ｴ竏壹・・代・・ｬ・ｫ陜｣・ｺ繝ｻ・ｽ隲帛ｲｩ繝ｻ驍ｵ・ｺ陷会ｽｱ遯ｶ・ｻ髫ｰ繝ｻ・ｽ・ｱ驍ｵ・ｺ郢晢ｽｻ・つ郢晢ｽｻ
    if (ImGui::MenuItem(
            TEXT::Localize("ui.assetBrowser.folder").c_str())) {
      CreateFolderFromBrowser(assetDatabase, currentDirectory_,
                              lastOperationMessage_);
    }
    if (ImGui::MenuItem(
            TEXT::Localize("ui.assetBrowser.sceneAsset").c_str())) {
      CreateSceneFromBrowser(assetDatabase, currentDirectory_, selection,
                             lastOperationMessage_);
    }
    if (ImGui::MenuItem(
            TEXT::Localize("ui.assetBrowser.materialAsset").c_str())) {
      CreateMaterialFromBrowser(assetDatabase, currentDirectory_, selection,
                                lastOperationMessage_);
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (EDITOR::IconToggleButton(EDITOR::EditorGlyph::Compact, "AssetViewCompact",
                               viewMode_ == 0, ImVec2(26.0f, 26.0f),
                               TEXT::Localize(
                                   "ui.assetBrowser.compactView").c_str())) {
    viewMode_ = 0;
  }
  ImGui::SameLine();
  if (EDITOR::IconToggleButton(EDITOR::EditorGlyph::List, "AssetViewList",
                               viewMode_ == 1, ImVec2(26.0f, 26.0f),
                               TEXT::Localize(
                                   "ui.assetBrowser.listView").c_str())) {
    viewMode_ = 1;
  }
  ImGui::SameLine();
  if (EDITOR::IconToggleButton(EDITOR::EditorGlyph::Grid, "AssetViewGrid",
                               viewMode_ == 2, ImVec2(26.0f, 26.0f),
                               TEXT::Localize(
                                   "ui.assetBrowser.gridView").c_str())) {
    viewMode_ = 2;
  }
  ImGui::SameLine();
  const int activeFilterCount = (searchBuffer_[0] != '\0' ? 1 : 0) +
                                (typeFilter_ != 0 ? 1 : 0) +
                                (stateFilter_ != 0 ? 1 : 0);
  const std::string filterButtonLabel =
      filtersExpanded_
          ? TEXT::Localize("ui.assetBrowser.hideFilters")
          : (activeFilterCount > 0
                 ? TEXT::Localize("ui.assetBrowser.filtersCountFormat") +
                     std::to_string(activeFilterCount) + ")"
                 : TEXT::Localize("ui.assetBrowser.filters"));
  if (EDITOR::IconTextButton(
          EDITOR::EditorGlyph::Filter, filterButtonLabel.c_str(),
          "AssetBrowserFilters",
          filtersExpanded_ ? EDITOR::EditorButtonTone::Primary
                           : EDITOR::EditorButtonTone::Quiet,
          ImVec2(0.0f, 26.0f),
          filtersExpanded_
              ? TEXT::Localize(
                  "ui.assetBrowser.hideAssetFilters").c_str()
              : TEXT::Localize(
                  "ui.assetBrowser.showAssetFilters").c_str())) {
    filtersExpanded_ = !filtersExpanded_;
  }
  if (activeFilterCount > 0) {
    ImGui::SameLine();
    if (EDITOR::IconButton(EDITOR::EditorGlyph::Close, "AssetFiltersClear",
                           EDITOR::EditorButtonTone::Quiet,
                           ImVec2(26.0f, 26.0f),
                           TEXT::Localize(
                               "ui.assetBrowser.clearFilters").c_str())) {
      searchBuffer_.fill('\0');
      typeFilter_ = 0;
      stateFilter_ = 0;
    }
  }
  ImGui::SameLine();
  ImGui::Checkbox(
      TEXT::Localize("ui.assetBrowser.recursive").c_str(),
      &recursive_);

  if (filtersExpanded_) {
    EDITOR::SearchField(
        "AssetSearch",
        TEXT::Localize("ui.assetBrowser.searchPlaceholder").c_str(),
        searchBuffer_.data(),
        searchBuffer_.size(),
        (std::max)(220.0f, ImGui::GetContentRegionAvail().x * 0.42f));
    ImGui::SameLine();
    const std::array<std::string, 10> TypeFilterItems{
        TEXT::Localize("ui.assetBrowser.filterAll"),
        TEXT::Localize("ui.assetBrowser.filterTexture"),
        TEXT::Localize("ui.assetBrowser.filterModel"),
        TEXT::Localize("ui.assetBrowser.filterScene"),
        TEXT::Localize("ui.assetBrowser.filterSky"),
        TEXT::Localize("ui.assetBrowser.filterMaterial"),
        TEXT::Localize("ui.assetBrowser.filterVfx"),
        TEXT::Localize("ui.assetBrowser.filterSequence"),
        TEXT::Localize("ui.assetBrowser.filterAnimationStateMachine"),
        TEXT::Localize("ui.assetBrowser.filterMaterialSet"),
    };
    const char *TypeFilterItemPtrs[10]{};
    for (size_t index = 0; index < TypeFilterItems.size(); ++index) {
        TypeFilterItemPtrs[index] = TypeFilterItems[index].c_str();
    };
    ImGui::TextUnformatted(
        TEXT::Localize("ui.assetBrowser.type").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo(
        "##AssetTypeFilter", &typeFilter_, TypeFilterItemPtrs,
        static_cast<int>(TypeFilterItems.size()));
    ImGui::SameLine();
    const std::array<std::string, 6> StateFilterItems{
        TEXT::Localize("ui.assetBrowser.filterAll"),
        TEXT::Localize("ui.assetBrowser.stateImported"),
        TEXT::Localize("ui.assetBrowser.stateOutdated"),
        TEXT::Localize("ui.assetBrowser.stateMissing"),
        TEXT::Localize("ui.assetBrowser.stateError"),
        TEXT::Localize("ui.assetBrowser.stateMetaOnly"),
    };
    const char *StateFilterItemPtrs[6]{};
    for (size_t index = 0; index < StateFilterItems.size(); ++index) {
        StateFilterItemPtrs[index] = StateFilterItems[index].c_str();
    };
    ImGui::TextUnformatted(
        TEXT::Localize("ui.assetBrowser.state").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    ImGui::Combo(
        "##AssetStateFilter", &stateFilter_, StateFilterItemPtrs,
        static_cast<int>(StateFilterItems.size()));
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
    ImGui::TextDisabled(
        "%s",
        TEXT::Localize("ui.assetBrowser.folders").c_str());
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
    ImGui::TextUnformatted(ToScopeTitle(scope).c_str());
  }
  ImGui::SameLine();
  ImGui::TextDisabled(
      TEXT::Localize("ui.assetBrowser.assetsFormat").c_str(),
      static_cast<int>(records.size()));
  ImGui::Separator();
  if (ImGui::BeginPopupContextWindow("AssetBrowserEmptyContext",
                                     ImGuiPopupFlags_MouseButtonRight |
                                         ImGuiPopupFlags_NoOpenOverItems)) {
    if (ImGui::MenuItem(
            TEXT::Localize("ui.assetBrowser.newFolder").c_str())) {
      CreateFolderFromBrowser(assetDatabase, currentDirectory_,
                              lastOperationMessage_);
    }
    if (ImGui::MenuItem(
            TEXT::Localize("ui.assetBrowser.newSceneAsset").c_str())) {
      CreateSceneFromBrowser(assetDatabase, currentDirectory_, selection,
                             lastOperationMessage_);
    }
    if (ImGui::MenuItem(
            TEXT::Localize("ui.assetBrowser.newMaterialAsset").c_str())) {
      CreateMaterialFromBrowser(assetDatabase, currentDirectory_, selection,
                                lastOperationMessage_);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(
            TEXT::Localize("ui.assetBrowser.refreshAssets").c_str())) {
      const bool ok = assetDatabase.ScanAssets(true);
      lastOperationMessage_ =
          ok
              ? TEXT::Localize(
                  "ui.assetBrowser.assetDatabaseRefreshed")
              : TEXT::Localize(
                  "ui.assetBrowser.assetDatabaseRefreshFailed");
    }
    ImGui::EndPopup();
  }

  if (records.empty()) {
    EDITOR::EmptyState(
        TEXT::Localize("ui.assetBrowser.emptyTitle").c_str(),
        TEXT::Localize("ui.assetBrowser.emptyHint").c_str());
  } else if (viewMode_ == 1) {
    DrawRecordList(assetDatabase, records, selection, usageSummary,
                   lastOperationMessage_, context, activatedAsset_,
                   saveSceneAsGuid_,
                   refreshRuntimeAssetGuid_,
                   pendingReimportAndRefreshRuntimeAssetGuid_, renameSceneGuid_,
                   deleteSceneGuid_, renameSceneNameBuffer_);
  } else if (viewMode_ == 2) {
    DrawRecordGrid(assetDatabase, records, selection, usageSummary,
                   lastOperationMessage_, context, activatedAsset_,
                   saveSceneAsGuid_,
                   refreshRuntimeAssetGuid_,
                   pendingReimportAndRefreshRuntimeAssetGuid_, renameSceneGuid_,
                   deleteSceneGuid_, renameSceneNameBuffer_);
  } else {
    DrawRecordCompactRows(
        assetDatabase, records, selection, usageSummary, lastOperationMessage_,
        context, activatedAsset_, saveSceneAsGuid_, refreshRuntimeAssetGuid_,
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

std::optional<AssetWorkspaceActivation>
AssetBrowserPanel::ConsumeActivatedAsset() const {
#if defined(HIKARI_WITH_EDITOR)
  std::optional<AssetWorkspaceActivation> value = std::move(activatedAsset_);
  activatedAsset_.reset();
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
