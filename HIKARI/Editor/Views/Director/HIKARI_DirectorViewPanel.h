#pragma once

#include <cstdint>

#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Views/Director/HIKARI_EditorDirectorCameraController.h"
#include "Editor/Views/HIKARI_EditorViewInputRouter.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"

namespace HIKARI {

class DocumentSceneBase;

namespace EDITOR {

enum class DirectorViewMode : uint8_t {
    Free,
    LookThrough,
    Pilot,
};

struct DirectorViewGizmoEdit {
    bool changed = false;
    bool interacting = false;
    SceneObjectId objectId{};
    TransformData transform{};
    MATH::Quat rotation = MATH::Quat::Identity();
};

struct DirectorViewPanelResult {
    EditorViewportRect viewport;
    bool selectionRequested = false;
    SceneObjectId selectedObjectId{};

    bool alignCameraRequested = false;
    SceneObjectId alignCameraObjectId{};
    DirectorCameraPose alignPose{};

    bool pilotPoseChanged = false;
    SceneObjectId pilotCameraObjectId{};
    DirectorCameraPose pilotPose{};

    DirectorViewGizmoEdit gizmo{};
    // このフレームに视图の操作（飛行・周回・平移・聚焦・视点の预设）で視点が動いた。
    bool viewMoved = false;
    DirectorViewMode mode = DirectorViewMode::Free;
    SceneObjectId targetCameraObjectId{};
};

class DirectorViewPanel {
public:
    DirectorViewPanel();

    DirectorViewPanelResult DrawContents(const DocumentSceneBase& scene, EditorViewInstance& view,
                                         SceneObjectId selectedObjectId, const EditorTransformGizmoState& gizmoState,
                                         float deltaTime, bool runtimePlayActive, bool sceneCameraControls = true);

    void SetTargetCamera(SceneObjectId cameraObjectId) noexcept;
    // cameraCut が偽なら前のフレームの画の履歴を保つ（再生や驾驶で視点を毎フレーム合わせる時）。
    void SetFreeCamera(const Camera3D& camera, bool cameraCut = true);
    void FocusPoint(const MATH::Vec3& point, float distance);
    bool FrameTargetCamera(const DocumentSceneBase& scene, SceneObjectId cameraObjectId,
                           float preferredDistance = 10.0f);
    void ExitPilot() noexcept;
    void ResetForScene();
    void LeaveWorkspace();

    DirectorViewMode GetMode() const noexcept;
    const Camera3D& GetViewCamera() const noexcept;

private:
    EditorDirectorCameraController& ActiveController() noexcept;
    const EditorDirectorCameraController& ActiveController() const noexcept;
    void SetMode(DirectorViewMode mode) noexcept;

    DirectorViewMode mode_ = DirectorViewMode::Free;
    SceneObjectId targetCameraObjectId_{};
    EditorDirectorCameraController freeCamera_{};
    EditorDirectorCameraController pilotCamera_{};
    Camera3D resolvedLookCamera_{};
    Camera3D currentViewCamera_{};
    EditorViewInputRouter inputRouter_{};
    EditorTransformGizmo transformGizmo_{};
    uint64_t cameraRevision_ = 1;
    float lastAspect_ = 0.0f;
    bool cameraCutPending_ = true;
};

} // namespace EDITOR

} // namespace HIKARI
