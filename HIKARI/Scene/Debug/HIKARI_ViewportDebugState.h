#pragma once

#include "Render3D/Debug/HIKARI_RenderDebugView.h"

namespace HIKARI {

    struct ViewportOverlayState {
        bool showGrid = true;
        bool showAxis = true;
        bool showLights = true;
        bool showProbeLabels = true;
        bool showXRayGizmos = false;
    };

    struct ViewportPerformanceState {
        bool disableSsaoInEditorViewport = false;
    };

    struct ViewportDebugViewState {
        RenderDebugView renderView = RenderDebugView::None;
        bool showLegend = true;
        bool freezeCullingCamera = false;
    };

} // namespace HIKARI
