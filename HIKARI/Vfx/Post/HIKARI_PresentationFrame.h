#pragma once

#include <cstdint>

#include <d3d12.h>

namespace HIKARI {
    class RenderTarget2D;
}

namespace HIKARI::POST {

    struct PresentationFrameResources {
        uint64_t frameIndex = 0;
        int width = 0;
        int height = 0;
        int uiLogicalWidth = 0;
        int uiLogicalHeight = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        RenderTarget2D* hudlessColor = nullptr;
        RenderTarget2D* uiColorAndAlpha = nullptr;
        RenderTarget2D* finalColor = nullptr;
        bool hudlessReady = false;
        bool uiReady = false;
        bool finalReady = false;

        bool HasFrameGenerationInputs() const {
            return
                hudlessReady &&
                uiReady &&
                hudlessColor != nullptr &&
                uiColorAndAlpha != nullptr &&
                width > 0 &&
                height > 0;
        }
    };

} // namespace HIKARI::POST
