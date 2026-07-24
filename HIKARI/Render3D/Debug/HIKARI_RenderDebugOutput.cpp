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
        gRoute.bypassPostProcessing = gRoute.active;
    }

    const RenderDebugOutputRoute& GetRenderDebugOutputRoute() {
        return gRoute;
    }

} // namespace HIKARI::RENDER3D
