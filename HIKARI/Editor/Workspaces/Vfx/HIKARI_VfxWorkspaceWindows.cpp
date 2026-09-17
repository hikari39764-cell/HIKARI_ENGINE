#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
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
            "HIKARI_VfxWorkspaceDockSpace_v4");
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
                body, ImGuiDir_Down, 0.30f, &bottom, &body);
            ImGui::DockBuilderSplitNode(
                body, ImGuiDir_Left, 0.20f, &hierarchy, &body);
            ImGui::DockBuilderSplitNode(
                body, ImGuiDir_Right, 0.34f, &inspector, &body);

            ImGui::DockBuilderDockWindow(
                "VFX###Vfx/Toolbar", toolbar);
            ImGui::DockBuilderDockWindow(
                "Library & Layers###Vfx/Hierarchy", hierarchy);
            ImGui::DockBuilderDockWindow(
                "Preview###Vfx/Preview", body);
            ImGui::DockBuilderDockWindow(
                "Layer Settings###Vfx/Inspector", inspector);
            ImGui::DockBuilderDockWindow(
                "Timing & Curves###Vfx/CurvesDiagnostics", bottom);
            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
        (void)resetDefaultDockLayout;
#endif
    }

    VfxWorkspaceResult VfxWorkspaceController::Draw(
        AssetDatabase& database,
        const AssetRegistry& assetRegistry,
        EditorViewInstance& previewView,
        uint64_t frameIndex,
        float deltaSeconds,
        EditorCommandRouter& commandRouter) {

        VfxWorkspaceResult result{};
        (void)assetRegistry;
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
        DrawPendingAssetOpenModal(database);
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

    void VfxWorkspaceController::DrawPendingAssetOpenModal(
        AssetDatabase& database) {
#if defined(HIKARI_WITH_EDITOR)
        constexpr const char* kPopupId =
            "Unsaved VFX Changes###Vfx/PendingAssetOpen";
        if (!pendingAssetOpenGuid_.IsValid()) {
            return;
        }
        if (pendingAssetOpenPopupRequested_ ||
            !ImGui::IsPopupOpen(kPopupId)) {
            ImGui::OpenPopup(kPopupId);
            pendingAssetOpenPopupRequested_ = false;
        }
        if (!ImGui::BeginPopupModal(
                kPopupId,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        const AssetRecord* target =
            database.FindByGuid(pendingAssetOpenGuid_);
        const std::string targetName = target == nullptr
            ? pendingAssetOpenGuid_.value
            : target->displayName.empty()
                ? target->sourcePath.stem().string()
                : target->displayName;
        ImGui::TextWrapped(
            "Save changes to '%s' before opening '%s'?",
            document_.GetDisplayName().c_str(),
            targetName.c_str());
        ImGui::TextDisabled(
            VfxUiText("The requested effect will open only after this decision succeeds.").c_str());
        ImGui::Spacing();

        if (ImGui::Button(
                VfxUiText("Save and Continue").c_str(), ImVec2(150.0f, 0.0f))) {
            const AssetGuid targetGuid = pendingAssetOpenGuid_;
            if (Save(database, statusMessage_) &&
                OpenAsset(
                    database,
                    targetGuid,
                    statusMessage_)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(
                VfxUiText("Discard and Continue").c_str(), ImVec2(150.0f, 0.0f))) {
            const AssetGuid targetGuid = pendingAssetOpenGuid_;
            if (OpenAsset(
                    database,
                    targetGuid,
                    statusMessage_,
                    true)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(VfxUiText("Cancel").c_str(), ImVec2(90.0f, 0.0f))) {
            ClearPendingAssetOpen();
            statusMessage_ =
                "VFX asset switch canceled; current edits are unchanged";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
#else
        (void)database;
#endif
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

        if (ImGui::Button(VfxUiText("New").c_str(), { 64.0f, 28.0f })) {
            ImGui::OpenPopup("Create VFX Asset");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_.IsOpen());
        if (ImGui::Button(VfxUiText("Save").c_str(), { 64.0f, 28.0f })) {
            (void)Save(database, statusMessage_);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_.CanUndo());
        if (ImGui::Button(VfxUiText("Undo").c_str(), { 58.0f, 28.0f })) {
            (void)Undo(statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_.CanRedo());
        if (ImGui::Button(VfxUiText("Redo").c_str(), { 58.0f, 28.0f })) {
            (void)Redo(statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SameLine();
        const char* state = VFX::HasVfxErrors(diagnostics_)
            ? "Compile errors"
            : document_.IsDirty()
                ? VfxUiText("Unsaved").c_str()
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
        if (ImGui::Button(VfxUiText("Scene").c_str(), { exitWidth, 28.0f })) {
            result.exitToSceneRequested = true;
        }

        if (ImGui::BeginPopupModal(
                "Create VFX Asset",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(VfxUiText("Start from a template or create an empty effect.").c_str());
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
            if (ImGui::Button(VfxUiText("Cancel").c_str())) {
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
            ImGui::SeparatorText(VfxUiText("Start From Preset").c_str());
            ImGui::TextDisabled(
                VfxUiText("Choose a starting point. Every layer remains editable.").c_str());
            if (ImGui::BeginListBox("##vfx_presets", { 320.0f, 180.0f })) {
                std::string lastCategory{};
                for (const auto& preset :
                    VFX::PRESETS::GetVfxPresetCatalog()) {
                    if (preset.category != lastCategory) {
                        lastCategory = preset.category;
                        ImGui::SeparatorText(VfxUiText(preset.category).c_str());
                    }
                    ImGui::PushID(preset.id.c_str());
                    const std::string label = document_.IsDirty()
                        ? "Discard and create: " + preset.displayName : preset.displayName;
                    if (ImGui::Selectable(label.c_str())) {
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
                        ImGui::TextUnformatted(VfxUiText(preset.summary).c_str());
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
