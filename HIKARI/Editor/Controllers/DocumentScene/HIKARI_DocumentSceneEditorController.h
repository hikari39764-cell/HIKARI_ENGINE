#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"

#include "Editor/AI/HIKARI_AI_AssistantPanel.h"
#include "Editor/AI/HIKARI_AI_LocalBridge.h"
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
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/Panels/HIKARI_SceneCreationPanel.h"
#include "Editor/Scene/Inspector/HIKARI_SceneInspectorPanel.h"
#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"
#include "Editor/Quality/HIKARI_QualityPanel.h"
#include "Editor/Settings/HIKARI_EditorSettingsPanel.h"
#include "Editor/Assets/Workspace/HIKARI_ResourceWorkspacePanel.h"
#include "Editor/Panels/HIKARI_ValidationLabPanel.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Editor/Viewport/HIKARI_SceneViewportSelectionService.h"
#include "Editor/Windows/HIKARI_SceneAuthoringUtilityWindows.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/TerrainSurface/HIKARI_TerrainSurfaceWorkspaceController.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/HIKARI_StatsPanel.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentToolbarController.h"
#include "Editor/HIKARI_TimePanel.h"
#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

    class DocumentSceneBase;
    namespace EDITOR {
        class EditorPlaySession;
    }

    class DocumentSceneEditorController {
    public:
        DocumentSceneEditorController();
        ~DocumentSceneEditorController();

        void Draw(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);

    private:
        void RegisterEditorOperations();
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
        EDITOR::AiAssistantPanel aiAssistantPanel_{};
        EDITOR::EditorSettingsPanel editorSettingsPanel_{};
        EDITOR::AI_ASSISTANT::AiLocalBridge aiLocalBridge_{};
        bool aiBridgeStarted_ = false;
        EnvironmentPanel environmentPanel_{};
        QualityPanel qualityPanel_{};
        EDITOR::EditorToolHost toolHost_{};
        EDITOR::SystemAuthoringRegistry systemAuthoringRegistry_{};
        EDITOR::EditorWorkspaceHost workspaceHost_{};
        EDITOR::EditorOperationRegistry operationRegistry_{};
        DocumentSceneBase* aiOperationScene_ = nullptr;
        EDITOR::CinematicsWorkspaceController cinematicsWorkspaceController_{};
        EDITOR::ModelCollisionWorkspaceController modelCollisionWorkspaceController_{};
        EDITOR::ModelMaterialWorkspaceController modelMaterialWorkspaceController_{};
        EDITOR::AnimationStateMachineWorkspaceController
            animationStateMachineWorkspaceController_{};
        EDITOR::RuntimeBindingWorkspaceController
            runtimeBindingWorkspaceController_{};
        EDITOR::VfxWorkspaceController vfxWorkspaceController_{};
        EDITOR::TerrainSurfaceWorkspaceController
            terrainSurfaceWorkspaceController_{};
        DebugCameraPanel debugCameraPanel_{};
        EDITOR::EditorTransformGizmo transformGizmo_{};
        void DrawGameViewportWindow(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession,
            std::string_view stableWindowId = "Game View");
        void HandleSandTerrainBrush(
            DocumentSceneBase& scene,
            const EDITOR::SceneViewportRect& viewport,
            bool viewportHovered);
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
        void EnsureAiLocalBridge();
        EDITOR::EditorOperationResult ExecuteAiSceneSetTransform(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiTerrainFillLayer(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiSceneObjectQuery(
            bool snapshot);
        EDITOR::EditorOperationResult ExecuteAiSceneQuery(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiAssetListModels();
        EDITOR::EditorOperationResult ExecuteAiCapturePreview(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiMaterialGraphQuery(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiMaterialGraphLayout();
        EDITOR::EditorOperationResult ExecuteAiAssetImport(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiAssetImportStatus();
        EDITOR::EditorOperationResult ExecuteAiUndoLast();
        EDITOR::EditorOperationResult ExecuteAiSceneCreateObject(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiProceduralCreatePrimitive(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiMaterialEditParameter(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiMaterialGraphEdit(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteAiMaterialVerify(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteSceneSave();
        EDITOR::EditorOperationResult ExecuteSceneDeleteObject(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteSceneDuplicateObject(
            const nlohmann::json& parameters);
        EDITOR::EditorOperationResult ExecuteSceneCreateEmptyObject(
            const nlohmann::json& parameters);
        bool OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid);
        void SyncDocumentHistory(DocumentSceneBase& scene);
        void ConfigureEditorCommands(DocumentSceneBase& scene);
        bool SaveSceneDocument(DocumentSceneBase& scene);
        void ExecuteSceneDocumentHistory(
            DocumentSceneBase& scene,
            bool redo);
        void CommitSceneObjectAuthoringHistory(
            DocumentSceneBase& scene,
            SceneObjectAuthoringHistoryRequest history);
        void ApplyHistoryResult(
            DocumentSceneBase& scene,
            const EDITOR::EditorHistoryResult& result,
            bool redo);
        void ApplyDocumentImpact(
            DocumentSceneBase& scene,
            EDITOR::EditorDocumentImpact impact);
        void CommitAppliedDocumentCommand(
            DocumentSceneBase& scene,
            std::unique_ptr<EDITOR::IEditorDocumentCommand> command,
            uint64_t mergeGroup,
            bool externalDirtyBefore,
            EDITOR::EditorDocumentImpact immediateImpact =
                EDITOR::EditorDocumentImpact::None);
        // サブワークスペース滞在中に Scene 共有カメラ（Editor/Debug）が
        // 変化しても、戻ったときの Game View を入場時の視点へ復元する。
        void SnapshotSceneViewCameras(DocumentSceneBase& scene);
        void RestoreSceneViewCameras(DocumentSceneBase& scene);

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
        bool sceneViewCameraSnapshotValid_ = false;
        Camera3D sceneViewEditorCameraSnapshot_{};
        DebugCameraController3D sceneViewDebugCameraSnapshot_{};
        bool historyExternalDirty_ = false;
        bool sandTerrainStrokeActive_ = false;
        bool sandBrushStrokeChanged_ = false;
        SceneObjectId sandBrushTargetObjectId_{};
        RuntimeObjectHandle sandBrushTargetRuntimeHandle_{};
        SceneComponentId sandBrushTargetComponentId_{};
        nlohmann::json sandBrushBeforeComponentProperties_ =
            nlohmann::json::object();
        MATH::Vec3 sandBrushPreviousLocalHit_{};
        bool sandBrushHasPreviousLocalHit_ = false;
        bool sandBrushDirtyBefore_ = false;
    };

} // namespace HIKARI
