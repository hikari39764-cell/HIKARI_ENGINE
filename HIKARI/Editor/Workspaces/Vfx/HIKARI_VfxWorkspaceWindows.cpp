#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

    void VfxWorkspaceController::DrawDockSpace(
        bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
            return;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiID dockspaceId = ImGui::GetID(
            "HIKARI_VfxWorkspaceDockSpace_v1");
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr ||
            resetDefaultDockLayout) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(
                dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(
                dockspaceId, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(
                dockspaceId, viewport->WorkSize);

            ImGuiID body = dockspaceId;
            ImGuiID toolbar = 0u;
            ImGuiID hierarchy = 0u;
            ImGuiID inspector = 0u;
            ImGuiID bottom = 0u;
            ImGui::DockBuilderSplitNode(
                body, ImGuiDir_Up, 0.07f, &toolbar, &body);
            ImGui::DockBuilderSplitNode(
                body, ImGuiDir_Left, 0.24f, &hierarchy, &body);
            ImGui::DockBuilderSplitNode(
                body, ImGuiDir_Right, 0.29f, &inspector, &body);
            ImGui::DockBuilderSplitNode(
                body, ImGuiDir_Down, 0.30f, &bottom, &body);

            ImGui::DockBuilderDockWindow(
                "VFX###Vfx/Toolbar", toolbar);
            ImGui::DockBuilderDockWindow(
                "Effect Hierarchy###Vfx/Hierarchy", hierarchy);
            ImGui::DockBuilderDockWindow(
                "Preview###Vfx/Preview", body);
            ImGui::DockBuilderDockWindow(
                "Properties###Vfx/Inspector", inspector);
            ImGui::DockBuilderDockWindow(
                "Timeline & Curves###Vfx/CurvesDiagnostics", bottom);
            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
        (void)resetDefaultDockLayout;
#endif
    }

    VfxWorkspaceResult VfxWorkspaceController::Draw(
        AssetDatabase& database,
        EditorViewInstance& previewView,
        uint64_t frameIndex,
        float deltaSeconds,
        EditorCommandRouter& commandRouter) {

        VfxWorkspaceResult result{};
#if defined(HIKARI_WITH_EDITOR)
        if (!document_.IsOpen()) {
            NewAsset();
        }
        UpdatePreview(frameIndex, deltaSeconds);
        DrawToolbarWindow(database, result, commandRouter);
        DrawHierarchyWindow(database);
        DrawCurveDiagnosticsWindow();
        DrawPreviewWindow(previewView, deltaSeconds);
        DrawInspectorWindow(database);
#else
        (void)database;
        (void)previewView;
        (void)frameIndex;
        (void)deltaSeconds;
        (void)commandRouter;
#endif
        result.statusMessage = statusMessage_;
        return result;
    }

    void VfxWorkspaceController::DrawToolbarWindow(
        AssetDatabase& database,
        VfxWorkspaceResult& result,
        EditorCommandRouter& commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
        (void)commandRouter;
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (!ImGui::Begin("VFX###Vfx/Toolbar", nullptr, flags)) {
            ImGui::End();
            return;
        }

        if (ImGui::Button("New", { 64.0f, 28.0f })) {
            ImGui::OpenPopup("Create VFX Asset");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_.IsOpen());
        if (ImGui::Button("Save", { 64.0f, 28.0f })) {
            (void)Save(database, statusMessage_);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_.CanUndo());
        if (ImGui::Button("Undo", { 58.0f, 28.0f })) {
            (void)Undo(statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_.CanRedo());
        if (ImGui::Button("Redo", { 58.0f, 28.0f })) {
            (void)Redo(statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::Dummy({ 12.0f, 1.0f });
        ImGui::SameLine();
        if (!previewPlaying_) {
            if (ImGui::Button("Play", { 64.0f, 28.0f })) {
                (void)PlayPreview(statusMessage_);
            }
        } else {
            if (ImGui::Button(
                    previewPaused_ ? "Resume" : "Pause",
                    { 68.0f, 28.0f })) {
                PausePreview(!previewPaused_);
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!previewPlaying_);
        if (ImGui::Button("Restart", { 72.0f, 28.0f })) {
            (void)RestartPreview(statusMessage_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop", { 60.0f, 28.0f })) {
            StopPreview();
            statusMessage_ = "VFX preview stopped";
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        const char* state = VFX::HasVfxErrors(diagnostics_)
            ? "Compile errors"
            : document_.IsDirty()
                ? "Unsaved"
                : "Saved";
        ImGui::TextDisabled(
            "%s  |  %.2f / %.2f s",
            state,
            previewElapsedSeconds_,
            document_.Asset().durationSeconds);

        const float exitWidth = 94.0f;
        ImGui::SameLine((std::max)(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowWidth() - exitWidth - 12.0f));
        if (ImGui::Button("Scene", { exitWidth, 28.0f })) {
            result.exitToSceneRequested = true;
        }

        if (ImGui::BeginPopupModal(
                "Create VFX Asset",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Start a stack-based native VFX asset.");
            if (document_.IsDirty()) {
                ImGui::TextColored(
                    { 1.0f, 0.72f, 0.22f, 1.0f },
                    "The current document has unsaved changes.");
            }
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputText(
                "Name",
                newAssetName_.data(),
                newAssetName_.size());
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(
                    document_.IsDirty()
                        ? "Discard and Create"
                        : "Create")) {
                NewAsset(newAssetName_.data());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SeparatorText("Start From Preset");
            ImGui::TextDisabled(
                "Effekseer-style starter presets (no external assets).");
            if (ImGui::BeginListBox("##vfx_presets", { 320.0f, 180.0f })) {
                std::string lastCategory{};
                for (const auto& preset :
                    VFX::PRESETS::GetVfxPresetCatalog()) {
                    if (preset.category != lastCategory) {
                        lastCategory = preset.category;
                        ImGui::SeparatorText(preset.category.c_str());
                    }
                    ImGui::PushID(preset.id.c_str());
                    if (ImGui::Selectable(preset.displayName.c_str())) {
                        std::string presetMessage{};
                        if (NewAssetFromPreset(preset.id, presetMessage)) {
                            ImGui::CloseCurrentPopup();
                        } else if (!presetMessage.empty()) {
                            statusMessage_ = presetMessage;
                        }
                    }
                    if (ImGui::IsItemHovered(
                            ImGuiHoveredFlags_DelayShort)) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(preset.summary.c_str());
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
                ImGui::EndListBox();
            }
            ImGui::EndPopup();
        }
        ImGui::End();
#else
        (void)database;
        (void)result;
        (void)commandRouter;
#endif
    }

} // namespace HIKARI::EDITOR
