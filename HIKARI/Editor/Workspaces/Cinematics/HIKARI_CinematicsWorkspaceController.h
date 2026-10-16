#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <string>

#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
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

struct ImDrawList;

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
    CinematicsWorkspaceResult Draw(DocumentSceneBase& scene, EditorContext& context, EditorWorkspaceHost& workspaceHost,
                                   EditorOperationRegistry& operations);
    bool RequestOpenSequenceAsset(DocumentSceneBase& scene, const AssetGuid& assetGuid, std::string& outMessage);
    bool ConfigureCameraEditing(DocumentSceneBase&, const nlohmann::json&, std::string&);
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
    bool DrawResources(DocumentSceneBase& scene, CinematicSequence& sequence, EditorContext& context,
                       EditorOperationRegistry& operations);
    // 触发区域：玩家が入るとこの演出を流し、終わったら游戏摄像机を選んだ模式へ（場面の流程）。
    void DrawTriggers(DocumentSceneBase& scene, EditorOperationRegistry& operations);
    bool DrawInspector(DocumentSceneBase& scene, CinematicSequence& sequence, EditorOperationRegistry& operations);

    bool DrawTimeline(DocumentSceneBase& scene, CinematicSequence& sequence);
    void DrawMonitor(DocumentSceneBase& scene, EditorWorkspaceHost& workspaceHost, CinematicsWorkspaceResult& result);
    bool DrawStage(DocumentSceneBase& scene, CinematicSequence& sequence, EditorContext& context,
                   EditorWorkspaceHost& host, bool enabled);
    bool DrawShotOverlays(DocumentSceneBase& scene, CinematicSequence& sequence, const EditorViewportRect& viewport,
                          bool enabled);
    Camera3D ResolveShotCamera(DocumentSceneBase& scene, CinematicSequence& sequence,
                               const SEQUENCER::CameraCutClip& shot, float localTime, bool includeMotion = true) const;
    bool DrawTransport(DocumentSceneBase& scene, CinematicSequence& sequence);
    bool DrawEffects(std::vector<SEQUENCER::SequenceScreenEffect>& effects, float seconds);
    bool DrawCurve(HIKARI::ScalarCurve& parameter, float time, const std::string& channel);
    using Channel = CurveEditChannel;
    std::array<ScalarCurve, 4> poseCurves_; // 姿勢鍵の編集用投影。永続化しない。
    ScalarCurve timingCurve_;               // 未編集時は等速の表示だけを作り、原本には書かない。
    void WriteChannelEdits(DocumentSceneBase& scene, CinematicSequence& sequence,
                           const std::array<ScalarCurve, 4>& beforePose, const ScalarCurve& beforeTiming);
    bool DrawChannelToolbar(DocumentSceneBase& scene, CinematicSequence& sequence);
    std::vector<Channel> CollectChannels(DocumentSceneBase& scene, CinematicSequence& sequence,
                                         bool visibleOnly = false);
    bool DrawChannelLanes(DocumentSceneBase& scene, CinematicSequence& sequence, float x, float y, float width,
                          float labelWidth, float rowHeight);
    bool DrawChannelCurve(DocumentSceneBase& scene, CinematicSequence& sequence);
    // 再生位置に、今の姿勢（瞄准と運動効果を除く）を姿勢键として置く。
    bool AddPoseKeyAtCursor(DocumentSceneBase& scene, CinematicSequence& sequence);
    // 再生位置の姿勢键を消す。
    bool RemovePoseKeyAtCursor(CinematicSequence& sequence);
    // 選んだ镜头の前後の姿勢键へ再生位置を移す（direction は -1 か 1）。
    bool SeekPoseKey(CinematicSequence& sequence, int direction);
    // 選んだ镜头を再生位置で二つに分ける（後半は同じ姿勢の続きから始まる）。
    bool SplitSelectedShot(CinematicSequence& sequence);
    bool DuplicateSelectedShot(CinematicSequence& sequence);
    // 镜头の局所時刻での再生位置（镜头の範囲に収める）。
    float ShotLocalTime(const SEQUENCER::CameraCutClip& shot) const;
    float ShotSourceTime(const SEQUENCER::CameraCutClip& shot) const;
    bool SharedPoseAtCursor(const SEQUENCER::CameraCutClip& shot) const;
    // 時間轴の時刻を近くの境目（镜头の端、再生位置）か 1/60 秒へ寄せる。Alt で自由。再生位置が引いている物に
    // ついて動く時（姿勢键を引く）は includeCursor を偽にする。
    float SnapTimelineTime(const CinematicSequence& sequence, float seconds, uint64_t ignoreShot,
                           bool includeCursor = true) const;
    ScalarCurveView curveView_;
    std::optional<CinematicSequence> curveGestureBefore_;
    bool curveMode_ = false;
    bool animatedChannelsOnly_ = true;
    uint64_t channelOwner_ = 0;
    SEQUENCER::CameraCutClip* SelectedShot(CinematicSequence& sequence);
    bool DeleteSelectedClip(CinematicSequence& sequence);
    void CreateShot(CinematicSequence& sequence, const Camera3D& camera);
    void CapturePose(DocumentSceneBase& scene, CinematicSequence& sequence, const Camera3D& camera);
    // 镜头の局所時刻 keyTime に、camera の位置と向き（と画角）を镜头の掛け先（anchor）の局所空間の姿勢にする。
    SEQUENCER::ShotPose CaptureShotPoseFor(DocumentSceneBase& scene, CinematicSequence& sequence,
                                           const SEQUENCER::CameraCutClip& shot, const Camera3D& camera,
                                           float keyTime) const;
    // 姿勢を镜头へ書く。自動键か键がある時は時刻の键、無ければ基準の姿勢。parts で変える要素を選ぶ。
    enum PoseParts : uint32_t { kPosePosition = 1u, kPoseRotation = 2u, kPoseFov = 4u, kPoseAll = 7u };
    void WriteShotPose(CinematicSequence& sequence, SEQUENCER::CameraCutClip& shot, const SEQUENCER::ShotPose& pose,
                       uint32_t parts);
    // 飛行録画（HIKARI_CinematicsStageWindow.cpp）：録画を押した後、空間视图で右ボタンを押して飛び始めると镜头の
    // 始めから再生し、一定間隔で視点の位置と向きを键にする（画角と傾きは変えない）。録った範囲の键は置き換える。
    bool UpdateFlightRecording(DocumentSceneBase& scene, CinematicSequence& sequence, bool startRequested);
    // 驾驶（HIKARI_CinematicsStageWindow.cpp）：空間视图の視点を選んだ镜头の再生位置の姿勢に重ね、视图で飛ぶと
    // その姿勢を镜头へ書く（傾きと画角は镜头のまま）。
    bool UpdatePilot(DocumentSceneBase& scene, CinematicSequence& sequence, bool viewMoved);
    // 軌道の点（HIKARI_CinematicsStageWindow.cpp）：選んだ镜头の軌道を空間视图に描き、点を押して選び引いて動かす。
    bool DrawRailOverlay(DocumentSceneBase& scene, CinematicSequence& sequence, SEQUENCER::CameraCutClip& shot,
                         const EditorViewportRect& viewport, bool& interacting);
    void DrawShotTransition(ImDrawList& draw, const CinematicSequence& sequence, const SEQUENCER::CameraCutClip& shot,
                            float timeOriginX, float y, float rowHeight) const;
    // 時間軸で镜头の上に置いた時刻の画を説明の小窓に出す（再生位置は動かさない）。shot が無ければ描画の依頼を取り下げる。
    void DrawShotHoverPreview(DocumentSceneBase& scene, CinematicSequence& sequence,
                              const SEQUENCER::CameraCutClip* shot, float localTime);

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
    std::optional<std::pair<float, float>> selectionPreviewRange_;
    float previewRate_ = 1.f;
    bool previewEnabled_ = false;
    // 镜头がある演出を開いたら画面に镜头の画を出す（停止して元へ戻すと、次に開くか再生するまで自由な視点）。
    bool autoPreview_ = true;
    bool ownsPreview_ = false;
    bool previewChanged_ = true;
    std::optional<bool> requestedEndpoint_;
    std::string previewDiagnostic_;
    bool spaceView_ = true;
    bool stageInitialized_ = false;
    // 空間视图の手柄：位置を動かす、向きを回す、注視点を動かす（相手に結んだ镜头は注視の偏移、無ければ向きを変える）。
    enum class StageTool { Move, Rotate, Aim };
    StageTool stageTool_ = StageTool::Move;
    bool showShotPaths_ = true;
    bool pilot_ = false;
    uint64_t pilotShot_ = 0;
    Camera3D pilotCamera_{};
    int selectedRailPoint_ = -1;
    EditorTransformGizmo stageGizmo_;
    // 既定で有効：動かした结果を再生位置の键として残す（姿勢を変えると今の時刻のキーが増える）。
    bool autoKey_ = true;
    enum class FlightState { Off, Armed, Recording };
    FlightState flight_ = FlightState::Off;
    uint64_t flightShot_ = 0;
    float flightLastKey_ = 0.0f;
    bool shotPreviewSubmitted_ = false;
    bool sequenceLibraryVisible_ = true;
    // 時間轴の 1 秒あたりの画素数。Ctrl + 滚轮で再生位置の下を中心に拡縮する。
    float zoom_ = 80.0f;
    bool fitTimelineRequested_ = true;
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
