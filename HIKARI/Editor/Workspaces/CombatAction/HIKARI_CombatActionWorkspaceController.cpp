#include "Editor/Workspaces/CombatAction/HIKARI_CombatActionWorkspaceController.h"

#include <algorithm>
#include <utility>

#include "Core/HIKARI_TimeService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {
    void CombatActionWorkspaceController::ApplyWorkspaceActivation(
        DocumentSceneBase& scene,
        const EditorWorkspaceActivation& activation) {
        if (activation.previous == EditorWorkspaceId::CombatAction &&
            activation.current != EditorWorkspaceId::CombatAction) {
            RestorePreviewPose(scene);
            directorViewPanel_.LeaveWorkspace();
            playing_ = false;
        }
        if (activation.current != EditorWorkspaceId::CombatAction) return;
        if (activation.previous != EditorWorkspaceId::CombatAction) {
            directorViewPanel_.ResetForScene();
        }
        if (activation.combatActionAssetGuid) {
            if (document_.IsDirty() &&
                document_.GetAssetGuid() != *activation.combatActionAssetGuid) {
                statusMessage_ =
                    "Save or revert the current Combat Action before opening another asset";
            } else {
                (void)OpenAsset(
                    scene,
                    *activation.combatActionAssetGuid,
                    statusMessage_);
            }
        } else if (!document_.IsOpen()) {
            document_.New();
            statusMessage_ = "New Combat Action";
        }
        NormalizeSelection();
    }

    void CombatActionWorkspaceController::DrawDockSpace(
        bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) return;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiID dockspaceId = ImGui::GetID(
            "HIKARI_CombatActionDockSpace_v1");
        ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspaceId);
        const bool needsLayout = dockNode == nullptr || dockNode->IsLeafNode();
        if (needsLayout || resetDefaultDockLayout) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(
                dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
            ImGuiID center = dockspaceId;
            ImGuiID timeline = 0u;
            ImGuiID left = 0u;
            ImGuiID right = 0u;
            ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Down, 0.34f, &timeline, &center);
            ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Left, 0.22f, &left, &center);
            ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Right, 0.29f, &right, &center);
            ImGui::DockBuilderDockWindow(
                "Action Library###CombatAction/Navigator", left);
            ImGui::DockBuilderDockWindow(
                "Action Preview###CombatAction/Preview", center);
            ImGui::DockBuilderDockWindow(
                "Action Inspector###CombatAction/Inspector", right);
            ImGui::DockBuilderDockWindow(
                "Diagnostics###CombatAction/Diagnostics", right);
            ImGui::DockBuilderDockWindow(
                "Action Timeline###CombatAction/Timeline", timeline);
            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
        (void)resetDefaultDockLayout;
#endif
    }

    CombatActionWorkspaceResult CombatActionWorkspaceController::Draw(
        DocumentSceneBase& scene,
        EditorContext& context,
        SelectionSyncService& selectionSync,
        EditorWorkspaceHost& workspaceHost,
        EditorCommandRouter& commandRouter) {
        CombatActionWorkspaceResult result{};
#if defined(HIKARI_WITH_EDITOR)
        (void)commandRouter;
        AdvancePreview(TIME::GetFrameContext().unscaledDt);
        DrawNavigator(scene, result);
        DrawPreview(scene, context, selectionSync, workspaceHost);
        DrawTimeline();
        DrawInspector(scene);
        DrawDiagnostics();
        ApplyPreviewPose(scene, context);
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)workspaceHost;
        (void)commandRouter;
#endif
        result.statusMessage = statusMessage_;
        return result;
    }

    void CombatActionWorkspaceController::BindDocumentCommands(
        EditorCommandRouter& commandRouter,
        DocumentSceneBase& scene) {
        commandRouter.Bind(
            EditorCommandId::SaveDocument,
            "Save Combat Action",
            document_.IsOpen(),
            [this, &scene]() { (void)Save(scene, statusMessage_); });
        commandRouter.Bind(
            EditorCommandId::Undo,
            "Undo Combat Action Edit",
            document_.CanUndo(),
            [this]() { (void)Undo(statusMessage_); });
        commandRouter.Bind(
            EditorCommandId::Redo,
            "Redo Combat Action Edit",
            document_.CanRedo(),
            [this]() { (void)Redo(statusMessage_); });
        commandRouter.Bind(
            EditorCommandId::DeleteSelection,
            "Delete Action Timeline Item",
            selection_.track != CombatActionTrackKind::None &&
                selection_.track != CombatActionTrackKind::Presentation,
            [this]() { DeleteSelection(); });
    }

    bool CombatActionWorkspaceController::Save(
        DocumentSceneBase& scene,
        std::string& outMessage) {
        return document_.Save(scene.GetAssetDatabase(), outMessage);
    }

    bool CombatActionWorkspaceController::Undo(std::string& outMessage) {
        if (!document_.Undo()) {
            outMessage = "Nothing to undo";
            return false;
        }
        NormalizeSelection();
        currentTimeSeconds_ = (std::min)(
            currentTimeSeconds_, document_.Definition().durationSeconds);
        outMessage = "Combat Action edit undone";
        return true;
    }

    bool CombatActionWorkspaceController::Redo(std::string& outMessage) {
        if (!document_.Redo()) {
            outMessage = "Nothing to redo";
            return false;
        }
        NormalizeSelection();
        currentTimeSeconds_ = (std::min)(
            currentTimeSeconds_, document_.Definition().durationSeconds);
        outMessage = "Combat Action edit redone";
        return true;
    }

    bool CombatActionWorkspaceController::NewAsset(std::string& outMessage) {
        if (document_.IsDirty()) {
            outMessage = "Current Combat Action has unsaved changes";
            return false;
        }
        document_.New();
        selection_ = {};
        currentTimeSeconds_ = 0.0f;
        outMessage = "New Combat Action created";
        return true;
    }

    bool CombatActionWorkspaceController::OpenAsset(
        DocumentSceneBase& scene,
        const AssetGuid& assetGuid,
        std::string& outMessage) {
        if (!assetGuid.IsValid()) {
            outMessage = "Select a Combat Action asset to open";
            return false;
        }
        if (document_.IsDirty() && document_.GetAssetGuid() != assetGuid) {
            outMessage = "Current Combat Action has unsaved changes";
            return false;
        }
        RestorePreviewPose(scene);
        if (!document_.Open(
                scene.GetAssetDatabase(), assetGuid, outMessage)) {
            return false;
        }
        selection_ = {};
        currentTimeSeconds_ = 0.0f;
        playing_ = false;
        outMessage = "Combat Action asset opened";
        return true;
    }

} // namespace HIKARI::EDITOR
