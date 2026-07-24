#pragma once

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_CinematicCameraPlayback.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"

namespace HIKARI {

    struct DocumentSceneCameraRuntimeState {
        Camera3D editorCamera{};
        Camera3D gameplayCamera{};
        CameraDirector director{};
        CAMERA::CameraRigService rigService{};
        CinematicPlaybackHandle currentCameraSequenceHandle{};
        RENDER3D::ResolvedCameraFrame resolvedFrame{};
        DebugCameraController3D debugCamera{};
        DebugCameraController3D runtimePreviewCamera{};
        Camera3D editorCameraSnapshot{};
        Camera3D editorCameraPreviewSnapshot{};
        DebugCameraController3D editorDebugCameraSnapshot{};
        SceneObjectId editorCameraPreviewObjectId{};
        CameraOverrideToken editorCameraPreviewToken{};
        SEQUENCER::SequencePlaybackService sequencePlayback{};
        bool editorCameraCutPending = false;
        bool runtimePreviewCameraActive = false;
        bool runtimeSceneCameraActive = false;
    };

} // namespace HIKARI
