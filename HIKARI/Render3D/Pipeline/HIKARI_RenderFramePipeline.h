#pragma once

#include <cstdint>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"

namespace HIKARI {
    class Camera3D;
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::PIPELINE {

    bool RenderMeshLightingFrame(
        const GFX::Context& graphics,
        const RenderViewContext& view,
        const SceneEnvironment& environment,
        RenderDebugView debugView = RenderDebugView::None);

    bool RenderMeshCaptureOpaqueFrame(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t width,
        uint32_t height);

} // namespace HIKARI::RENDER3D::PIPELINE
