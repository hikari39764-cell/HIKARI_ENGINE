#include "Render3D/Debug/HIKARI_RenderDebugOutput.h"

namespace HIKARI::RENDER3D {

namespace {
RenderDebugOutputRoute gRoute{};
}

void SetRenderDebugOutputView(RenderDebugView view) {
    gRoute.view = view;
    gRoute.active = view != RenderDebugView::None;
    gRoute.temporalVisualization = IsTemporalRenderDebugView(view);
    gRoute.volumetricVisualization = IsVolumetricRenderDebugView(view);
    gRoute.bypassTemporalUpscaler = gRoute.active;
    // 路径追跡の全光は Lit と比べるので、露出と色調の写像を同じに通す。
    gRoute.bypassPostProcessing = gRoute.active && view != RenderDebugView::PathTraced;
}

const RenderDebugOutputRoute& GetRenderDebugOutputRoute() { return gRoute; }

} // namespace HIKARI::RENDER3D
