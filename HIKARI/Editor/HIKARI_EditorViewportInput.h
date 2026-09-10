#pragma once

#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI::EDITOR {

    void SetGameViewportInputRect(float x, float y, float width, float height, bool windowFocused);
    void ClearGameViewportInputRect();
    void SetGameViewportGizmoCapture(bool captured);
    void SetGameViewportPlayInputEnabled(bool enabled);
    bool IsGameViewportPlayInputActive() noexcept;
    bool TryGetGameViewportPointer(INPUT::InputPointerState& pointer,
        uint32_t& width, uint32_t& height) noexcept;

    bool HasGameViewportInputRect();
    bool IsGameViewportMouseHovered();
    bool IsGameViewportMouseInputActive();
    bool IsGameViewportWheelInputActive();
    bool IsGameViewportKeyboardInputActive();

} // namespace HIKARI::EDITOR
