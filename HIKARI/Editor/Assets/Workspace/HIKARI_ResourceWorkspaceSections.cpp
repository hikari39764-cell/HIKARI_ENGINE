#include "Editor/Assets/Workspace/HIKARI_ResourceWorkspacePanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/HIKARI_SceneDocument.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
#include "Editor/Assets/Workspace/HIKARI_ResourceWorkspaceSections.h"

namespace HIKARI::EDITOR::RESOURCE_WORKSPACE {
#if defined(HIKARI_WITH_EDITOR)
ResourceImportBatchMonitor MakeImportMonitor(int attempted, int succeeded,
                                             int failed, std::string label) {
  const double visibilitySeconds =
      failed > 0 ? 8.0 : (attempted > 0 ? 3.5 : 2.0);
  return ResourceImportBatchMonitor{
      attempted, succeeded,        failed,
      true,      std::move(label), ImGui::GetTime() + visibilitySeconds};
}

ResourceImportBatchMonitor
MakeImportMonitor(const AssetImportBatchStatus &status) {
  return MakeImportMonitor(status.attempted, status.succeeded, status.failed,
                           status.label);
}

std::string ScopeLabel(AssetBrowserScope scope) {
  switch (scope) {
  case AssetBrowserScope::CurrentScene:
    return TEXT::Localize("ui.resource.scope.currentScene");
  case AssetBrowserScope::UnusedInScene:
    return TEXT::Localize("ui.resource.scope.unused");
  case AssetBrowserScope::Broken:
    return TEXT::Localize("ui.resource.scope.broken");
  case AssetBrowserScope::Textures:
    return TEXT::Localize("ui.resource.scope.textures");
  case AssetBrowserScope::Models:
    return TEXT::Localize("ui.resource.scope.models");
  case AssetBrowserScope::Scenes:
    return TEXT::Localize("ui.resource.scope.scenes");
  case AssetBrowserScope::Materials:
    return TEXT::Localize("ui.resource.scope.materials");
  case AssetBrowserScope::Skies:
    return TEXT::Localize("ui.resource.scope.skies");
  case AssetBrowserScope::Vfx:
    return TEXT::Localize("ui.resource.scope.vfx");
  case AssetBrowserScope::Sequences:
    return TEXT::Localize("ui.resource.scope.sequences");
  case AssetBrowserScope::Project:
  default:
    return TEXT::Localize("ui.resource.scope.project");
  }
}

int ScopeCount(const ASSET_BROWSER::AssetBrowserStatisticsCache &statistics,
               const AssetUsageSummary &usageSummary, AssetBrowserScope scope) {

  const int total = static_cast<int>(statistics.Total());
  const int used = static_cast<int>(usageSummary.usedGuids.size());
  switch (scope) {
  case AssetBrowserScope::CurrentScene:
    return used;
  case AssetBrowserScope::UnusedInScene:
    return (std::max)(0, total - used);
  case AssetBrowserScope::Broken:
    return statistics.Broken() +
           static_cast<int>(usageSummary.missingReferences.size());
  case AssetBrowserScope::Textures:
    return statistics.Count(AssetType::Texture);
  case AssetBrowserScope::Models:
    return statistics.Count(AssetType::Model);
  case AssetBrowserScope::Scenes:
    return statistics.Count(AssetType::Scene);
  case AssetBrowserScope::Materials:
    return statistics.Count(AssetType::Material);
  case AssetBrowserScope::Skies:
    return statistics.Count(AssetType::Sky);
  case AssetBrowserScope::Vfx:
    return statistics.Count(AssetType::VfxEffect);
  case AssetBrowserScope::Sequences:
    return statistics.Count(AssetType::Sequence);
  case AssetBrowserScope::Project:
  default:
    return total;
  }
}

void DrawScopeMenuItem(const ASSET_BROWSER::AssetBrowserStatisticsCache &statistics,
                       const AssetUsageSummary &usageSummary, const char *label,
                       AssetBrowserScope scope,
                       AssetBrowserScope &activeScope) {

  const std::string itemLabel =
      std::string(label) + "  " +
      std::to_string(ScopeCount(statistics, usageSummary, scope));
  if (ImGui::MenuItem(itemLabel.c_str(), nullptr, activeScope == scope)) {
    activeScope = scope;
  }
}

void DrawScopeCombo(const ASSET_BROWSER::AssetBrowserStatisticsCache &statistics,
                    const AssetUsageSummary &usageSummary,
                    AssetBrowserScope &activeScope) {

  const std::string preview =
      ScopeLabel(activeScope) + "  " +
      std::to_string(ScopeCount(statistics, usageSummary, activeScope));

  ImGui::SetNextItemWidth(190.0f);
  if (!ImGui::BeginCombo("##ResourceScope", preview.c_str())) {
    return;
  }

  ImGui::TextDisabled(
      "%s",
      TEXT::Localize("ui.resource.library").c_str());
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.project").c_str(),
      AssetBrowserScope::Project, activeScope);
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.currentScene").c_str(),
      AssetBrowserScope::CurrentScene, activeScope);
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.unused").c_str(),
      AssetBrowserScope::UnusedInScene, activeScope);
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.broken").c_str(),
      AssetBrowserScope::Broken, activeScope);

  ImGui::Separator();
  ImGui::TextDisabled(
      "%s",
      TEXT::Localize("ui.resource.types").c_str());
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.textures").c_str(),
      AssetBrowserScope::Textures, activeScope);
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.models").c_str(),
      AssetBrowserScope::Models, activeScope);
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.scenes").c_str(),
      AssetBrowserScope::Scenes, activeScope);
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.materials").c_str(),
      AssetBrowserScope::Materials, activeScope);
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.skies").c_str(),
      AssetBrowserScope::Skies, activeScope);
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.vfx").c_str(),
      AssetBrowserScope::Vfx, activeScope);
  DrawScopeMenuItem(
      statistics, usageSummary,
      TEXT::Localize("ui.resource.scope.sequences").c_str(),
      AssetBrowserScope::Sequences, activeScope);

  if (!usageSummary.missingReferences.empty()) {
    ImGui::Separator();
    ImGui::TextColored(
        ImVec4(1.0f, 0.42f, 0.36f, 1.0f),
        "%s",
        TEXT::Localize("ui.resource.missingReferences").c_str());
    for (const AssetMissingReference &missing :
         usageSummary.missingReferences) {
      ImGui::TextDisabled("%s: %s", missing.role.c_str(),
                          missing.assetId.c_str());
    }
  }

  ImGui::EndCombo();
}

void DrawPreviewAndImportLog(AssetDatabase &assetDatabase,
                             EditorSelection &selection) {
  ImGui::TextUnformatted(
      TEXT::Localize("ui.resource.backgroundTasks").c_str());
  ImGui::SameLine();
  const std::vector<AssetTaskSnapshot> tasks =
      assetDatabase.GetAssetTaskService().CollectSnapshots();
  ImGui::TextDisabled(
      TEXT::Localize("ui.resource.recentTasksFormat").c_str(),
      static_cast<int>(tasks.size()));
  if (!tasks.empty()) {
    ImGui::SameLine();
    if (ImGui::SmallButton(
            TEXT::Localize(
                "ui.resource.clearCompleted").c_str())) {
      assetDatabase.GetAssetTaskService().ClearCompleted();
    }
  }
  ImGui::Separator();

  int drawnTasks = 0;
  for (const AssetTaskSnapshot &task : tasks) {
    if (drawnTasks >= 6) {
      break;
    }
    ++drawnTasks;
    ImGui::PushID(static_cast<int>(task.id));
    ImGui::Text("%s  %s", ToString(task.state), task.label.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f s  |  %s", task.elapsedSeconds,
                        task.progress.stage.c_str());
    if (!task.progress.currentItem.empty() && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", task.progress.currentItem.c_str());
    }
    if (!task.resultMessage.empty() && IsTerminal(task.state)) {
      ImGui::TextDisabled("%s", task.resultMessage.c_str());
    }
    ImGui::PopID();
  }

  ImGui::Spacing();
  ImGui::SeparatorText(
      TEXT::Localize("ui.resource.selectedAssetImport").c_str());
  if (selection.selectedAssetGuid.empty()) {
    ImGui::TextDisabled(
        "%s",
        TEXT::Localize(
            "ui.resource.selectAssetHint").c_str());
  } else {
    const AssetRecord *record =
        assetDatabase.FindByGuid(AssetGuid{selection.selectedAssetGuid});
    if (!record) {
      ImGui::TextDisabled(
          "%s",
          TEXT::Localize(
              "ui.resource.assetUnavailable").c_str());
      return;
    }

    ImGui::Text("%s | %s", record->displayName.c_str(),
                ToString(GetImportState(*record)));
    if (!record->lastImportMessage.empty()) {
      ImGui::TextWrapped("%s", record->lastImportMessage.c_str());
    } else {
      ImGui::TextDisabled(
          "%s",
          TEXT::Localize(
              "ui.resource.noImportMessage").c_str());
    }

    if (!record->artifactManifest.artifacts.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled(
          TEXT::Localize(
              "ui.resource.artifactsFormat").c_str(),
          static_cast<int>(record->artifactManifest.artifacts.size()));
    }
  }
}
#endif

} // namespace HIKARI::EDITOR::RESOURCE_WORKSPACE
