#pragma once

#include <unordered_map>

#include "Render3D/Core/HIKARI_RenderView.h"

namespace HIKARI::EDITOR {

    struct EditorViewInputRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        bool IsValid() const noexcept {
            return width > 1.0f && height > 1.0f;
        }
    };

    struct EditorViewInputSubmission {
        EditorViewInputRect rect{};
        bool visible = false;
        bool focused = false;
        bool hovered = false;
        bool rightMouseDown = false;
        bool rightMouseClicked = false;
        bool middleMouseDown = false;
        bool middleMouseClicked = false;
        bool leftMouseDown = false;
        bool leftMouseClicked = false;
        bool altDown = false;
        bool pointerBlocked = false;
        bool keyboardBlocked = false;
    };

    struct EditorViewInputState {
        EditorViewInputRect rect{};
        bool visible = false;
        bool focused = false;
        bool hovered = false;
        bool rightMouseCaptured = false;
        bool middleMouseCaptured = false;
        bool orbitMouseCaptured = false;
        bool gizmoCaptured = false;
        bool pointerBlocked = false;
        bool keyboardBlocked = false;

        bool IsMouseCaptured() const noexcept {
            return rightMouseCaptured || middleMouseCaptured ||
                orbitMouseCaptured;
        }

        bool AcceptsKeyboard() const noexcept {
            return visible && (focused || IsMouseCaptured()) &&
                !gizmoCaptured && !keyboardBlocked;
        }

        bool AcceptsWheel() const noexcept {
            return visible && hovered && !gizmoCaptured &&
                !pointerBlocked && !keyboardBlocked;
        }
    };

    class EditorViewInputRouter {
    public:
        const EditorViewInputState& Submit(
            RENDER3D::RenderViewId viewId,
            const EditorViewInputSubmission& submission);
        void SetGizmoCapture(
            RENDER3D::RenderViewId viewId,
            bool captured);
        void Clear(RENDER3D::RenderViewId viewId);
        void ClearAll();

        const EditorViewInputState* Find(
            RENDER3D::RenderViewId viewId) const noexcept;

    private:
        std::unordered_map<uint64_t, EditorViewInputState> states_{};
    };

} // namespace HIKARI::EDITOR
