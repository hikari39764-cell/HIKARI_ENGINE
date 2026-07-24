#include "HIKARI_EditorWorkspaceHost.h"

#include <utility>

namespace HIKARI::EDITOR {

    EditorWorkspaceHost::EditorWorkspaceHost() {
        cinematicsGameView_.renderViewId = RENDER3D::kPrimaryRenderViewId;
        cinematicsGameView_.role = EditorViewRole::Game;
        cinematicsGameView_.purpose = RENDER3D::RenderViewPurpose::Game;
        cinematicsGameView_.cameraBinding.kind = EditorViewCameraSourceKind::SceneDirector;
        cinematicsGameView_.execution = EditorViewExecutionMode::PrimaryFullQuality;

        cinematicsDirectorView_.renderViewId = kEditorDirectorRenderViewId;
        cinematicsDirectorView_.role = EditorViewRole::Director;
        cinematicsDirectorView_.purpose = RENDER3D::RenderViewPurpose::EditorScene;
        cinematicsDirectorView_.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
        cinematicsDirectorView_.execution = EditorViewExecutionMode::SecondaryEditorScene;
        cinematicsDirectorView_.visualization.shadingMode =
            EditorViewShadingMode::Neutral;
        cinematicsDirectorView_.visualization.displayExposure = 1.25f;

        cinematicsOverviewView_.renderViewId = kEditorOverviewRenderViewId;
        cinematicsOverviewView_.role = EditorViewRole::Overview;
        cinematicsOverviewView_.purpose = RENDER3D::RenderViewPurpose::EditorScene;
        cinematicsOverviewView_.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
        cinematicsOverviewView_.execution = EditorViewExecutionMode::LightweightEditorOverview;

        modelCollisionPreviewView_.renderViewId =
            kModelCollisionPreviewRenderViewId;
        modelCollisionPreviewView_.role = EditorViewRole::Director;
        modelCollisionPreviewView_.purpose =
            RENDER3D::RenderViewPurpose::EditorScene;
        modelCollisionPreviewView_.cameraBinding.kind =
            EditorViewCameraSourceKind::OwnedEditorCamera;
        modelCollisionPreviewView_.execution =
            EditorViewExecutionMode::SecondaryEditorScene;
        modelCollisionPreviewView_.visualization.shadingMode =
            EditorViewShadingMode::Neutral;
        modelCollisionPreviewView_.visualization.displayExposure = 1.0f;
    }

    bool EditorWorkspaceHost::RequestOpen(EditorWorkspaceOpenRequest request) {
        if (request.targetCameraObjectId &&
            request.targetCameraObjectId->value == 0) {
            request.targetCameraObjectId.reset();
        }
        pendingOpenRequest_ = std::move(request);
        return true;
    }

    std::optional<EditorWorkspaceActivation> EditorWorkspaceHost::ApplyPending() {
        if (!pendingOpenRequest_) {
            return std::nullopt;
        }

        EditorWorkspaceOpenRequest request = std::move(*pendingOpenRequest_);
        pendingOpenRequest_.reset();

        EditorWorkspaceActivation activation{};
        activation.previous = activeWorkspace_;
        activation.current = request.workspaceId;
        activation.targetCameraObjectId = request.targetCameraObjectId;
        activation.sequenceAssetGuid = request.sequenceAssetGuid;
        activation.modelAssetGuid = request.modelAssetGuid;
        activation.animationStateMachineAssetGuid =
            request.animationStateMachineAssetGuid;
        activeWorkspace_ = request.workspaceId;

        return activation;
    }

    EditorWorkspaceId EditorWorkspaceHost::GetActive() const noexcept {
        return activeWorkspace_;
    }

    bool EditorWorkspaceHost::IsActive(EditorWorkspaceId workspaceId) const noexcept {
        return activeWorkspace_ == workspaceId;
    }

    void EditorWorkspaceHost::RequestResetActiveLayout() noexcept {
        if (activeWorkspace_ == EditorWorkspaceId::ModelCollision) {
            resetModelCollisionLayoutRequested_ = true;
        } else if (activeWorkspace_ ==
                EditorWorkspaceId::AnimationStateMachine) {
            resetAnimationStateMachineLayoutRequested_ = true;
        } else if (activeWorkspace_ == EditorWorkspaceId::Cinematics) {
            resetCinematicsLayoutRequested_ = true;
        } else {
            resetSceneLayoutRequested_ = true;
        }
    }

    bool EditorWorkspaceHost::ConsumeReset(EditorWorkspaceId workspaceId) noexcept {
        bool* requested = &resetSceneLayoutRequested_;
        if (workspaceId == EditorWorkspaceId::Cinematics) {
            requested = &resetCinematicsLayoutRequested_;
        } else if (workspaceId == EditorWorkspaceId::ModelCollision) {
            requested = &resetModelCollisionLayoutRequested_;
        } else if (workspaceId ==
                EditorWorkspaceId::AnimationStateMachine) {
            requested = &resetAnimationStateMachineLayoutRequested_;
        }
        const bool result = *requested;
        *requested = false;
        return result;
    }

    EditorViewInstance& EditorWorkspaceHost::GetCinematicsGameView() noexcept {
        return cinematicsGameView_;
    }

    const EditorViewInstance& EditorWorkspaceHost::GetCinematicsGameView() const noexcept {
        return cinematicsGameView_;
    }

    EditorViewInstance& EditorWorkspaceHost::GetCinematicsDirectorView() noexcept {
        return cinematicsDirectorView_;
    }

    const EditorViewInstance& EditorWorkspaceHost::GetCinematicsDirectorView() const noexcept {
        return cinematicsDirectorView_;
    }

    EditorViewInstance& EditorWorkspaceHost::GetCinematicsOverviewView() noexcept {
        return cinematicsOverviewView_;
    }

    const EditorViewInstance& EditorWorkspaceHost::GetCinematicsOverviewView() const noexcept {
        return cinematicsOverviewView_;
    }

    EditorViewInstance& EditorWorkspaceHost::GetModelCollisionPreviewView() noexcept {
        return modelCollisionPreviewView_;
    }

    const EditorViewInstance& EditorWorkspaceHost::GetModelCollisionPreviewView() const noexcept {
        return modelCollisionPreviewView_;
    }

} // namespace HIKARI::EDITOR
