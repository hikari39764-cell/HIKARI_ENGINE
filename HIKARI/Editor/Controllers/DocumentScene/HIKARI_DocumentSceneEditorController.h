#pragma once
#include "Editor/Authoring/HIKARI_TerrainAuthoringOperations.h"
#include "Editor/Authoring/HIKARI_SceneObjectOperations.h"
#include "Editor/Authoring/HIKARI_EditorAuthoringSessionHost.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"

#include "Editor/Diagnostics/HIKARI_RuntimeDebugWindow.h"
#include "Editor/Agent/HIKARI_AgentBridge.h"
#include "Editor/Agent/HIKARI_AgentEngineRoots.h"
#include "Editor/Agent/HIKARI_AgentHost.h"
#include "Editor/Agent/HIKARI_AgentPanelState.h"
#include "Editor/Agent/HIKARI_AgentPlayRun.h"
#include "Editor/Play/HIKARI_BackgroundPlayRun.h"
#include "Editor/Diagnostics/HIKARI_EditorCameraPath.h"
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
#include "Editor/Workspaces/LookDev/HIKARI_LookDevArrange.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevLightTools.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevShapeHandles.h"
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
class GameObject;
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
    EDITOR::AGENT::AgentHost agentHost_{};
    // play.background：保存前の場面を後ろの無頭の引擎で遊ばせる。
    EDITOR::BackgroundPlayRun backgroundPlay_{};
    EDITOR::EditorOperationResult StartBackgroundPlay(const nlohmann::json& parameters);
    // 智能体が書き込めない引擎目录（この编辑器の安装先）。
    std::vector<std::filesystem::path> engineRoots_ = EDITOR::AGENT::FindEngineRoots();
    EDITOR::EditorSettingsPanel editorSettingsPanel_{};
    EDITOR::EditorShell editorShell_{};
    bool HasForegroundDocument(EDITOR::EditorWorkspaceId workspace) const;
    // ワークスペースの文書状態。includeSessions では AI が表示中のバックグラウンド文書を優先する。
    nlohmann::json DescribeWorkspaceDocument(EDITOR::EditorWorkspaceId workspace, bool includeSessions) const;
    void DrawGlobalShell(DocumentSceneBase& scene);
    EDITOR::AGENT::AgentBridge agentBridge_{};
    // 起動に失敗したら設定を切り替えるまで毎フレーム再試行しない。
    bool agentBridgeStartFailed_ = false;
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
    void DrawLookDevObjectsWindow(DocumentSceneBase& scene);
    // 灯光の窓：一灯一行（开关・单独显示・亮度・颜色・阴影の状態）と、从视角放灯・三点布光。
    void DrawLookDevLightsWindow(DocumentSceneBase& scene);
    // 今の視点から画面の中心へ向けたスポット灯を scene.create_light で置く。
    void PlaceLightAtView(DocumentSceneBase& scene);
    // 布景の操作（RegisterLookDevOperations）。画面のボタンと智能体が同じ操作を呼ぶ。
    void RegisterLookDevOperations();
    // 画面のボタンから布景の操作を呼び、失敗したら視口の札に出す。
    EDITOR::EditorOperationResult RunLookDevOperation(const std::string& id, const nlohmann::json& parameters);
    // 画面のボタンで呼んだ操作が作った物（createdIds・groupId・keyId・objectId）を選ぶ。操作そのものは選択を変えない
    // （智能体が人の選択や視点を変えずに並行して作業できるように）。
    void SelectLookDevOperationResult(DocumentSceneBase& scene, const EDITOR::EditorOperationResult& result);
    // 布景の操作で物を作る：その場で一つの取り消しに積む（批量や“这一轮”の取り消しの組にも入る）。選択は変えない。
    std::vector<SceneObjectId> CreateLookDevObjects(DocumentSceneBase& scene, std::string label,
                                                    const std::function<std::vector<GameObject*>()>& create);
    // scene.create_light：種類・位置・向き（aimAt か rotation）・LightSettings の部分 JSON で灯を一つ作る。
    EDITOR::EditorOperationResult CreateLookDevLight(const nlohmann::json& p);
    // lighting.three_point：物体の外接箱を照らす主光・補光・輪郭光。
    EDITOR::EditorOperationResult CreateThreePointLights(const nlohmann::json& p);
    // 視口で主光を動かす（美術布景は方位と高度、昼夜は時刻）。ドラッグ中は一つの取り消しにまとめる。
    void DragLookDevMainLight(DocumentSceneBase& scene, float azimuthDegrees, float elevationDegrees, bool dragging);
    // 布景の視口の手柄：L + ドラッグの主光と、選んだ灯の範囲・角度・大きさ。選択より先に処理し、マウスを使えば true。
    bool UpdateLookDevHandles(DocumentSceneBase& scene, const EDITOR::SceneViewportRect& viewport, bool hovered,
                              bool focused, bool gizmoCapture);
    // 一つの物体の文書と実行時の部品を書き換え続け（視口のドラッグ・一覧の数値欄）、FinishLookDevObjectDrag で
    // 一つの取り消しとして積む。
    void ContinueLookDevObjectDrag(DocumentSceneBase& scene, SceneObjectId objectId, std::string_view label,
                                   const std::function<bool(SceneObjectData&)>& edit);
    void FinishLookDevObjectDrag(DocumentSceneBase& scene);
    void EditLookDevLight(DocumentSceneBase& scene, SceneObjectId objectId, const LightSettings& settings);
    // 選んだ白模の寸法と開口の手柄。ドラッグ中は箱の線だけを描き、離した時に一度だけ形を作り直す。
    bool UpdateLookDevShapeHandles(DocumentSceneBase& scene, const EDITOR::SceneViewportRect& viewport, bool hovered,
                                   bool gizmoCapture);
    // 開口（床の穴、壁の門窓）と挖空方块の欄。開口は箱の切り抜きなので後から手柄で動かせる。
    void DrawLookDevOpeningsSection(DocumentSceneBase& scene);
    // procedural.opening：開口の追加（種類の既定の大きさ）・変更・削除。
    EDITOR::EditorOperationResult EditLookDevOpening(const nlohmann::json& p);
    // procedural.subtract：切る箱と重なる白模の箱を、その重なりで切り抜く（既定で切る箱を消す）。
    EDITOR::EditorOperationResult SubtractLookDevShapes(const nlohmann::json& p);
    // 整理（揃え・等間隔・落地・编组・镜像复制・阵列・换成模型）。物体の窓の上に出す。
    void DrawLookDevArrangeSection(DocumentSceneBase& scene);
    // scene.arrange：揃え・等間隔・落地・编组・镜像复制・阵列。
    EDITOR::EditorOperationResult ArrangeLookDevObjects(const nlohmann::json& p);
    // scene.replace_model：白模の箱を模型に替え、箱に収まる大きさで底を合わせる。
    EDITOR::EditorOperationResult ReplaceLookDevShapeWithModel(const nlohmann::json& p);
    // 根の物（子を含む）の世界の外接箱。描かれない物は位置の点。
    std::vector<EDITOR::LookDevArrangeItem> CollectLookDevArrangeItems(DocumentSceneBase& scene,
                                                                       std::span<const SceneObjectId> roots);
    bool MoveLookDevItems(DocumentSceneBase& scene, std::string label,
                          const std::vector<EDITOR::LookDevArrangeItem>& items, const std::vector<MATH::Vec3>& moves);
    // 根の物（子を含む）を copies 回複製し、place で複製の位置と向きを決める（remapped は元の ID から複製の ID）。
    // 複製した根を返す。
    using LookDevClonePlacement =
        std::function<void(size_t copy, SceneDocument& document, const std::unordered_map<uint64_t, uint64_t>& remapped,
                           std::span<const SceneObjectId> roots)>;
    std::vector<SceneObjectId> CloneLookDevObjects(DocumentSceneBase& scene, std::string label,
                                                   std::span<const SceneObjectId> roots, size_t copies,
                                                   const LookDevClonePlacement& place);
    std::vector<SceneObjectId> CreateLookDevArray(DocumentSceneBase& scene, std::span<const SceneObjectId> roots,
                                                  const EDITOR::LookDevArraySettings& settings);
    std::vector<SceneObjectId> MirrorLookDevObjects(DocumentSceneBase& scene, std::span<const SceneObjectId> roots,
                                                    int axis);
    SceneObjectId GroupLookDevObjects(DocumentSceneBase& scene, std::span<const SceneObjectId> roots,
                                      const std::string& name);
    // 文書をまとめて書き換え、変わった物を実行時へ反映して一つの取り消しに積む。edit が false なら何もしない。
    bool EditLookDevDocument(DocumentSceneBase& scene, std::string label,
                             const std::function<bool(SceneDocument&)>& edit);
    // 布景の工作区の状態（editor.state の当前工作区）。置く物・吸着・見え方・露出・影の名額・単独表示・機位・環境。
    nlohmann::json DescribeLookDevState() const;
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
    void EnsureAgentBridge(DocumentSceneBase& scene);
    // 外部智能体（本地桥）からの一つの操作。公開範囲と访问模式を確かめて操作表で実行する。主线程で呼ぶ。
    EDITOR::EditorOperationResult ExecuteAgentAction(const std::string& operation, const nlohmann::json& parameters);
    // 人が場面を操作している途中か（視口の手柄、布景の物・灯・寸法・开口・主光のドラッグ、地形の笔刷、数値のドラッグ）。
    // 智能体の修改はその間待たせる。
    bool IsUserEditingScene() const;
    // editor.state と operations.batch（HIKARI_DocumentSceneEditorAgentOperations.cpp）。
    void RegisterAgentOperations();
    // project.features / project.features.edit（HIKARI_DocumentSceneEditorProjectOperations.cpp）。
    void RegisterProjectOperations();
    EDITOR::EditorOperationResult DescribeEditorState();
    EDITOR::EditorOperationResult ExecuteOperationBatch(const nlohmann::json& parameters);
    // ExecuteAgentAction の実行中か。一括操作の各手順も智能体の操作として確かめる。
    bool executingAgentAction_ = false;
    // 設定の「写入智能体说明」の結果。
    std::string agentFilesMessage_{};
    // Play・運行時・ログの操作（HIKARI_DocumentSceneEditorPlayOperations.cpp）。
    void RegisterPlayOperations();
    // play.run と --agent-play-run を毎フレーム進める。
    void TickAgentPlayRun();
    // --agent-play-run の結果を result.json に書く（書き終えたら backgroundResult_ を空にする）。
    void WriteBackgroundResult(const EDITOR::EditorOperationResult& result);
    EDITOR::EditorOperationResult DescribePlayState() const;
    EDITOR::EditorOperationResult StartAgentPlay();
    EDITOR::EditorOperationResult StartAgentPlayRun(const nlohmann::json& parameters);
    EDITOR::EditorOperationResult PollAgentPlayRun(uint64_t runId) const;
    // Draw が毎フレーム受け取る Play 会話。操作はこれを通して Play を始め・止める。
    EDITOR::EditorPlaySession* aiPlaySession_ = nullptr;
    // project.reload が使う、Draw が毎フレーム受け取る項目会話。
    EDITOR::EditorProjectSessionController* aiProjectSession_ = nullptr;
    std::optional<EDITOR::AGENT::AgentPlayRun> agentPlayRun_{};
    uint64_t nextAgentPlayRunId_ = 1;
    // --agent-play-run で渡された依頼（後台試玩・hikari headless）。場面が開いたら play.run を一回走らせる。
    std::optional<std::filesystem::path> pendingBackgroundRun_{};
    // その結果を書く result.json（走っている間だけ）。
    std::optional<std::filesystem::path> backgroundResult_{};
    // 診断用の --editor-camera-path。编辑中は毎フレーム编辑相机をこの経路に置く。
    std::optional<EDITOR::EditorCameraPath> cameraPath_{};
    // 助手页签の「外部智能体」（HIKARI_DocumentSceneEditorAgentPanel.cpp）。
    void DrawAgentPanel(DocumentSceneBase& scene);
    void StartAgentTurn(DocumentSceneBase& scene);
    void TickAgentPanel();
    void RecordAgentOperation(const std::string& action, const EDITOR::EditorOperationResult& result);
    EDITOR::AGENT::AgentPanelState agentPanel_{};
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
    struct LookDevObjectDrag {
        SceneObjectId objectId{};
        std::string label{};
        std::vector<SceneObjectData> beforeObjects{};
        SceneCameraSettings beforeCamera{};
        bool dirtyBefore = false;
        bool changed = false;
    };
    std::optional<LookDevObjectDrag> lookDevDrag_{};
    struct LookDevShapeDrag {
        SceneObjectId objectId{};
        EDITOR::LookDevShapeHandle handle{};
        ProceduralMeshSettings start{};
        MATH::Mat4 startWorld{};
        ProceduralMeshSettings result{};
        MATH::Vec3 worldShift{};
        // 開口の手柄なら切り抜きの番号（-1 は形の面）、moveCut なら開口の中心を動かす。
        int cutIndex = -1;
        bool moveCut = false;
    };
    std::optional<LookDevShapeDrag> lookDevShapeDrag_{};
    EDITOR::LookDevLightHandle lookDevLightHandle_ = EDITOR::LookDevLightHandle::None;
    bool lookDevMainLightDrag_ = false;
};

} // namespace HIKARI
