#include "Editor/Views/HIKARI_EditorViewInputRouter.h"

namespace HIKARI::EDITOR {

    const EditorViewInputState& EditorViewInputRouter::Submit(
        RENDER3D::RenderViewId viewId,
        const EditorViewInputSubmission& submission) {

        EditorViewInputState& state = states_[viewId.value];
        state.rect = submission.rect;
        state.visible = submission.visible && submission.rect.IsValid();
        state.focused = state.visible && submission.focused;
        state.hovered = state.visible && submission.hovered;
        state.pointerBlocked = submission.pointerBlocked;
        state.keyboardBlocked = submission.keyboardBlocked;

        if (!state.visible || state.pointerBlocked) {
            state.rightMouseCaptured = false;
            state.middleMouseCaptured = false;
            state.orbitMouseCaptured = false;
            if (!state.visible) {
                state.gizmoCaptured = false;
            }
            return state;
        }

        if (!submission.rightMouseDown) {
            state.rightMouseCaptured = false;
        } else if (state.hovered && !state.gizmoCaptured) {
            state.rightMouseCaptured = true;
        }

        if (!submission.middleMouseDown) {
            state.middleMouseCaptured = false;
        } else if (state.hovered && !state.gizmoCaptured) {
            state.middleMouseCaptured = true;
        }

        if (!submission.leftMouseDown || !submission.altDown) {
            state.orbitMouseCaptured = false;
        } else if (state.hovered && !state.gizmoCaptured) {
            state.orbitMouseCaptured = true;
        }

        if (state.gizmoCaptured) {
            state.rightMouseCaptured = false;
            state.middleMouseCaptured = false;
            state.orbitMouseCaptured = false;
        }
        return state;
    }

    void EditorViewInputRouter::SetGizmoCapture(
        RENDER3D::RenderViewId viewId,
        bool captured) {

        EditorViewInputState& state = states_[viewId.value];
        state.gizmoCaptured = captured;
        if (captured) {
            state.rightMouseCaptured = false;
            state.middleMouseCaptured = false;
            state.orbitMouseCaptured = false;
        }
    }

    void EditorViewInputRouter::Clear(RENDER3D::RenderViewId viewId) {
        states_.erase(viewId.value);
    }

    void EditorViewInputRouter::ClearAll() {
        states_.clear();
    }

    const EditorViewInputState* EditorViewInputRouter::Find(
        RENDER3D::RenderViewId viewId) const noexcept {

        const auto found = states_.find(viewId.value);
        return found != states_.end() ? &found->second : nullptr;
    }

} // namespace HIKARI::EDITOR
