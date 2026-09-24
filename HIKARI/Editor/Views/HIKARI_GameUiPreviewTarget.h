#pragma once
#include "Render2D/HIKARI_GameUiRenderData.h"
#include "Render2D/HIKARI_SurfaceTextureTargetRing.h"

namespace HIKARI::EDITOR {
class GameUiPreviewTarget {
public:
    ~GameUiPreviewTarget() { Reset(); }
    D3D12_GPU_DESCRIPTOR_HANDLE
    Render(const GAME_UI::GameUiFrameSource& source, uint32_t width, uint32_t height,
           GameProjectUiColor clear = {.035f, .045f, .065f, 1});
    uint32_t FailedTextRequests() const { return failedTextRequests_; }
    void Reset() {
        target_.Reset();
        published_ = {};
        fence_ = ~uint64_t(0);
        failedTextRequests_ = 0;
    }

private:
    SurfaceTextureTargetRing target_;
    D3D12_GPU_DESCRIPTOR_HANDLE published_{};
    uint64_t fence_ = ~uint64_t(0);
    uint32_t failedTextRequests_ = 0;
};
} // namespace HIKARI::EDITOR
