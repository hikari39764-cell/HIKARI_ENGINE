#pragma once

namespace HIKARI::EDITOR {

    void SetGameViewportInputRect(float x, float y, float width, float height, bool windowFocused);
    void ClearGameViewportInputRect();
    void SetGameViewportGizmoCapture(bool captured);

    bool HasGameViewportInputRect();
    bool IsGameViewportMouseHovered();
    bool IsGameViewportMouseInputActive();
    bool IsGameViewportWheelInputActive();
    bool IsGameViewportKeyboardInputActive();

} // namespace HIKARI::EDITOR
