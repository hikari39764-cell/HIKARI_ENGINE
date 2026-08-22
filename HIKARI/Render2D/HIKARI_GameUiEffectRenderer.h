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
    // Renders an editor-owned immutable source into the currently bound
    // target without borrowing or replacing the runtime HUD source.
    bool RenderGameUiPreviewFrame(
        const GameUiFrameSource& source,
        uint64_t frameIndex,
        int logicalWidth,
        int logicalHeight) noexcept;
    bool GetGameUiBackdropRequest(
        uint64_t frameIndex,
        GameProjectUiBackdropRequest& outRequest) noexcept;
    void ShutdownGameUiRenderer() noexcept;

} // namespace HIKARI::GAME_UI
