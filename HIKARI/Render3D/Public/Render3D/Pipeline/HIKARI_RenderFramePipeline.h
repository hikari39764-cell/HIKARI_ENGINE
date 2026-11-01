#pragma once

#include <cstdint>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"

namespace HIKARI {
class Camera3D;
struct SceneEnvironment;
namespace RENDER3D::LIGHTING {
struct SceneLightingSet;
}
} // namespace HIKARI

namespace HIKARI::RENDER3D::DYNAMICGI {
struct Stats;
}
namespace HIKARI::RENDER3D::RAYTRACING {
struct RuntimeRayTracingStats;
}

namespace HIKARI::RENDER3D::PIPELINE {

bool RenderMeshLightingFrame(const GFX::Context& graphics, const RenderViewContext& view,
                             const SceneEnvironment& environment, const LIGHTING::SceneLightingSet& lighting,
                             RenderDebugView debugView = RenderDebugView::None);

bool RenderMeshCaptureOpaqueFrame(const GFX::Context& graphics, const Camera3D& camera,
                                  const SceneEnvironment& environment, const LIGHTING::SceneLightingSet& lighting,
                                  uint32_t width, uint32_t height);

const RAYTRACING::RuntimeRayTracingStats& GetRuntimeRayTracingStats() noexcept;
const DYNAMICGI::Stats& GetDynamicDiffuseGiStats() noexcept;
uint64_t GetPrimaryFrameFailureCount() noexcept;
void ShutdownRenderFramePipelineResources() noexcept;

} // namespace HIKARI::RENDER3D::PIPELINE
