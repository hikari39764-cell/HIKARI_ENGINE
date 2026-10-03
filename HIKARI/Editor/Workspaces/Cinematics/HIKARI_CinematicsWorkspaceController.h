#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Documents/HIKARI_SequenceEditorDocumentController.h"
#include "Editor/Views/Director/HIKARI_DirectorViewPanel.h"
#include "Editor/Widgets/HIKARI_ScalarCurveEditor.h"
#include "Scene/Sequencer/HIKARI_SequencePlayback.h"
#include <json.hpp>
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/Sequence/HIKARI_SequenceEditorDocumentToolbar.h"
#include "Editor/Workspaces/Sequence/HIKARI_SequenceLibraryPanel.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI {

class DocumentSceneBase;
struct EditorContext;

namespace EDITOR {

struct CinematicsWorkspaceResult {
    INPUT::MouseCaptureRegion playViewport{};
    bool playViewportClicked = false;
    bool cinematicsChanged = false;
    bool saveSceneRequested = false;
    uint64_t timelineEditMergeId = 0;
    std::string statusMessage{};
};

class CinematicsWorkspaceController {
public:
    void DrawDockSpace(bool resetDefaultDockLayout) const;
    void SyncSceneIdentity(DocumentSceneBase& scene, EditorWorkspaceHost& workspaceHost);
    void PrepareForRuntimePlay(DocumentSceneBase& scene);
    void OnCinematicsDocumentRestored(DocumentSceneBase& scene, EditorWorkspaceHost& workspaceHost);
    void ApplyWorkspaceActivation(DocumentSceneBase& scene, const EditorWorkspaceActivation& activation,
                                  EditorContext& context, EditorWorkspaceHost& workspaceHost);
    CinematicsWorkspaceResult Draw(DocumentSceneBase& scene, EditorContext& context,
                                   EditorWorkspaceHost& workspaceHost);
    bool RequestOpenSequenceAsset(DocumentSceneBase& scene, const AssetGuid& assetGuid, std::string& outMessage);
    bool IsEditingSequenceAsset() const noexcept;
    bool IsSequenceDocumentDirty() const noexcept;
    // 文書の状態。シェル、AI の文脈、バックグラウンドのセッションで共有する。
    // 表示中の文書があるか。DescribeDocumentState の open と同じ判定。
    bool IsDocumentOpen() const noexcept;
    nlohmann::json DescribeDocumentState() const;
    bool CanUndoSequenceDocument() const noexcept;
    bool CanRedoSequenceDocument() const noexcept;
    bool SaveSequenceDocument(DocumentSceneBase& scene, std::string& outMessage);
    bool UndoSequenceDocument(std::string& outMessage);
    bool RedoSequenceDocument(std::string& outMessage);
    bool RequestNewSequenceAsset(DocumentSceneBase& scene, std::string& outMessage);
    bool RequestRevertSequenceAsset(DocumentSceneBase& scene, std::string& outMessage);
    bool RequestCloseSequenceAsset(DocumentSceneBase& scene, std::string& outMessage);
    bool HasPendingSequenceConfirmation() const noexcept;
    const SequenceEditorDocument& GetSequenceDocument() const noexcept;
    void BindDocumentCommands(EditorCommandRouter& commandRouter, DocumentSceneBase& scene);

    bool ExecuteAuthoring(DocumentSceneBase& scene, const nlohmann::json& command, std::string& message);
    static nlohmann::json AuthoringCommandSchema();
    nlohmann::json DescribeAuthoring(DocumentSceneBase& scene) const;

private:
    CinematicSequence& ActiveSequence(DocumentSceneBase& scene);
    void ResetEditing(DocumentSceneBase& scene);
    bool DrawResources(DocumentSceneBase& scene, CinematicSequence& sequence, EditorContext& context);
    bool DrawInspector(DocumentSceneBase& scene, CinematicSequence& sequence);
    bool DrawTimeline(DocumentSceneBase& scene, CinematicSequence& sequence);
    void DrawMonitor(DocumentSceneBase& scene, EditorWorkspaceHost& workspaceHost, CinematicsWorkspaceResult& result);
    bool DrawStage(DocumentSceneBase& scene, CinematicSequence& sequence, EditorContext& context,
                   EditorWorkspaceHost& host, bool enabled);
    bool DrawShotOverlays(DocumentSceneBase& scene, CinematicSequence& sequence, const EditorViewportRect& viewport,
                          bool enabled);
    Camera3D ResolveShotCamera(DocumentSceneBase& scene, CinematicSequence& sequence,
                               const SEQUENCER::CameraCutClip& shot, float localTime, bool includeMotion = true) const;
    bool DrawTransport(CinematicSequence& sequence);
    bool DrawEffects(std::vector<SEQUENCER::SequenceScreenEffect>& effects, float seconds);
    bool DrawCurve(HIKARI::ScalarCurve& parameter, float time, const std::string& channel);
    struct Channel {
        std::string id, label;
        HIKARI::ScalarCurve* parameter = nullptr;
        float start = 0, duration = 1;
    };
    std::vector<Channel> CollectChannels(CinematicSequence& sequence, bool visibleOnly = false);
    bool DrawChannelLanes(CinematicSequence& sequence, float x, float y, float width, float labelWidth,
                          float rowHeight);
    bool DrawChannelCurve(CinematicSequence& sequence);
    std::string selectedChannel_ = "eyeX";
    ScalarCurveView curveView_;
    bool curveMode_ = false;
    bool animatedChannelsOnly_ = true;
    uint64_t channelOwner_ = 0;
    std::vector<HIKARI::ScalarCurveKey> keyClipboard_;
    SEQUENCER::CameraCutClip* SelectedShot(CinematicSequence& sequence);
    bool DeleteSelectedClip(CinematicSequence& sequence);
    void CreateShot(CinematicSequence& sequence, const Camera3D& camera);
    void CapturePose(DocumentSceneBase& scene, CinematicSequence& sequence, const Camera3D& camera);

    SequenceEditorDocumentController sequenceDocumentController_{};
    SequenceEditorDocumentToolbar sequenceDocumentToolbar_{};
    SequenceLibraryPanel sequenceLibraryPanel_{};
    SEQUENCER::SequenceBindingContext previewBindings_{};
    DirectorViewPanel directorViewPanel_{};
    SEQUENCER::SequencePlaybackCursor cursor_{};
    CinematicSequenceId activeSequenceId_{1};
    enum class SelectionKind { Shot, Cue, Effect };
    SelectionKind selectionKind_ = SelectionKind::Shot;
    uint64_t selectedId_ = 0;
    bool loop_ = false;
    bool previewEnabled_ = false;
    bool ownsPreview_ = false;
    bool previewChanged_ = true;
    bool spaceView_ = true;
    bool stageInitialized_ = false;
    bool stageEditTarget_ = false;
    bool stageRotate_ = false;
    bool showShotPaths_ = true;
    EditorTransformGizmo stageGizmo_;
    bool autoKey_ = false;
    bool sequenceLibraryVisible_ = true;
    float zoom_ = 80.0f;
    float timelineScroll_ = 0.0f;
    std::filesystem::path audioProjectRoot_;
    std::vector<std::filesystem::path> audioAssets_;
    float dragMouseX_ = 0.0f;
    float dragStart_ = 0.0f;
    float dragDuration_ = 0.0f;
    uint64_t draggingId_ = 0;
    SelectionKind draggingKind_ = SelectionKind::Shot;
    int dragPart_ = 0;
    uint64_t mergeId_ = 1;
    std::string sceneIdentity_{};
    std::string pendingStatusMessage_{};
    uint64_t sceneDocumentRevision_ = 0;
};

} // namespace EDITOR
} // namespace HIKARI
