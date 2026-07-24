#pragma once

#include <optional>
#include <vector>

#include "Editor/HIKARI_DebugCameraPanel.h"
#include "Editor/HIKARI_DebugMenuBar.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/History/HIKARI_EditorDocumentHistory.h"
#include "Editor/History/HIKARI_SceneObjectTransformHistorySession.h"
#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Environment/HIKARI_EnvironmentPanel.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/Panels/HIKARI_SceneCreationPanel.h"
#include "Editor/Scene/Inspector/HIKARI_SceneInspectorPanel.h"
#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"
#include "Editor/Quality/HIKARI_QualityPanel.h"
#include "Editor/Assets/Workspace/HIKARI_ResourceWorkspacePanel.h"
#include "Editor/Panels/HIKARI_ValidationLabPanel.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Editor/Viewport/HIKARI_SceneViewportSelectionService.h"
#include "Editor/Windows/HIKARI_SceneAuthoringUtilityWindows.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/HIKARI_StatsPanel.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentToolbarController.h"
#include "Editor/HIKARI_TimePanel.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

    class DocumentSceneBase;
    namespace EDITOR {
        class EditorPlaySession;
    }

    class DocumentSceneEditorController {
    public:
        DocumentSceneEditorController();

        void Draw(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);

    private:
        EditorContext context_{};
        SelectionSyncService selectionSync_{};

        DebugMenuBar debugMenuBar_{};
        DocumentToolbarController documentToolbarController_{};
        EDITOR::SceneObjectCommandService sceneObjectCommands_{};
        EDITOR::SceneCreationPanel sceneCreationPanel_{};
        EDITOR::SceneInspectorPanel sceneInspectorPanel_{};
        EDITOR::SceneViewportSelectionService viewportSelectionService_{};

        HierarchyPanel hierarchyPanel_{};
        TimePanel timePanel_{};
        ResourceWorkspacePanel resourceWorkspacePanel_{};
        StatsPanel statsPanel_{};
        PerformanceAuditPanel performanceAuditPanel_{};
        EDITOR::SceneAuthoringUtilityWindows sceneAuthoringUtilityWindows_{};
        ValidationLabPanel validationLabPanel_{};
        EnvironmentPanel environmentPanel_{};
        QualityPanel qualityPanel_{};
        EDITOR::EditorToolHost toolHost_{};
        EDITOR::SystemAuthoringRegistry systemAuthoringRegistry_{};
        EDITOR::EditorWorkspaceHost workspaceHost_{};
        EDITOR::CinematicsWorkspaceController cinematicsWorkspaceController_{};
        EDITOR::ModelCollisionWorkspaceController modelCollisionWorkspaceController_{};
        EDITOR::AnimationStateMachineWorkspaceController
            animationStateMachineWorkspaceController_{};
        DebugCameraPanel debugCameraPanel_{};
        EDITOR::EditorTransformGizmo transformGizmo_{};
        void DrawGameViewportWindow(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        void ToggleGamePreview(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        void LaunchWindowedGamePreview(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        bool PrepareGamePreview(DocumentSceneBase& scene);
        bool SaveRenderQualityProfile(DocumentSceneBase& scene);
        void DrawSceneWorkspaceWindow(DocumentSceneBase& scene);
        void DrawInspectorWindow(DocumentSceneBase& scene);
        void SelectViewportObjects(
            DocumentSceneBase& scene,
            const std::vector<SceneObjectId>& objectIds,
            EditorObjectSelectionMode mode);
        void FocusSceneObjects(
            DocumentSceneBase& scene,
            const std::vector<SceneObjectId>& objectIds);
        void DrawViewportContextMenu(DocumentSceneBase& scene);
        void DrawDebugWorkspaceWindow(DocumentSceneBase& scene);
        void DrawDebugViewWindow(DocumentSceneBase& scene, bool& open);
        void HandleGameViewportAssetDrop(DocumentSceneBase& scene);
        void DrawPendingSceneOpenModal(DocumentSceneBase& scene);
        bool OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid);
        void SyncDocumentHistory(DocumentSceneBase& scene);
        void ConfigureEditorCommands(DocumentSceneBase& scene);
        void SaveSceneDocument(DocumentSceneBase& scene);
        void ExecuteSceneDocumentHistory(
            DocumentSceneBase& scene,
            bool redo);
        void ApplyHistoryResult(
            DocumentSceneBase& scene,
            const EDITOR::EditorHistoryResult& result,
            bool redo);
        void RecordCinematicsHistory(
            DocumentSceneBase& scene,
            SceneCinematicsSettings before,
            uint64_t mergeGroup,
            bool externalDirtyBefore);

        AssetGuid pendingSceneOpenGuid_{};
        std::string viewportDropMessage_{};
        bool renderQualitySavePending_ = false;
        EDITOR::EditorDocumentHistory documentHistory_{};
        EDITOR::EditorCommandRouter commandRouter_{};
        EDITOR::SceneObjectTransformHistorySession
            viewportTransformHistory_{};
        bool historyExternalDirty_ = false;
    };

} // namespace HIKARI
