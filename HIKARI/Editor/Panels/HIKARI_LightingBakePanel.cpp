#include "Editor/Panels/HIKARI_LightingBakePanel.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {

        TOOLS::BAKING::LightingBakeRequest BuildRequest(
            DocumentSceneBase& scene) {

            TOOLS::BAKING::LightingBakeRequest request{};
            request.projectRoot =
                scene.GetAssetDatabase().GetProjectRoot();
            request.sceneGuid =
                scene.GetCurrentSceneAssetGuid().value;
            request.sceneName =
                scene.GetCurrentSceneDisplayName();
            request.scenePath = scene.GetScenePath();
            request.sceneDocument = &scene.GetSceneDocument();
            request.assetDatabase = &scene.GetAssetDatabase();
            request.target =
                TOOLS::BAKING::LightingBakeTarget::All;
            return request;
        }

        bool OpenFolder(
            const std::filesystem::path& folder,
            std::string& outMessage) {

            std::error_code ec{};
            if (!std::filesystem::exists(folder, ec)) {
                outMessage =
                    "Bake folder does not exist yet.";
                return false;
            }
            if (!EDITOR::SHELL::OpenFolderInExplorer(folder)) {
                outMessage = "Failed to open bake folder.";
                return false;
            }
            outMessage = "Opened bake folder.";
            return true;
        }

#if defined(HIKARI_WITH_EDITOR)
        EDITOR::EditorGlyph ItemGlyph(
            TOOLS::BAKING::LightingBakeItemKind kind) {

            return kind ==
                TOOLS::BAKING::LightingBakeItemKind::
                    ReflectionProbe
                ? EDITOR::EditorGlyph::ReflectionProbe
                : EDITOR::EditorGlyph::IrradianceVolume;
        }

        void DrawInlineGlyph(
            EDITOR::EditorGlyph glyph,
            const ImVec4& color) {

            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float height = ImGui::GetTextLineHeight();
            EDITOR::DrawEditorGlyph(
                *ImGui::GetWindowDrawList(),
                glyph,
                ImVec2(
                    cursor.x + height * 0.5f,
                    cursor.y + height * 0.5f),
                height,
                ImGui::GetColorU32(color));
            ImGui::Dummy(ImVec2(height, height));
        }

        float ResolveProgress(
            const TOOLS::BAKING::LightingBakeReport& report) {

            if (report.bakeItemCount == 0u) {
                return 0.0f;
            }
            float current = 0.0f;
            if (report.reflectionProbeCapturedFaceCount > 0u) {
                current = std::clamp(
                    static_cast<float>(
                        report.reflectionProbeCapturedFaceCount) /
                        6.0f,
                    0.0f,
                    1.0f);
            } else if (report.lightProbeProbeCount > 0u) {
                const uint32_t totalFaces =
                    report.lightProbeProbeCount * 6u;
                current = totalFaces > 0u
                    ? std::clamp(
                        static_cast<float>(
                            report.lightProbeCapturedFaceCount) /
                            static_cast<float>(totalFaces),
                        0.0f,
                        1.0f)
                    : 0.0f;
            }
            return std::clamp(
                (static_cast<float>(report.completedItemCount) +
                    current) /
                    static_cast<float>(report.bakeItemCount),
                0.0f,
                1.0f);
        }

        void DrawLines(
            const char* label,
            const std::vector<std::string>& lines,
            const ImVec4& color) {

            if (lines.empty() ||
                !ImGui::TreeNode(label)) {
                return;
            }
            for (const std::string& line : lines) {
                ImGui::TextColored(
                    color,
                    "%s",
                    line.c_str());
            }
            ImGui::TreePop();
        }
#endif

    } // namespace

    void LightingBakePanel::Draw(
        DocumentSceneBase& scene,
        bool& open) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Lighting Bake", &open)) {
            ImGui::End();
            return;
        }

        TOOLS::BAKING::LightingBakeService service{};
        const TOOLS::BAKING::LightingBakeRequest request =
            BuildRequest(scene);
        const TOOLS::BAKING::LightingBakeStatus status =
            service.AnalyzeLightingBakeStatus(request);
        const std::filesystem::path bakeRoot =
            ASSETS::LIGHTING::BuildLightingBakeRoot(
                request.projectRoot,
                request.sceneGuid);
        const EDITOR::EditorThemePalette& palette =
            EDITOR::GetEditorThemePalette();

        EDITOR::PanelTitle(
            "Lighting Bake",
            "Scene lighting objects, dependencies, and cooked outputs");
        ImGui::TextDisabled(
            "%s  |  %s",
            request.sceneName.empty()
                ? "Untitled"
                : request.sceneName.c_str(),
            request.sceneGuid.empty()
                ? "Save scene to enable bake"
                : request.sceneGuid.c_str());

        ImGui::Spacing();
        if (ImGui::BeginTable(
                "LightingBakeSummary",
                3,
                ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("LIGHTING OBJECTS");
            ImGui::Text("%u", status.totalCount);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("READY");
            EDITOR::StatusBadge(
                std::to_string(status.readyCount).c_str(),
                EDITOR::EditorStatusTone::Ready);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("DIRTY");
            EDITOR::StatusBadge(
                std::to_string(status.dirtyCount).c_str(),
                status.dirtyCount == 0u
                    ? EDITOR::EditorStatusTone::Ready
                    : EDITOR::EditorStatusTone::Warning);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        const bool bakeBusy =
            TOOLS::BAKING::IsLightingBakeJobRunning(
                scene.GetLightingBakeJobState());
        if (bakeBusy || !status.valid) {
            ImGui::BeginDisabled();
        }
        if (EDITOR::IconTextButton(
                EDITOR::EditorGlyph::Bake,
                status.dirtyCount == 0u
                    ? "Up To Date"
                    : "Bake Dirty",
                "LightingBakeDirty",
                EDITOR::EditorButtonTone::Primary,
                ImVec2(150.0f, 32.0f),
                "Bake only objects whose authoring or dependencies changed.")) {
            lastReport_ = service.BakeDirtyLighting(scene);
            hasReport_ = true;
        }
        ImGui::SameLine();
        if (EDITOR::IconTextButton(
                EDITOR::EditorGlyph::Focus,
                "Bake Selected",
                "LightingBakeSelected",
                EDITOR::EditorButtonTone::Neutral,
                ImVec2(150.0f, 32.0f),
                "Force bake the selected lighting object.")) {
            lastReport_ = service.BakeSelectedLighting(scene);
            hasReport_ = true;
        }
        ImGui::SameLine();
        if (EDITOR::IconTextButton(
                EDITOR::EditorGlyph::Refresh,
                "Bake All",
                "LightingBakeAll",
                EDITOR::EditorButtonTone::Quiet,
                ImVec2(120.0f, 32.0f),
                "Force bake every enabled lighting object.")) {
            lastReport_ = service.BakeAllLighting(scene);
            hasReport_ = true;
        }
        if (bakeBusy || !status.valid) {
            ImGui::EndDisabled();
        }

        if (scene.HasLastLightingBakeReport()) {
            lastReport_ = scene.GetLastLightingBakeReport();
            hasReport_ = true;
        }
        if (bakeBusy && hasReport_) {
            const float progress = ResolveProgress(lastReport_);
            const std::string label =
                lastReport_.currentItemName.empty()
                    ? std::string(TOOLS::BAKING::ToString(
                        lastReport_.jobState))
                    : lastReport_.currentItemName + " - " +
                        TOOLS::BAKING::ToString(
                            lastReport_.jobState);
            ImGui::ProgressBar(
                progress,
                ImVec2(-1.0f, 0.0f),
                label.c_str());
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Lighting Objects");
        if (status.items.empty()) {
            EDITOR::EmptyState(
                "No lighting objects",
                "Use Add > Lighting to create a Reflection Probe or Irradiance Volume.");
        } else if (ImGui::BeginTable(
                "LightingBakeItems",
                4,
                ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_BordersInnerH |
                    ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(
                "Type",
                ImGuiTableColumnFlags_WidthFixed,
                42.0f);
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn(
                "State",
                ImGuiTableColumnFlags_WidthFixed,
                76.0f);
            ImGui::TableSetupColumn("Reason");
            ImGui::TableHeadersRow();
            for (const TOOLS::BAKING::LightingBakeItemStatus& item :
                status.items) {
                ImGui::PushID(
                    static_cast<int>(item.objectId));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                DrawInlineGlyph(
                    ItemGlyph(item.kind),
                    item.enabled
                        ? palette.accent
                        : palette.textMuted);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(item.name.c_str());
                ImGui::TableNextColumn();
                if (!item.enabled) {
                    EDITOR::StatusBadge(
                        "Disabled",
                        EDITOR::EditorStatusTone::Normal);
                } else if (item.dirty) {
                    EDITOR::StatusBadge(
                        "Dirty",
                        EDITOR::EditorStatusTone::Warning);
                } else {
                    EDITOR::StatusBadge(
                        "Ready",
                        EDITOR::EditorStatusTone::Ready);
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled(
                    "%s",
                    item.dirtyReason.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader(
                "Diagnostics & Output",
                ImGuiTreeNodeFlags_DefaultOpen)) {
            if (hasReport_) {
                ImGui::Text(
                    "Job: %s",
                    TOOLS::BAKING::ToString(lastReport_.jobState));
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "| %u / %u complete, %u reused",
                    lastReport_.completedItemCount,
                    lastReport_.bakeItemCount,
                    lastReport_.reusedItemCount);
                DrawLines(
                    "Dirty Reasons",
                    lastReport_.dirtyReasons,
                    palette.warning);
                DrawLines(
                    "Warnings",
                    lastReport_.warnings,
                    palette.warning);
                DrawLines(
                    "Errors",
                    lastReport_.errors,
                    palette.error);
                DrawLines(
                    "Messages",
                    lastReport_.messages,
                    palette.textMuted);
            } else {
                ImGui::TextDisabled(
                    "No bake action has been run in this session.");
            }
            for (const std::string& warning : status.warnings) {
                ImGui::TextColored(
                    palette.warning,
                    "%s",
                    warning.c_str());
            }
            ImGui::TextWrapped(
                "Output: %s",
                bakeRoot.generic_string().c_str());
            if (EDITOR::IconTextButton(
                    EDITOR::EditorGlyph::Folder,
                    "Open Output",
                    "LightingBakeOpenOutput",
                    EDITOR::EditorButtonTone::Quiet)) {
                if (!OpenFolder(
                        bakeRoot,
                        lastOpenFolderMessage_)) {
                    HIKARI_LOG_WARN(
                        "[LightingBake] " +
                        lastOpenFolderMessage_);
                }
            }
            ImGui::SameLine();
            if (EDITOR::IconTextButton(
                    EDITOR::EditorGlyph::Delete,
                    "Clear Outputs",
                    "LightingBakeClear",
                    EDITOR::EditorButtonTone::Danger)) {
                lastReport_ = service.ClearLightingBake(request);
                hasReport_ = true;
                scene.RefreshLightingRuntime();
            }
            if (!lastOpenFolderMessage_.empty()) {
                ImGui::TextDisabled(
                    "%s",
                    lastOpenFolderMessage_.c_str());
            }
        }

        ImGui::End();
#else
        (void)scene;
        (void)open;
#endif
    }

} // namespace HIKARI
