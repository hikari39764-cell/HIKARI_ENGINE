#include "Editor/History/HIKARI_SceneObjectTransformHistorySession.h"

#include <utility>

namespace HIKARI::EDITOR {

    void SceneObjectTransformHistorySession::BeginFrame() noexcept {
        manipulatingThisFrame_ = false;
    }

    void SceneObjectTransformHistorySession::ObserveBeforeApply(
        const SceneDocument& document,
        SceneObjectId objectId,
        std::string_view label,
        bool manipulating,
        bool dirtyBefore) {

        if (!manipulating || objectId.value == 0u) {
            return;
        }
        manipulatingThisFrame_ = true;
        if (pending_) {
            return;
        }
        pending_ = PendingEdit{
            std::string(label),
            objectId,
            document.objects,
            document.camera,
            dirtyBefore,
            false
        };
    }

    void SceneObjectTransformHistorySession::MarkChanged() noexcept {
        if (pending_) {
            pending_->changed = true;
        }
    }

    void SceneObjectTransformHistorySession::EndFrame(
        const SceneDocument& document) {

        if (!pending_ || manipulatingThisFrame_) {
            return;
        }
        if (pending_->changed) {
            completed_ = SceneObjectAuthoringHistoryRequest{
                std::move(pending_->label),
                std::move(pending_->beforeObjects),
                document.objects,
                std::move(pending_->beforeCamera),
                document.camera,
                pending_->dirtyBefore
            };
        }
        pending_.reset();
    }

    std::optional<SceneObjectAuthoringHistoryRequest>
        SceneObjectTransformHistorySession::ConsumeHistoryRequest() {

        std::optional<SceneObjectAuthoringHistoryRequest> result =
            std::move(completed_);
        completed_.reset();
        return result;
    }

} // namespace HIKARI::EDITOR
