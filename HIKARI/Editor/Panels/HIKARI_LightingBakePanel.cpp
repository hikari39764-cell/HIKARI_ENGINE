#include "Editor/Panels/HIKARI_LightingBakePanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/Panels/Internal/HIKARI_LightingBakePanelSummary.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Tools/HIKARI_EditorTool.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

namespace {

TOOLS::BAKING::LightingBakeRequest BuildRequest(DocumentSceneBase& scene) {
    TOOLS::BAKING::LightingBakeRequest request{};
    request.projectRoot = scene.GetAssetDatabase().GetProjectRoot();
    request.sceneGuid = scene.GetCurrentSceneAssetGuid().value;
    request.sceneName = scene.GetCurrentSceneDisplayName();
    request.scenePath = scene.GetScenePath();
    request.sceneDocument = &scene.GetSceneDocument();
    request.assetDatabase = &scene.GetAssetDatabase();
    request.target = TOOLS::BAKING::LightingBakeTarget::All;
    return request;
}

bool OpenFolder(const std::filesystem::path& folder, std::string& outMessage) {
    std::error_code ec{};
    if (!std::filesystem::exists(folder, ec)) {
        outMessage = TEXT::Localize("ui.lightingBake.folderNotExist");
        return false;
    }
    if (!EDITOR::SHELL::OpenFolderInExplorer(folder)) {
        outMessage = TEXT::Localize("ui.lightingBake.folderOpenFailed");
        return false;
    }
    outMessage = TEXT::Localize("ui.lightingBake.folderOpened");
    return true;
}

#if defined(HIKARI_WITH_EDITOR)
EDITOR::EditorGlyph ItemGlyph(TOOLS::BAKING::LightingBakeItemKind kind) {
    switch (kind) {
    case TOOLS::BAKING::LightingBakeItemKind::ReflectionProbe: return EDITOR::EditorGlyph::ReflectionProbe;
    case TOOLS::BAKING::LightingBakeItemKind::IrradianceVolume: return EDITOR::EditorGlyph::IrradianceVolume;
    case TOOLS::BAKING::LightingBakeItemKind::LightmapObject:
    default: return EDITOR::EditorGlyph::Model;
    }
}

void DrawInlineGlyph(EDITOR::EditorGlyph glyph, const ImVec4& color) {
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetTextLineHeight();
    EDITOR::DrawEditorGlyph(*ImGui::GetWindowDrawList(), glyph,
                            ImVec2(cursor.x + height * 0.5f, cursor.y + height * 0.5f), height,
                            ImGui::GetColorU32(color));
    ImGui::Dummy(ImVec2(height, height));
}

float ResolveProgress(const TOOLS::BAKING::LightingBakeReport& report) {
    if (report.bakeItemCount == 0u) {
        return 0.0f;
    }
    float current = 0.0f;
    if (report.reflectionProbeCapturedFaceCount > 0u) {
        current = std::clamp(static_cast<float>(report.reflectionProbeCapturedFaceCount) / 6.0f, 0.0f, 1.0f);
    } else if (report.lightProbeProbeCount > 0u) {
        const uint32_t totalFaces = report.lightProbeProbeCount * 6u;
        current =
            totalFaces > 0u
                ? std::clamp(static_cast<float>(report.lightProbeCapturedFaceCount) / static_cast<float>(totalFaces),
                             0.0f, 1.0f)
                : 0.0f;
    } else if (report.lightmapRequestedSamples > 0u) {
        current = std::clamp(static_cast<float>(report.lightmapCompletedSamples) /
                                 static_cast<float>(report.lightmapRequestedSamples),
                             0.0f, 1.0f);
    }
    return std::clamp((static_cast<float>(report.completedItemCount) + current) /
                          static_cast<float>(report.bakeItemCount),
                      0.0f, 1.0f);
}

void DrawLines(const char* label, const std::vector<std::string>& lines, const ImVec4& color) {
    if (lines.empty() || !ImGui::TreeNode(label)) {
        return;
    }
    for (const std::string& line : lines) {
        ImGui::TextColored(color, "%s", line.c_str());
    }
    ImGui::TreePop();
}

#endif

} // namespace

void LightingBakePanel::Draw(EDITOR::EditorToolContext& tool, bool& open) {
#if defined(HIKARI_WITH_EDITOR)
    auto& scene = tool.scene;
    const std::string windowName = TEXT::LocalizeWindow("ui.window.lightingBake", "Lighting Bake");
    if (tool.openRequest)
        EDITOR::RevealEditorWindow(windowName.c_str());
    ImGui::SetNextWindowSize({760.0f, 620.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(windowName.c_str(), &open)) {
        ImGui::End();
        return;
    }

    TOOLS::BAKING::LightingBakeService service{};
    const TOOLS::BAKING::LightingBakeRequest request = BuildRequest(scene);
    const bool bakeBusy = TOOLS::BAKING::IsLightingBakeJobRunning(scene.GetLightingBakeJobState());
    const std::string blockReason = service.GetBlockReason(scene);
    const auto execute = [&](const char* action, nlohmann::json parameters = nlohmann::json::object()) {
        const auto result = tool.operations.Execute(action, parameters);
        actionMessage_ = result.message;
        statusRefreshRequested_ = true;
    };
    const uint64_t sceneRevision = scene.GetSceneDocumentRevision();
    const uint64_t assetRevision = scene.GetAssetDatabase().GetContentRevision();
    const bool cacheIdentityChanged = cachedSceneGuid_ != request.sceneGuid;
    if (cacheIdentityChanged) {
        actionMessage_.clear();
        lastOpenFolderMessage_.clear();
    }
    const bool cacheRevisionChanged = cachedSceneRevision_ != sceneRevision || cachedAssetRevision_ != assetRevision;
    const bool bakeCompleted = wasBakeBusy_ && !bakeBusy;
    const bool needsStatusRefresh = !hasCachedStatus_ || cacheIdentityChanged || statusRefreshRequested_ ||
                                    bakeCompleted || (!bakeBusy && cacheRevisionChanged);
    if (needsStatusRefresh && (!bakeBusy || !hasCachedStatus_)) {
        cachedStatus_ = service.AnalyzeLightingBakeStatus(request);
        cachedSceneGuid_ = request.sceneGuid;
        cachedSceneRevision_ = sceneRevision;
        cachedAssetRevision_ = assetRevision;
        hasCachedStatus_ = true;
        statusRefreshRequested_ = false;
    }
    const TOOLS::BAKING::LightingBakeStatus& status = cachedStatus_;
    const std::filesystem::path bakeRoot =
        ASSETS::LIGHTING::BuildLightingBakeRoot(request.projectRoot, request.sceneGuid);
    const EDITOR::EditorThemePalette& palette = EDITOR::GetEditorThemePalette();

    EDITOR::PanelTitle(TEXT::Localize("ui.lightingBake.title").c_str(),
                       TEXT::Localize("ui.lightingBake.subtitle").c_str());
    ImGui::TextDisabled(TEXT::Localize("ui.lightingBake.sceneFormat").c_str(),
                        request.sceneName.empty() ? TEXT::Localize("ui.common.untitled").c_str()
                                                  : request.sceneName.c_str(),
                        request.sceneGuid.empty() ? TEXT::Localize("ui.lightingBake.saveSceneToEnable").c_str()
                                                  : request.sceneGuid.c_str());
    ImGui::SameLine();
    if (EDITOR::IconTextButton(EDITOR::EditorGlyph::Refresh, TEXT::Localize("ui.lightingBake.refresh").c_str(),
                               "LightingBakeRefreshStatus", EDITOR::EditorButtonTone::Quiet, {},
                               TEXT::Localize("ui.lightingBake.refreshTooltip").c_str())) {
        statusRefreshRequested_ = true;
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("LightingBakeSummary", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.lightingObjects").c_str());
        ImGui::Text("%u", status.totalCount);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.ready").c_str());
        EDITOR::StatusBadge(std::to_string(status.readyCount).c_str(), EDITOR::EditorStatusTone::Ready);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.dirty").c_str());
        EDITOR::StatusBadge(std::to_string(status.dirtyCount).c_str(), status.dirtyCount == 0u
                                                                           ? EDITOR::EditorStatusTone::Ready
                                                                           : EDITOR::EditorStatusTone::Warning);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    EDITOR::LIGHTING_BAKE_PANEL::DrawWorkflowSummary(status, palette);

    ImGui::Spacing();
    ImGui::TextWrapped("%s", TEXT::LocalizeCStr("ui.lightingBake.workflowHelp"));
    if (!blockReason.empty())
        ImGui::TextWrapped("%s", blockReason.c_str());
    const auto selected = tool.editorContext.selection.GetActiveObjectId();
    const auto selectedItem = std::find_if(status.items.begin(), status.items.end(), [&](const auto& item) {
        return item.enabled && item.objectId == selected.value;
    });
    if (!blockReason.empty() || !status.valid) {
        ImGui::BeginDisabled();
    }
    if (EDITOR::IconTextButton(EDITOR::EditorGlyph::Bake,
                               status.dirtyCount == 0u ? TEXT::Localize("ui.lightingBake.upToDate").c_str()
                                                       : TEXT::Localize("ui.lightingBake.bakeDirty").c_str(),
                               "LightingBakeDirty", EDITOR::EditorButtonTone::Primary, ImVec2(150.0f, 32.0f),
                               TEXT::Localize("ui.lightingBake.bakeDirtyTooltip").c_str())) {
        execute("lighting.bake.start");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedItem == status.items.end());
    if (EDITOR::IconTextButton(EDITOR::EditorGlyph::Focus, TEXT::Localize("ui.lightingBake.bakeSelected").c_str(),
                               "LightingBakeSelected", EDITOR::EditorButtonTone::Neutral, ImVec2(150.0f, 32.0f),
                               TEXT::Localize("ui.lightingBake.bakeSelectedTooltip").c_str())) {
        execute("lighting.bake.start", {{"force", true}, {"targetObjectId", selected.value}});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (EDITOR::IconTextButton(EDITOR::EditorGlyph::Refresh, TEXT::Localize("ui.lightingBake.bakeAll").c_str(),
                               "LightingBakeAll", EDITOR::EditorButtonTone::Quiet, ImVec2(120.0f, 32.0f),
                               TEXT::Localize("ui.lightingBake.bakeAllTooltip").c_str())) {
        execute("lighting.bake.start", {{"force", true}});
    }
    if (!blockReason.empty() || !status.valid) {
        ImGui::EndDisabled();
    }

    if (selectedItem != status.items.end())
        ImGui::Text(TEXT::LocalizeCStr("ui.lightingBake.selectedName"), selectedItem->name.c_str());
    else
        ImGui::TextDisabled("%s", TEXT::LocalizeCStr("ui.lightingBake.selectHint"));
    if (!actionMessage_.empty())
        ImGui::TextWrapped("%s", actionMessage_.c_str());
    const bool hasReport = scene.HasLastLightingBakeReport();
    const auto& report = scene.GetLastLightingBakeReport();
    if (hasReport && !report.errors.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, palette.error);
        ImGui::TextWrapped("%s", report.errors.front().c_str());
        ImGui::PopStyleColor();
    }
    if (bakeBusy && hasReport) {
        const float progress = ResolveProgress(report);
        const std::string label = report.currentItemName.empty()
                                      ? std::string(TOOLS::BAKING::ToString(report.jobState))
                                      : report.currentItemName + " - " + TOOLS::BAKING::ToString(report.jobState);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), label.c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText(TEXT::Localize("ui.lightingBake.lightingObjectsSection").c_str());
    EDITOR::SearchField("bakeObjects", TEXT::LocalizeCStr("ui.lightingBake.filter"), objectFilter_,
                        sizeof(objectFilter_));
    ImGui::SameLine();
    ImGui::Checkbox(TEXT::LocalizeCStr("ui.lightingBake.onlyDirty"), &onlyDirty_);
    if (status.items.empty()) {
        EDITOR::EmptyState(TEXT::Localize("ui.lightingBake.emptyTitle").c_str(),
                           TEXT::Localize("ui.lightingBake.emptyHint").c_str());
    } else if (ImGui::BeginTable("LightingBakeItems", 4,
                                 ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                     ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(TEXT::Localize("ui.lightingBake.tableType").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                42.0f);
        ImGui::TableSetupColumn(TEXT::Localize("ui.lightingBake.tableName").c_str());
        ImGui::TableSetupColumn(TEXT::Localize("ui.lightingBake.tableState").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                76.0f);
        ImGui::TableSetupColumn(TEXT::Localize("ui.lightingBake.tableReason").c_str());
        ImGui::TableHeadersRow();
        for (const TOOLS::BAKING::LightingBakeItemStatus& item : status.items) {
            if ((onlyDirty_ && (!item.enabled || !item.dirty)) ||
                (objectFilter_[0] && item.name.find(objectFilter_) == std::string::npos))
                continue;
            ImGui::PushID(static_cast<int>(item.objectId));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawInlineGlyph(ItemGlyph(item.kind), item.enabled ? palette.accent : palette.textMuted);
            ImGui::TableNextColumn();
            if (ImGui::Selectable(item.name.c_str(), selected.value == item.objectId))
                execute("scene.object.select", {{"objectId", item.objectId}});
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ID: %llu", static_cast<unsigned long long>(item.objectId));
            ImGui::TableNextColumn();
            if (!item.enabled) {
                EDITOR::StatusBadge(TEXT::Localize("ui.lightingBake.disabled").c_str(),
                                    EDITOR::EditorStatusTone::Normal);
            } else if (item.dirty) {
                EDITOR::StatusBadge(TEXT::Localize("ui.lightingBake.dirtyBadge").c_str(),
                                    EDITOR::EditorStatusTone::Warning);
            } else {
                EDITOR::StatusBadge(TEXT::Localize("ui.lightingBake.readyBadge").c_str(),
                                    EDITOR::EditorStatusTone::Ready);
            }
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", item.dirtyReason.c_str());
            if (ImGui::SmallButton(TEXT::LocalizeCStr("ui.lightingBake.locate")))
                execute("scene.object.focus", {{"objectId", item.objectId}});
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader(TEXT::Localize("ui.lightingBake.diagnosticsOutput").c_str())) {
        if (hasReport) {
            ImGui::Text(TEXT::Localize("ui.lightingBake.jobFormat").c_str(), TOOLS::BAKING::ToString(report.jobState));
            ImGui::SameLine();
            ImGui::TextDisabled(TEXT::Localize("ui.lightingBake.completeFormat").c_str(), report.completedItemCount,
                                report.bakeItemCount, report.reusedItemCount);
            DrawLines(TEXT::Localize("ui.lightingBake.dirtyReasons").c_str(), report.dirtyReasons, palette.warning);
            DrawLines(TEXT::Localize("ui.lightingBake.warnings").c_str(), report.warnings, palette.warning);
            DrawLines(TEXT::Localize("ui.lightingBake.errors").c_str(), report.errors, palette.error);
            DrawLines(TEXT::Localize("ui.lightingBake.messages").c_str(), report.messages, palette.textMuted);
        } else {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.noBakeAction").c_str());
        }
        for (const std::string& warning : status.warnings) {
            ImGui::TextColored(palette.warning, "%s", warning.c_str());
        }
        ImGui::TextWrapped(TEXT::Localize("ui.lightingBake.outputFormat").c_str(), bakeRoot.generic_string().c_str());
        if (EDITOR::IconTextButton(EDITOR::EditorGlyph::Folder, TEXT::Localize("ui.lightingBake.openOutput").c_str(),
                                   "LightingBakeOpenOutput", EDITOR::EditorButtonTone::Quiet)) {
            if (!OpenFolder(bakeRoot, lastOpenFolderMessage_)) {
                HIKARI_LOG_WARN("[LightingBake] " + lastOpenFolderMessage_);
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!blockReason.empty());
        if (EDITOR::IconTextButton(EDITOR::EditorGlyph::Delete, TEXT::Localize("ui.lightingBake.clearOutputs").c_str(),
                                   "LightingBakeClear", EDITOR::EditorButtonTone::Danger)) {
            ImGui::OpenPopup(TEXT::LocalizeWindow("ui.lightingBake.clearOutputs", "ClearLightingOutputs").c_str());
        }
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal(TEXT::LocalizeWindow("ui.lightingBake.clearOutputs", "ClearLightingOutputs").c_str(),
                                   nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", TEXT::LocalizeCStr("ui.lightingBake.clearConfirm"));
            ImGui::BeginDisabled(!blockReason.empty());
            if (ImGui::Button(TEXT::LocalizeCStr("ui.lightingBake.clearOutputs"))) {
                execute("lighting.bake.clear");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(TEXT::LocalizeCStr("ui.common.cancel")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (!lastOpenFolderMessage_.empty()) {
            ImGui::TextDisabled("%s", lastOpenFolderMessage_.c_str());
        }
    }

    wasBakeBusy_ = bakeBusy;
    ImGui::End();
#else
    (void)tool;
    (void)open;
#endif
}

} // namespace HIKARI
