#pragma once

#include <string>

#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI {

    class DocumentSceneBase;

    namespace EDITOR {

        enum class EditorPlayMode {
            None,
            Embedded,
            Windowed,
        };

        enum class EditorPlayState {
            Stopped,
            Starting,
            Running,
            Paused,
            StopRequested,
            Draining,
            RestoringEditor,
            Failed,
        };

        enum class PlayStopReason {
            Toolbar,
            WindowClose,
            Escape,
            ApplicationShutdown,
        };

        class EditorPlaySession {
        public:
            EditorPlaySession() = default;
            ~EditorPlaySession() = default;

            EditorPlaySession(const EditorPlaySession&) = delete;
            EditorPlaySession& operator=(const EditorPlaySession&) = delete;

            void RequestEmbeddedStart();
            void RequestWindowedStart();
            void RequestStop(
                PlayStopReason reason = PlayStopReason::Toolbar);
            void TogglePause();
            void CaptureEmbeddedInput(
                const INPUT::MouseCaptureRegion& region);
            void UpdateEmbeddedInputRegion(
                const INPUT::MouseCaptureRegion& region);
            void ReleaseEmbeddedInput();
            void Update(DocumentSceneBase& scene);
            void Shutdown(DocumentSceneBase* scene);

            EditorPlayMode GetMode() const noexcept { return mode_; }
            EditorPlayState GetState() const noexcept { return state_; }
            bool IsRunning() const noexcept;
            bool IsPaused() const noexcept;
            bool IsEmbeddedRunning() const noexcept;
            bool IsWindowedRunning() const noexcept;
            bool HasEmbeddedInput() const noexcept {
                return embeddedInputCaptured_;
            }
            bool IsTransitioning() const noexcept;
            const std::string& GetStatusMessage() const noexcept {
                return statusMessage_;
            }

        private:
            enum class PendingAction {
                None,
                StartEmbedded,
                StartWindowed,
            };

            void StartEmbedded(DocumentSceneBase& scene);
            void StartWindowed(DocumentSceneBase& scene);
            void StopEmbedded(DocumentSceneBase& scene);
            void AdvanceWindowedStop(DocumentSceneBase& scene);
            void BeginPlayTimeControl();
            void EndPlayTimeControl();
            void SetGameplayInputEnabled(bool enabled);
            void Fail(std::string message);

            PendingAction pendingAction_ = PendingAction::None;
            EditorPlayMode mode_ = EditorPlayMode::None;
            EditorPlayState state_ = EditorPlayState::Stopped;
            std::string statusMessage_{};
            bool embeddedInputCaptured_ = false;
            bool timePauseSnapshotValid_ = false;
            bool timeWasPaused_ = false;
        };

    } // namespace EDITOR
} // namespace HIKARI
