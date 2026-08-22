#pragma once

#include <cstdint>

#include "Render2D/HIKARI_GameUiRenderData.h"

namespace HIKARI::GAME_UI {

    // FramePresentation clears the borrowed source at BeginFrame;
    // RenderSubmission installs the current world's immutable frame source.
    void BeginGameUiFrame() noexcept;
    void SetGameUiFrameSource(
        const GameUiFrameSource* source) noexcept;
    bool RenderGameUiFrame(
        uint64_t frameIndex,
        int logicalWidth,
        int logicalHeight) noexcept;
    void ShutdownGameUiRenderer() noexcept;

} // namespace HIKARI::GAME_UI
