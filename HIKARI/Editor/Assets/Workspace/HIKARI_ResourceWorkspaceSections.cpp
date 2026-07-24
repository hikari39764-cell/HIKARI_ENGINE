#include "Editor/Assets/Workspace/HIKARI_ResourceWorkspacePanel.h"

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
int CountByType(const AssetDatabase &assetDatabase, AssetType type) {
  return static_cast<int>(assetDatabase.CollectByType(type).size());
}

int CountBroken(const AssetDatabase &assetDatabase) {
  int count = 0;
  for (const AssetRecord *record : assetDatabase.CollectAll()) {
    if (record && IsBrokenAssetRecord(*record)) {
      ++count;
    }
  }
  return count;
}

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

const char *ScopeLabel(AssetBrowserScope scope) {
  switch (scope) {
  case AssetBrowserScope::CurrentScene:
    return "Current Scene";
  case AssetBrowserScope::UnusedInScene:
    return "Unused";
  case AssetBrowserScope::Broken:
    return "Broken";
  case AssetBrowserScope::Textures:
    return "Textures";
  case AssetBrowserScope::Models:
    return "Models";
  case AssetBrowserScope::Scenes:
    return "Scenes";
  case AssetBrowserScope::Materials:
    return "Materials";
  case AssetBrowserScope::Skies:
    return "Skies";
  case AssetBrowserScope::Vfx:
    return "VFX";
  case AssetBrowserScope::Sequences:
    return "Sequences";
  case AssetBrowserScope::Project:
  default:
    return "Project";
  }
}

int ScopeCount(const AssetDatabase &assetDatabase,
               const AssetUsageSummary &usageSummary, AssetBrowserScope scope) {

  const int total = static_cast<int>(assetDatabase.CollectAll().size());
  const int used = static_cast<int>(usageSummary.usedGuids.size());
  switch (scope) {
  case AssetBrowserScope::CurrentScene:
    return used;
  case AssetBrowserScope::UnusedInScene:
    return (std::max)(0, total - used);
  case AssetBrowserScope::Broken:
    return CountBroken(assetDatabase) +
           static_cast<int>(usageSummary.missingReferences.size());
  case AssetBrowserScope::Textures:
    return CountByType(assetDatabase, AssetType::Texture);
  case AssetBrowserScope::Models:
    return CountByType(assetDatabase, AssetType::Model);
  case AssetBrowserScope::Scenes:
    return CountByType(assetDatabase, AssetType::Scene);
  case AssetBrowserScope::Materials:
    return CountByType(assetDatabase, AssetType::Material);
  case AssetBrowserScope::Skies:
    return CountByType(assetDatabase, AssetType::Sky);
  case AssetBrowserScope::Vfx:
    return CountByType(assetDatabase, AssetType::VfxEffect);
  case AssetBrowserScope::Sequences:
    return CountByType(assetDatabase, AssetType::Sequence);
  case AssetBrowserScope::Project:
  default:
    return total;
  }
}

void DrawScopeMenuItem(const AssetDatabase &assetDatabase,
                       const AssetUsageSummary &usageSummary, const char *label,
                       AssetBrowserScope scope,
                       AssetBrowserScope &activeScope) {

  const std::string itemLabel =
      std::string(label) + "  " +
      std::to_string(ScopeCount(assetDatabase, usageSummary, scope));
  if (ImGui::MenuItem(itemLabel.c_str(), nullptr, activeScope == scope)) {
    activeScope = scope;
  }
}

void DrawScopeCombo(const AssetDatabase &assetDatabase,
                    const AssetUsageSummary &usageSummary,
                    AssetBrowserScope &activeScope) {

  const std::string preview =
      std::string(ScopeLabel(activeScope)) + "  " +
      std::to_string(ScopeCount(assetDatabase, usageSummary, activeScope));

  ImGui::SetNextItemWidth(190.0f);
  if (!ImGui::BeginCombo("##ResourceScope", preview.c_str())) {
    return;
  }

  ImGui::TextDisabled("Library");
  DrawScopeMenuItem(assetDatabase, usageSummary, "Project",
                    AssetBrowserScope::Project, activeScope);
  DrawScopeMenuItem(assetDatabase, usageSummary, "Current Scene",
                    AssetBrowserScope::CurrentScene, activeScope);
  DrawScopeMenuItem(assetDatabase, usageSummary, "Unused",
                    AssetBrowserScope::UnusedInScene, activeScope);
  DrawScopeMenuItem(assetDatabase, usageSummary, "Broken",
                    AssetBrowserScope::Broken, activeScope);

  ImGui::Separator();
  ImGui::TextDisabled("Types");
  DrawScopeMenuItem(assetDatabase, usageSummary, "Textures",
                    AssetBrowserScope::Textures, activeScope);
  DrawScopeMenuItem(assetDatabase, usageSummary, "Models",
                    AssetBrowserScope::Models, activeScope);
  DrawScopeMenuItem(assetDatabase, usageSummary, "Scenes",
                    AssetBrowserScope::Scenes, activeScope);
  DrawScopeMenuItem(assetDatabase, usageSummary, "Materials",
                    AssetBrowserScope::Materials, activeScope);
  DrawScopeMenuItem(assetDatabase, usageSummary, "Skies",
                    AssetBrowserScope::Skies, activeScope);
  DrawScopeMenuItem(assetDatabase, usageSummary, "VFX", AssetBrowserScope::Vfx,
                    activeScope);
  DrawScopeMenuItem(assetDatabase, usageSummary, "Sequences",
                    AssetBrowserScope::Sequences, activeScope);

  if (!usageSummary.missingReferences.empty()) {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.36f, 1.0f), "Missing References");
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
  ImGui::TextUnformatted("Background Asset Tasks");
  ImGui::SameLine();
  const std::vector<AssetTaskSnapshot> tasks =
      assetDatabase.GetAssetTaskService().CollectSnapshots();
  ImGui::TextDisabled("%d recent", static_cast<int>(tasks.size()));
  if (!tasks.empty()) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear Completed")) {
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
  ImGui::SeparatorText("Selected Asset Import");
  if (selection.selectedAssetGuid.empty()) {
    ImGui::TextDisabled("Select an asset to inspect its latest import message");
  } else {
    const AssetRecord *record =
        assetDatabase.FindByGuid(AssetGuid{selection.selectedAssetGuid});
    if (!record) {
      ImGui::TextDisabled("Selected asset is no longer available");
      return;
    }

    ImGui::Text("%s | %s", record->displayName.c_str(),
                ToString(GetImportState(*record)));
    if (!record->lastImportMessage.empty()) {
      ImGui::TextWrapped("%s", record->lastImportMessage.c_str());
    } else {
      ImGui::TextDisabled("No import report message yet");
    }

    if (!record->artifactManifest.artifacts.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled(
          "Artifacts: %d",
          static_cast<int>(record->artifactManifest.artifacts.size()));
    }
  }
}
#endif

} // namespace HIKARI::EDITOR::RESOURCE_WORKSPACE
