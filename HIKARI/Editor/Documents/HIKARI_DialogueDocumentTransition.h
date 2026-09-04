#pragma once

#include <cstdint>

namespace HIKARI::EDITOR {

    enum class DialogueDocumentTransitionIntent : uint8_t {
        None = 0u,
        NewAsset,
        OpenAsset,
        CloseAsset,
        RevertAsset,
        LeaveWorkspace,
    };

    enum class DialogueDocumentTransitionState : uint8_t {
        Idle = 0u,
        AwaitingDecision,
        Saving,
        ReadyToApply,
        Applying,
    };

    enum class DialogueDocumentTransitionRequest : uint8_t {
        ApplyImmediately = 0u,
        ConfirmationRequired,
        Busy,
        InvalidIntent,
    };

    class DialogueDocumentTransition final {
    public:
        DialogueDocumentTransitionRequest Request(
            DialogueDocumentTransitionIntent intent,
            bool dirty) noexcept {
            if (intent == DialogueDocumentTransitionIntent::None) {
                return DialogueDocumentTransitionRequest::InvalidIntent;
            }
            if (state_ != DialogueDocumentTransitionState::Idle) {
                return DialogueDocumentTransitionRequest::Busy;
            }
            if (!dirty) {
                return DialogueDocumentTransitionRequest::ApplyImmediately;
            }
            intent_ = intent;
            state_ = DialogueDocumentTransitionState::AwaitingDecision;
            confirmationOpenRequested_ = true;
            return DialogueDocumentTransitionRequest::ConfirmationRequired;
        }

        bool BeginSave() noexcept {
            if (state_ !=
                DialogueDocumentTransitionState::AwaitingDecision) {
                return false;
            }
            state_ = DialogueDocumentTransitionState::Saving;
            return true;
        }

        bool CompleteSave(bool succeeded) noexcept {
            if (state_ != DialogueDocumentTransitionState::Saving) {
                return false;
            }
            state_ = succeeded
                ? DialogueDocumentTransitionState::ReadyToApply
                : DialogueDocumentTransitionState::AwaitingDecision;
            return true;
        }

        bool ChooseDiscard() noexcept {
            if (state_ !=
                DialogueDocumentTransitionState::AwaitingDecision) {
                return false;
            }
            state_ = DialogueDocumentTransitionState::ReadyToApply;
            return true;
        }

        bool BeginApply() noexcept {
            if (state_ != DialogueDocumentTransitionState::ReadyToApply) {
                return false;
            }
            state_ = DialogueDocumentTransitionState::Applying;
            return true;
        }

        bool CompleteApply(bool succeeded) noexcept {
            if (state_ != DialogueDocumentTransitionState::Applying) {
                return false;
            }
            if (succeeded) {
                Reset();
            }
            else {
                state_ = DialogueDocumentTransitionState::ReadyToApply;
            }
            return true;
        }

        void Cancel() noexcept { Reset(); }

        bool ConsumeConfirmationOpenRequest() noexcept {
            const bool requested = confirmationOpenRequested_;
            confirmationOpenRequested_ = false;
            return requested;
        }

        bool HasPendingIntent() const noexcept {
            return state_ != DialogueDocumentTransitionState::Idle;
        }

        DialogueDocumentTransitionIntent GetIntent() const noexcept {
            return intent_;
        }

        DialogueDocumentTransitionState GetState() const noexcept {
            return state_;
        }

    private:
        void Reset() noexcept {
            intent_ = DialogueDocumentTransitionIntent::None;
            state_ = DialogueDocumentTransitionState::Idle;
            confirmationOpenRequested_ = false;
        }

        DialogueDocumentTransitionIntent intent_ =
            DialogueDocumentTransitionIntent::None;
        DialogueDocumentTransitionState state_ =
            DialogueDocumentTransitionState::Idle;
        bool confirmationOpenRequested_ = false;
    };

} // namespace HIKARI::EDITOR
