#pragma once
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Documents/HIKARI_GameplayCameraEditorDocument.h"
#include "Editor/Views/Director/HIKARI_DirectorViewPanel.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/HIKARI_CameraFollowSystem.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include <functional>

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
    nlohmann::json DescribeDocumentState() const;
    nlohmann::json DescribeFlows(const DocumentSceneBase& scene) const;
    bool EditingFlow() const { return flowEditing_; }
    bool HasFlowDraft() const { return flowDirty_; }
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
    void DrawFlowLibrary(DocumentSceneBase& scene);
    void DrawFlowInspector(DocumentSceneBase& scene, EditorOperationRegistry& operations, EditorWorkspaceHost& host);
    void DrawFlowTimeline(DocumentSceneBase& scene, EditorWorkspaceHost& host, EditorOperationRegistry& operations);
    void DrawFlowOverlay(DocumentSceneBase& scene, const EditorViewportRect& rect);
    void SelectFlow(DocumentSceneBase& scene, SceneObjectId id);
    void DrawLibrary(DocumentSceneBase& scene, EditorOperationRegistry& operations);
    void DrawInspector(DocumentSceneBase& scene, EditorOperationRegistry& operations, EditorWorkspaceHost& host,
                       SceneObjectId selectedObject);
    void DrawViews(DocumentSceneBase& scene, EditorWorkspaceHost& host, GameplayCameraWorkspaceResult& result);
    bool SelectObject(DocumentSceneBase& scene, const char* key, SceneObjectId& id, bool camerasOnly);
    std::shared_ptr<const SequenceAsset> flowSequence_;
    std::string flowSequenceKey_;
    SceneObjectId flowId_{};
    nlohmann::json flowDraft_ = nlohmann::json::object();
    uint64_t flowRevision_ = 0;
    bool flowEditing_ = false, flowDirty_ = false;
    GameplayCameraEditorDocument document_;
    DirectorViewPanel observer_;
    EditorTransformGizmoState flowGizmoState_;
    EditorTransformGizmo flowGizmo_;
    CameraFollowSystem evaluator_;
    CameraFollowComponent preview_;
    SceneObjectId camera_, target_, lookTarget_;
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
