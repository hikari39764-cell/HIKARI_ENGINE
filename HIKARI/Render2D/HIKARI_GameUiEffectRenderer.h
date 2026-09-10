#pragma once

#include <cstdint>
#include "HIKARI/Project/UiCanvasTypes.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

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
    bool RenderGameUiWorkspacePreviewFrame(const GameUiFrameSource& source,int width,int height) noexcept;
    bool GetGameUiBackdropRequest(
        uint64_t frameIndex,
        GameProjectUiBackdropRequest& outRequest) noexcept;
    bool RenderGameUiCanvasTarget(uint32_t slot, uint64_t worldIdentity,
        const GameUiFrameSource& source, uint64_t revision, uint32_t width, uint32_t height,
        GameProjectUiColor clearColor, RENDER3D::TextureResourceHandle& output,
        uint64_t& renderedRevision, uint64_t& renderedFrame) noexcept;
    void ResetGameUiCanvasTarget(uint32_t slot) noexcept;
    void ShutdownGameUiRenderer() noexcept;

} // namespace HIKARI::GAME_UI
