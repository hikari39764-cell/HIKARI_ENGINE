#pragma once

#include "Render3D/Debug/HIKARI_RenderDebugView.h"

namespace HIKARI::RENDER3D {

    struct RenderDebugOutputRoute {
        RenderDebugView view = RenderDebugView::None;
        bool active = false;
        bool temporalVisualization = false;
        bool volumetricVisualization = false;
        bool bypassTemporalUpscaler = false;
        bool bypassPostProcessing = false;
    };

    void SetRenderDebugOutputView(RenderDebugView view);
    const RenderDebugOutputRoute& GetRenderDebugOutputRoute();

} // namespace HIKARI::RENDER3D
