#pragma once
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Documents/HIKARI_GameplayCameraEditorDocument.h"
#include "Editor/Views/Director/HIKARI_DirectorViewPanel.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/HIKARI_CameraFollowSystem.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include <functional>
#include <optional>
#include <string_view>

namespace HIKARI {
class DocumentSceneBase;
struct SequenceAsset;
} // namespace HIKARI
namespace HIKARI::EDITOR {
class EditorCommandRouter;
struct GameplayCameraWorkspaceResult {
    bool togglePlay = false;
    INPUT::MouseCaptureRegion playViewport{};
    bool playViewportClicked = false;
};
class GameplayCameraWorkspaceController {
public:
    GameplayCameraEditorDocument& Document() { return document_; }
    const GameplayCameraEditorDocument& Document() const { return document_; }
    // 文書の状態。シェル、AI の文脈、バックグラウンドのセッションで共有する。
    // 表示中の文書があるか。DescribeDocumentState の open と同じ判定。
    bool IsDocumentOpen() const noexcept;
    nlohmann::json DescribeDocumentState() const;
    bool Edit(const nlohmann::json& command, std::string& message);
    bool PreviewEffect(DocumentSceneBase& scene, std::string_view id, float time, float strength, bool play,
                       std::string& message);
    void DrawDockSpace(bool reset);
    GameplayCameraWorkspaceResult Draw(DocumentSceneBase& scene, EditorWorkspaceHost& host,
                                       EditorOperationRegistry& operations, SceneObjectId selectedObject);
    void ApplyActivation(DocumentSceneBase& scene, const EditorWorkspaceActivation& activation,
                         EditorWorkspaceHost& host);
    void ClosePreviewRequest(EditorWorkspaceHost& host);
    void BindDocumentCommands(EditorCommandRouter& router);
    bool HasInteractiveEdit() const { return interactive_; }
    SceneObjectId CameraId() const { return camera_; }
    SceneObjectId TargetId() const { return target_; }
    SceneObjectId LookTargetId() const { return lookTarget_; }

private:
    bool SelectCamera(DocumentSceneBase& scene, SceneObjectId id);
    void DrawEffectLibrary();
    void DrawEffectInspector();
    void DrawEffectTimeline(DocumentSceneBase& scene);
    std::string effect_;
    bool effectsEditing_ = false, effectPlaying_ = false, effectLoop_ = true, effectBypass_ = false;
    float effectTime_ = 0, effectStrength_ = 1, previewSpeed_ = 0;
    void DrawLibrary(DocumentSceneBase& scene, EditorOperationRegistry& operations);
    // 摄像机方案の資産（保存・別名保存・破棄と一覧）。
    void DrawProfileAssets(DocumentSceneBase& scene, EditorOperationRegistry& operations);
    // 開いている方案の模式と镜头效果の一覧。
    void DrawModeList();
    // 模式の高级（模式名と呼び出し ID・響应・碰撞・構図・切入切出）。変えたら true。
    bool DrawModeAdvanced(CAMERA::GameplayCameraProfile& profile, CAMERA::GameplayCameraMode& mode);
    void DrawInspector(DocumentSceneBase& scene, EditorOperationRegistry& operations, SceneObjectId selectedObject);
    void DrawViews(DocumentSceneBase& scene, EditorWorkspaceHost& host, GameplayCameraWorkspaceResult& result);
    bool SelectObject(DocumentSceneBase& scene, const char* key, SceneObjectId& id, bool camerasOnly);
    // 機位と区域（HIKARI_GameplayCameraSetup.cpp）。
    void DrawCameraList(DocumentSceneBase& scene, EditorOperationRegistry& operations);
    void DrawZoneList(DocumentSceneBase& scene, EditorOperationRegistry& operations);
    // 模板の一覧（"CameraTemplates" の popup）。existingCamera が有効なら、その機位を游戏摄像机にする。
    void DrawTemplateMenu(DocumentSceneBase& scene, EditorOperationRegistry& operations, SceneObjectId existingCamera);
    // 模板から方案を作って保存し、玩家を追う機位として場面へ結ぶ（作業の手順を一つにまとめる）。
    void CreateCameraFromTemplate(DocumentSceneBase& scene, EditorOperationRegistry& operations,
                                  std::string_view preset, SceneObjectId existingCamera);
    void DrawZoneInspector(DocumentSceneBase& scene, EditorOperationRegistry& operations);
    // 機位の型と、その型で効く常用の項目。変えたら true。
    bool DrawMountSettings(DocumentSceneBase& scene, CAMERA::GameplayCameraMode& mode);
    void DrawZoneOverlay(DocumentSceneBase& scene, const EditorViewportRect& rect);
    // 追う物が入っている区域を探し、模拟走动なら相机画面をその区域の機位に切り替える。
    void UpdateZoneSimulation(DocumentSceneBase& scene);
    void SetZoneSimulation(DocumentSceneBase& scene, bool enabled);
    // 追う物・注視の相手を変えたら、今の方案のまま場面の機位へ結び直す。
    void RebindTargets(DocumentSceneBase& scene, EditorOperationRegistry& operations);
    GameplayCameraEditorDocument document_;
    DirectorViewPanel observer_;
    CameraFollowSystem evaluator_;
    CameraFollowComponent preview_;
    SceneObjectId camera_, target_, lookTarget_;
    SceneObjectId selectedZone_{}, activeZone_{}, simulatedTarget_{};
    bool simulateZones_ = false;
    std::optional<Transform3D> simulatedTargetOrigin_{};
    std::string mode_, message_, search_;
    Camera3D lastMonitorCamera_;
    uint64_t monitorRevision_ = 1;
    uint64_t previewRevision_ = 0, sceneRevision_ = 0;
    std::string previewBinding_;
    bool simulate_ = true, interactive_ = false, focused_ = false;
    float previewYaw_ = 0, previewPitch_ = 0, previewZoom_ = 0;
};
void RegisterGameplayCameraOperations(EditorOperationRegistry& registry, GameplayCameraWorkspaceController& controller,
                                      EditorWorkspaceHost& host, std::function<DocumentSceneBase*()> scene);
} // namespace HIKARI::EDITOR
