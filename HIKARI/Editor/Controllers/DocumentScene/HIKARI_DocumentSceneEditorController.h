#pragma once
#include "Editor/Authoring/HIKARI_TerrainAuthoringOperations.h"
#include "Editor/Authoring/HIKARI_SceneObjectOperations.h"
#include "Editor/Authoring/HIKARI_EditorAuthoringSessionHost.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"

#include "Editor/AI/HIKARI_AI_AssistantPanel.h"
#include "Editor/Diagnostics/HIKARI_RuntimeDebugWindow.h"
#include "Editor/AI/HIKARI_AI_LocalBridge.h"
#include "Editor/HIKARI_DebugCameraPanel.h"
#include "Editor/HIKARI_DebugMenuBar.h"
#include "Editor/Export/HIKARI_GameExportJob.h"
#include "Editor/Projects/HIKARI_ProjectCodeJob.h"
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
#include "Editor/Preview/HIKARI_SceneViewSeriesCapture.h"
#include "Editor/Quality/HIKARI_QualityPanel.h"
#include "Editor/Settings/HIKARI_EditorSettingsPanel.h"
#include "Editor/Shell/HIKARI_EditorShell.h"
#include "Editor/Assets/Workspace/HIKARI_ResourceWorkspacePanel.h"
#include "Editor/Panels/HIKARI_ValidationLabPanel.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Editor/Viewport/HIKARI_SceneViewportSelectionService.h"
#include "Editor/Views/HIKARI_EditorViewGuides.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevWorkspace.h"
#include "Editor/Windows/HIKARI_SceneAuthoringUtilityWindows.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Editor/Workspaces/Ui/HIKARI_UiWorkspaceController.h"
#include "Editor/Workspaces/TextStyle/HIKARI_TextStyleWorkspaceController.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialogueWorkspaceController.h"
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
class EditorProjectSessionController;
class EditorPlaySession;
class GameProjectEditorWorkspaceHost;
} // namespace EDITOR

class DocumentSceneEditorController {
public:
    DocumentSceneEditorController();
    ~DocumentSceneEditorController();
    EDITOR::EditorAuthoringSessionHost& AuthoringSessions() noexcept { return authoringSessions_; }
    EDITOR::EditorOperationRegistry& Operations() noexcept { return operationRegistry_; }
    void SetProjectWorkspaceHost(EDITOR::GameProjectEditorWorkspaceHost* host) noexcept {
        projectWorkspaceHost_ = host;
    }

    void Draw(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession,
              EDITOR::EditorProjectSessionController& projectSession);

private:
    RUNTIME::RuntimeDebugWindow runtimeDebugWindow_{};
    void RegisterEditorOperations();
    // 環境・光照ベイクの操作。
    void RegisterEnvironmentOperations();
    void RegisterSceneAuthoringSessions();
    EDITOR::EditorOperationResult ExecuteBackgroundSceneOperation(const EDITOR::EditorOperationDefinition& definition,
                                                                  const nlohmann::json& parameters);
    void RegisterSceneFolderOperations();
    void RegisterSceneLayoutOperations();
    EDITOR::EditorOperationResult ExecuteSceneLayoutOperation(bool apply, const nlohmann::json& parameters);
    EDITOR::EditorOperationResult ExecuteSceneFolderOperation(std::string_view operation,
                                                              const nlohmann::json& parameters);
    EditorContext context_{};
    SelectionSyncService selectionSync_{};

    DebugMenuBar debugMenuBar_{};
    EDITOR::GameExportJob exportJob_{};
    EDITOR::ProjectCodeJob projectCodeJob_{};
    bool projectBuildAllowed_ = false;
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
    EDITOR::EditorAuthoringSessionHost authoringSessions_{};
    std::string authoringProjectRoot_{};
    uint64_t aiSceneCaptureTicket_ = 0, aiSceneCaptureRevision_ = 0;
    std::string aiSceneCaptureIdentity_{};
    EDITOR::EditorOperationResult PollAiPreviewCapture(uint64_t id);
    void CancelAiSceneCapture();
    EDITOR::SceneViewSeriesCapture sceneViewSeries_{};
    EDITOR::AiAssistantSession aiAssistantSession_{};
    EDITOR::AiAssistantPanel aiAssistantPanel_{aiAssistantSession_};
    EDITOR::EditorSettingsPanel editorSettingsPanel_{};
    EDITOR::EditorShell editorShell_{};
    bool HasForegroundDocument(EDITOR::EditorWorkspaceId workspace) const;
    // ワークスペースの文書状態。includeSessions では AI が表示中のバックグラウンド文書を優先する。
    nlohmann::json DescribeWorkspaceDocument(EDITOR::EditorWorkspaceId workspace, bool includeSessions) const;
    void DrawGlobalShell(DocumentSceneBase& scene);
    EDITOR::AI_ASSISTANT::AiLocalBridge aiLocalBridge_{};
    bool aiBridgeStarted_ = false;
    EnvironmentPanel environmentPanel_{};
    QualityPanel qualityPanel_{};
    EDITOR::EditorToolHost toolHost_{};
    EDITOR::SystemAuthoringRegistry systemAuthoringRegistry_{};
    EDITOR::EditorWorkspaceHost workspaceHost_{};
    EDITOR::GameProjectEditorWorkspaceHost* projectWorkspaceHost_ = nullptr;
    EDITOR::EditorOperationRegistry operationRegistry_{};
    DocumentSceneBase* aiOperationScene_ = nullptr;
    EDITOR::GameplayCameraWorkspaceController gameplayCameraWorkspaceController_;
    EDITOR::UiWorkspaceController uiWorkspaceController_;
    EDITOR::TextStyleWorkspaceController textWorkspaceController_;
    void RegisterUiOperations();
    EDITOR::GameplayCameraWorkspaceController& GameplayCameraWorkspace() {
        return authoringSessions_.ResolveController("gameplay_camera", gameplayCameraWorkspaceController_);
    }
    const EDITOR::GameplayCameraWorkspaceController& GameplayCameraWorkspace() const {
        return const_cast<DocumentSceneEditorController*>(this)->GameplayCameraWorkspace();
    }
    EDITOR::EditorOperationResult EditCameraFlow(const nlohmann::json& parameters);
    EDITOR::EditorOperationResult BindGameplayCamera(const nlohmann::json& parameters);
    // 区域（玩家が入ると機位を切り替える箱）の作成・変更・削除。場面の履歴に載る。
    EDITOR::EditorOperationResult EditCameraZone(const nlohmann::json& parameters);
    EDITOR::CinematicsWorkspaceController cinematicsWorkspaceController_{};
    EDITOR::ModelCollisionWorkspaceController modelCollisionWorkspaceController_{};
    EDITOR::ModelMaterialWorkspaceController modelMaterialWorkspaceController_{};
    EDITOR::AnimationStateMachineWorkspaceController animationStateMachineWorkspaceController_{};
    EDITOR::RuntimeBindingWorkspaceController runtimeBindingWorkspaceController_{};
    EDITOR::VfxWorkspaceController vfxWorkspaceController_{};
    EDITOR::DialogueWorkspaceController dialogueWorkspaceController_{};
    EDITOR::TerrainSurfaceWorkspaceController terrainSurfaceWorkspaceController_{};
    EDITOR::VfxWorkspaceController& VfxWorkspace() {
        return authoringSessions_.ResolveController("vfx", vfxWorkspaceController_);
    }
    const EDITOR::VfxWorkspaceController& VfxWorkspace() const {
        return const_cast<DocumentSceneEditorController*>(this)->VfxWorkspace();
    }
    EDITOR::ModelMaterialWorkspaceController& ModelMaterialWorkspace() {
        return authoringSessions_.ResolveController("model_material", modelMaterialWorkspaceController_);
    }
    const EDITOR::ModelMaterialWorkspaceController& ModelMaterialWorkspace() const {
        return const_cast<DocumentSceneEditorController*>(this)->ModelMaterialWorkspace();
    }
    EDITOR::AnimationStateMachineWorkspaceController& AnimationStateMachineWorkspace() {
        return authoringSessions_.ResolveController("animation_state_machine",
                                                    animationStateMachineWorkspaceController_);
    }
    const EDITOR::AnimationStateMachineWorkspaceController& AnimationStateMachineWorkspace() const {
        return const_cast<DocumentSceneEditorController*>(this)->AnimationStateMachineWorkspace();
    }
    EDITOR::DialogueWorkspaceController& DialogueWorkspace() {
        return authoringSessions_.ResolveController("dialogue", dialogueWorkspaceController_);
    }
    const EDITOR::DialogueWorkspaceController& DialogueWorkspace() const {
        return const_cast<DocumentSceneEditorController*>(this)->DialogueWorkspace();
    }
    EDITOR::RuntimeBindingWorkspaceController& RuntimeBindingWorkspace() {
        return authoringSessions_.ResolveController("runtime_binding", runtimeBindingWorkspaceController_);
    }
    const EDITOR::RuntimeBindingWorkspaceController& RuntimeBindingWorkspace() const {
        return const_cast<DocumentSceneEditorController*>(this)->RuntimeBindingWorkspace();
    }
    EDITOR::ModelCollisionWorkspaceController& ModelCollisionWorkspace() {
        return authoringSessions_.ResolveController("model_collision", modelCollisionWorkspaceController_);
    }
    const EDITOR::ModelCollisionWorkspaceController& ModelCollisionWorkspace() const {
        return const_cast<DocumentSceneEditorController*>(this)->ModelCollisionWorkspace();
    }
    EDITOR::CinematicsWorkspaceController& CinematicsWorkspace() {
        return authoringSessions_.ResolveController("cinematics", cinematicsWorkspaceController_);
    }
    const EDITOR::CinematicsWorkspaceController& CinematicsWorkspace() const {
        return const_cast<DocumentSceneEditorController*>(this)->CinematicsWorkspace();
    }
    DebugCameraPanel debugCameraPanel_{};
    EDITOR::EditorTransformGizmo transformGizmo_{};
    // 場面の視口の構図補助線（工作区ごとの視口と同じく、この視口だけの開閉）。
    EDITOR::EditorViewGuides sceneViewGuides_{};
    EDITOR::LookDevWorkspace lookDevWorkspace_{};
    void DrawGameViewportWindow(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession,
                                std::string_view stableWindowId = "Game View");
    void HandleTerrainBrush(DocumentSceneBase& scene, const EDITOR::SceneViewportRect& viewport, bool viewportHovered);
    void ToggleGamePreview(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession);
    void LaunchWindowedGamePreview(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession);
    bool PrepareGamePreview(DocumentSceneBase& scene);
    bool SaveRenderQualityProfile(DocumentSceneBase& scene);
    void DrawWorkspaceContent(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession);
    void DrawSceneWorkspace(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession);
    void DrawCinematicsWorkspace(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession);
    void DrawTerrainSurfaceWorkspace(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession);
    // 布景：視口・摆放（置く物・機位・物体）・检查器・灯光与画面。場面の物体と履歴は場面の工作区と同じ経路。
    void DrawLookDevWorkspace(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession);
    void DrawLookDevPlaceWindow(DocumentSceneBase& scene);
    // 機位の一覧（押すとその視点へ、通过相机看、今の視点で更新）と「保存当前视角」。
    void DrawLookDevCameras(DocumentSceneBase& scene);
    void SaveViewAsCamera(DocumentSceneBase& scene);
    void MoveEditorCameraToObject(DocumentSceneBase& scene, SceneObjectId cameraObjectId);
    // 布景の視口：数字キーで機位へ飛び、置く物を選んでいればカーソルの下へ下書きを描いてクリックで置く。
    void UpdateLookDevViewport(DocumentSceneBase& scene, const EDITOR::SceneViewportRect& viewport, bool hovered,
                               bool focused, bool gizmoCapture);
    void PlaceLookDevItem(DocumentSceneBase& scene, EDITOR::LookDevPlaceItem item,
                          const EDITOR::LookDevPlacementPose& pose);
    // 物体の作成・检查器・視口の変形・命令が出した履歴と確認の窓を処理する（場面と布景の工作区で共通）。
    void ProcessSceneAuthoringRequests(DocumentSceneBase& scene);
    // 環境の窓（lookDev なら布景の灯光与画面）。参照の変更は場面の履歴へ積む。編集が続いていれば true。
    bool DrawEnvironmentWindow(DocumentSceneBase& scene, bool lookDev);
    // showGameView が無効な時は埋め込み入力と Game View の矩形を解放する。
    void DrawOptionalGameViewport(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession,
                                  std::string_view stableWindowId);
    void HandleResourceWorkspaceRequests(DocumentSceneBase& scene);
    void DrawEnvironmentPanels(DocumentSceneBase& scene);
    // 独立工作区のフレーム後処理: 場景への復帰要求・状態文言・場景切替確認。
    void FinishWorkspaceFrame(DocumentSceneBase& scene, bool exitToScene, const std::string& statusMessage);
    void SaveRenderQualityWhenIdle(DocumentSceneBase& scene);
    // 履歴に載っていない未保存変更。履歴コマンドを積む直前の状態として記録する。
    bool IsDirtyOutsideHistory(const DocumentSceneBase& scene) const;
    void DrawSceneWorkspaceWindow(DocumentSceneBase& scene);
    void DrawInspectorWindow(DocumentSceneBase& scene);
    void SelectViewportObjects(DocumentSceneBase& scene, const std::vector<SceneObjectId>& objectIds,
                               EditorObjectSelectionMode mode);
    void FocusSceneObjects(DocumentSceneBase& scene, const std::vector<SceneObjectId>& objectIds);
    void DrawViewportContextMenu(DocumentSceneBase& scene);
    void DrawDebugViewWindow(DocumentSceneBase& scene, bool& open);
    void HandleGameViewportAssetDrop(DocumentSceneBase& scene);
    // 場面を開く要求。未保存の変更があれば確認を待ち、無ければすぐ開く。
    void RequestSceneOpen(DocumentSceneBase& scene, const AssetGuid& sceneGuid);
    void DrawPendingSceneOpenModal(DocumentSceneBase& scene);
    void EnsureAiLocalBridge();
    EDITOR::EditorOperationResult ExecuteTerrainOperation(EDITOR::TerrainAuthoringOperation operation,
                                                          const nlohmann::json& parameters);
    EDITOR::EditorOperationResult ExecuteSceneObjectOperation(EDITOR::SceneObjectOperation operation,
                                                              const nlohmann::json& parameters);
    EDITOR::EditorOperationResult ExecuteAiCapturePreview(const nlohmann::json& parameters);

    EDITOR::EditorOperationResult ExecuteAiAssetImport(const nlohmann::json& parameters);
    EDITOR::EditorOperationResult ExecuteAiAssetImportStatus();
    EDITOR::EditorOperationResult ExecuteAiUndoLast();

    EDITOR::EditorOperationResult ExecuteSceneSave();
    // 場面と、編集中の資産文書（環境・天気・描画の様式・後処理・ゲームカメラ・外部の演出）をまとめて保存する。
    EDITOR::EditorOperationResult ExecuteSaveAll();
    // 場面・未保存の資産文書・編集中の演出序列をまとめて保存する。失敗は「名前: 理由」で failures に並べる。
    bool SaveAllDocuments(DocumentSceneBase& scene, std::vector<std::string>& saved, std::string& failures);
    // 未保存の場面と資産の名前を区切り文字で並べる（状態バーと場面切り替えの確認）。
    std::string UnsavedDocumentSummary(const DocumentSceneBase& scene);
    // 未保存の資産文書（場面と演出を除く）。全部保存と未保存の表示に使う。
    std::vector<EDITOR::EditorAssetDocument*> UnsavedAssetDocuments();
    bool OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid);
    void SyncDocumentHistory(DocumentSceneBase& scene);
    void ConfigureEditorCommands(DocumentSceneBase& scene);
    bool SaveSceneDocument(DocumentSceneBase& scene);
    void ExecuteSceneDocumentHistory(DocumentSceneBase& scene, bool redo);
    void CommitSceneObjectAuthoringHistory(DocumentSceneBase& scene, SceneObjectAuthoringHistoryRequest history);
    void ApplyHistoryResult(DocumentSceneBase& scene, const EDITOR::EditorHistoryResult& result, bool redo);
    // objectChanges があれば runtime world は差分反映、無ければ作り直す。
    void ApplyDocumentImpact(DocumentSceneBase& scene, EDITOR::EditorDocumentImpact impact,
                             const SceneObjectChanges* objectChanges = nullptr);
    void CommitAppliedDocumentCommand(
        DocumentSceneBase& scene, std::unique_ptr<EDITOR::IEditorDocumentCommand> command, uint64_t mergeGroup,
        bool externalDirtyBefore, EDITOR::EditorDocumentImpact immediateImpact = EDITOR::EditorDocumentImpact::None);
    // サブワークスペース滞在中に Scene 共有カメラ（Editor/Debug）が
    // 変化しても、戻ったときの Game View を入場時の視点へ復元する。
    void SnapshotSceneViewCameras(DocumentSceneBase& scene);
    void RestoreSceneViewCameras(DocumentSceneBase& scene);
    bool HasKnownUnsavedProjectChanges(const DocumentSceneBase& scene) const noexcept;

    void RecordCinematicsHistory(DocumentSceneBase& scene, SceneCinematicsSettings before, uint64_t mergeGroup,
                                 bool externalDirtyBefore);

    AssetGuid pendingSceneOpenGuid_{};
    std::string viewportDropMessage_{};
    bool renderQualitySavePending_ = false;
    EDITOR::EditorDocumentHistory documentHistory_{};
    EDITOR::EditorCommandRouter commandRouter_{};
    EDITOR::SceneObjectTransformHistorySession viewportTransformHistory_{};
    bool sceneViewCameraSnapshotValid_ = false;
    Camera3D sceneViewEditorCameraSnapshot_{};
    DebugCameraController3D sceneViewDebugCameraSnapshot_{};
    bool historyExternalDirty_ = false;
    bool terrainStrokeActive_ = false;
    bool terrainBrushStrokeChanged_ = false;
    SceneObjectId terrainBrushTargetObjectId_{};
    RuntimeObjectHandle terrainBrushTargetRuntimeHandle_{};
    SceneComponentId terrainBrushTargetComponentId_{};
    MATH::Vec3 terrainBrushPreviousLocalHit_{};
    bool terrainBrushHasPreviousLocalHit_ = false;
    bool terrainBrushDirtyBefore_ = false;
};

} // namespace HIKARI
