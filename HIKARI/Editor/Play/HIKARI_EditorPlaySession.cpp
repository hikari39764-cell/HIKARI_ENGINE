#include "Editor/Play/HIKARI_EditorPlaySession.h"

#include <utility>

#include "Core/HIKARI_TimeService.h"
#include "HIKARI_Services.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

    void EditorPlaySession::RequestEmbeddedStart() {
        if (IsRunning() || state_ == EditorPlayState::Starting) {
            return;
        }
        pendingAction_ = PendingAction::StartEmbedded;
        statusMessage_ = "Embedded Play requested.";
    }

    void EditorPlaySession::RequestWindowedStart() {
        if (IsRunning() || state_ == EditorPlayState::Starting) {
            return;
        }
        pendingAction_ = PendingAction::StartWindowed;
        statusMessage_ = "Play in New Window requested.";
    }

    void EditorPlaySession::RequestStop(PlayStopReason reason) {
        (void)reason;
        if (mode_ == EditorPlayMode::None &&
            state_ != EditorPlayState::Starting) {
            return;
        }
        if (state_ == EditorPlayState::StopRequested ||
            state_ == EditorPlayState::Draining ||
            state_ == EditorPlayState::RestoringEditor) {
            return;
        }
        state_ = EditorPlayState::StopRequested;
        statusMessage_ = "Stopping Play...";
    }

    void EditorPlaySession::TogglePause() {
        if (!IsEmbeddedRunning()) {
            return;
        }
        if (state_ == EditorPlayState::Paused) {
            TIME::SetPaused(false);
            TIME::ResetFrameClock();
            state_ = EditorPlayState::Running;
            statusMessage_ = "Embedded Play resumed. Click the Game View to control.";
            return;
        }
        ReleaseEmbeddedInput();
        TIME::SetPaused(true);
        state_ = EditorPlayState::Paused;
        statusMessage_ = "Embedded Play paused.";
    }

    void EditorPlaySession::CaptureEmbeddedInput(
        const INPUT::MouseCaptureRegion& region) {
        if (!IsEmbeddedRunning() || IsPaused() || !region.IsValid()) {
            return;
        }
        INPUT::InputService& input = SERVICES::GetInputService();
        input.SetMouseCaptureRegion(region);
        input.SetMouseCaptureMode(INPUT::MouseCaptureMode::Relative);
        SetGameplayInputEnabled(true);
        embeddedInputCaptured_ = true;
        statusMessage_ = "Embedded Play is receiving input. Press Esc to release.";
    }

    void EditorPlaySession::UpdateEmbeddedInputRegion(
        const INPUT::MouseCaptureRegion& region) {
        if (!embeddedInputCaptured_ || !IsEmbeddedRunning() ||
            IsPaused() || !region.IsValid()) {
            return;
        }
        SERVICES::GetInputService().SetMouseCaptureRegion(region);
    }

    void EditorPlaySession::ReleaseEmbeddedInput() {
        if (mode_ != EditorPlayMode::Embedded &&
            !embeddedInputCaptured_) {
            return;
        }
        INPUT::InputService& input = SERVICES::GetInputService();
        input.SetMouseCaptureMode(INPUT::MouseCaptureMode::Free);
        input.ClearMouseCaptureRegion();
        SetGameplayInputEnabled(false);
        embeddedInputCaptured_ = false;
        if (state_ == EditorPlayState::Running) {
            statusMessage_ = "Embedded Play is running. Click the Game View to control.";
        }
    }

    void EditorPlaySession::Update(DocumentSceneBase& scene) {
        if (IsWindowedRunning() &&
            SERVICES::ConsumeInProcessGameCloseRequest()) {
            RequestStop(PlayStopReason::WindowClose);
        }

        if (state_ == EditorPlayState::StopRequested ||
            state_ == EditorPlayState::Draining ||
            state_ == EditorPlayState::RestoringEditor) {
            if (mode_ == EditorPlayMode::Embedded) {
                StopEmbedded(scene);
            } else if (mode_ == EditorPlayMode::Windowed) {
                AdvanceWindowedStop(scene);
            }
            return;
        }

        const PendingAction action = pendingAction_;
        pendingAction_ = PendingAction::None;
        switch (action) {
        case PendingAction::StartEmbedded:
            StartEmbedded(scene);
            break;
        case PendingAction::StartWindowed:
            StartWindowed(scene);
            break;
        case PendingAction::None:
        default:
            break;
        }
    }

    void EditorPlaySession::Shutdown(DocumentSceneBase* scene) {
        pendingAction_ = PendingAction::None;
        if (mode_ == EditorPlayMode::Embedded) {
            ReleaseEmbeddedInput();
            if (scene != nullptr) {
                (void)scene->EndRuntimePlay();
            }
        } else if (mode_ == EditorPlayMode::Windowed) {
            (void)SERVICES::EndInProcessGamePresentation();
            if (scene != nullptr) {
                (void)scene->EndRuntimePlay();
            }
        }
        EndPlayTimeControl();
        mode_ = EditorPlayMode::None;
        state_ = EditorPlayState::Stopped;
        embeddedInputCaptured_ = false;
    }

    bool EditorPlaySession::IsRunning() const noexcept {
        return state_ == EditorPlayState::Running ||
            state_ == EditorPlayState::Paused;
    }

    bool EditorPlaySession::IsPaused() const noexcept {
        return state_ == EditorPlayState::Paused;
    }

    bool EditorPlaySession::IsEmbeddedRunning() const noexcept {
        return mode_ == EditorPlayMode::Embedded && IsRunning();
    }

    bool EditorPlaySession::IsWindowedRunning() const noexcept {
        return mode_ == EditorPlayMode::Windowed && IsRunning();
    }

    bool EditorPlaySession::IsTransitioning() const noexcept {
        return state_ == EditorPlayState::Starting ||
            state_ == EditorPlayState::StopRequested ||
            state_ == EditorPlayState::Draining ||
            state_ == EditorPlayState::RestoringEditor;
    }

    void EditorPlaySession::StartEmbedded(DocumentSceneBase& scene) {
        state_ = EditorPlayState::Starting;
        mode_ = EditorPlayMode::Embedded;
        BeginPlayTimeControl();
        if (!scene.BeginRuntimePlay()) {
            Fail("Could not create the runtime scene for Embedded Play.");
            return;
        }

        SetGameplayInputEnabled(false);
        embeddedInputCaptured_ = false;
        state_ = EditorPlayState::Running;
        statusMessage_ = "Embedded Play is running. Click the Game View to control.";
    }

    void EditorPlaySession::StartWindowed(DocumentSceneBase& scene) {
        state_ = EditorPlayState::Starting;
        mode_ = EditorPlayMode::Windowed;
        BeginPlayTimeControl();
        if (!scene.BeginRuntimePlay()) {
            Fail("Could not create the runtime scene for Play.");
            return;
        }
        SERVICES::GetInputService().ClearMouseCaptureRegion();
        if (!SERVICES::BeginInProcessGamePresentation()) {
            (void)scene.EndRuntimePlay();
            Fail("Could not transfer presentation to the game window.");
            return;
        }

        state_ = EditorPlayState::Running;
        statusMessage_ = "Play in New Window is running.";
    }

    void EditorPlaySession::StopEmbedded(DocumentSceneBase& scene) {
        ReleaseEmbeddedInput();
        const bool sceneRestored = scene.EndRuntimePlay();
        EndPlayTimeControl();
        mode_ = EditorPlayMode::None;
        state_ = sceneRestored
            ? EditorPlayState::Stopped
            : EditorPlayState::Failed;
        statusMessage_ = sceneRestored
            ? "Embedded Play stopped."
            : "Embedded Play stopped, but the editor scene could not be restored.";
    }

    void EditorPlaySession::AdvanceWindowedStop(DocumentSceneBase& scene) {
        if (state_ == EditorPlayState::StopRequested) {
            if (!SERVICES::BeginInProcessGamePresentationStop()) {
                state_ = EditorPlayState::Running;
                statusMessage_ = "Could not begin the Play shutdown sequence.";
                return;
            }
            state_ = EditorPlayState::Draining;
            statusMessage_ = "Stopping Play: draining frame generation...";
            return;
        }

        if (state_ == EditorPlayState::Draining) {
            if (!SERVICES::DrainInProcessGamePresentation()) {
                state_ = EditorPlayState::Running;
                statusMessage_ = "Could not drain the game presentation surface.";
                return;
            }
            state_ = EditorPlayState::RestoringEditor;
            statusMessage_ = "Stopping Play: restoring editor presentation...";
            return;
        }

        if (state_ != EditorPlayState::RestoringEditor) {
            return;
        }

        const bool presentationRestored =
            SERVICES::RestoreEditorPresentation();
        if (SERVICES::IsInProcessGamePresentationActive()) {
            statusMessage_ = "Editor presentation restore is still pending.";
            return;
        }

        const bool sceneRestored = scene.EndRuntimePlay();
        EndPlayTimeControl();
        mode_ = EditorPlayMode::None;
        if (!presentationRestored || !sceneRestored) {
            state_ = EditorPlayState::Failed;
            statusMessage_ = !sceneRestored
                ? "Play stopped, but the editor scene could not be restored."
                : "Play stopped, but DLSS-G did not unload cleanly.";
            return;
        }
        state_ = EditorPlayState::Stopped;
        statusMessage_ = "Play stopped.";
    }

    void EditorPlaySession::BeginPlayTimeControl() {
        if (!timePauseSnapshotValid_) {
            timeWasPaused_ = TIME::IsPaused();
            timePauseSnapshotValid_ = true;
        }
        TIME::SetPaused(false);
        TIME::ResetFrameClock();
    }

    void EditorPlaySession::EndPlayTimeControl() {
        if (!timePauseSnapshotValid_) {
            return;
        }
        TIME::SetPaused(timeWasPaused_);
        TIME::ResetFrameClock();
        timePauseSnapshotValid_ = false;
    }

    void EditorPlaySession::SetGameplayInputEnabled(bool enabled) {
        SERVICES::GetInputService().Contexts().SetActive(
            "Gameplay",
            enabled);
    }

    void EditorPlaySession::Fail(std::string message) {
        ReleaseEmbeddedInput();
        EndPlayTimeControl();
        mode_ = EditorPlayMode::None;
        state_ = EditorPlayState::Failed;
        statusMessage_ = std::move(message);
    }

} // namespace HIKARI::EDITOR
