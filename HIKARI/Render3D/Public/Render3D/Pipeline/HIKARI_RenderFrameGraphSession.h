#pragma once

#include <cstdint>
#include <type_traits>

#include "Render3D/Surface/HIKARI_RenderFrameSurfaceRegistry.h"

namespace HIKARI::RENDER3D::GRAPH {
class RenderGraph;
}

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {
struct StageExecutionState;
}

namespace HIKARI::RENDER3D::PIPELINE {

struct RenderFrameGraphSessionReceipt {
    SURFACE::RenderFrameSurfaceReceipt surfaces{};
    uint64_t sessionGeneration = 0u;

    bool IsValid() const noexcept { return surfaces.IsValid() && sessionGeneration != 0u; }
    bool Matches(RenderFrameGraphSessionReceipt expected) const noexcept {
        return IsValid() && expected.IsValid() && surfaces.Matches(expected.surfaces) &&
               sessionGeneration == expected.sessionGeneration;
    }
};

bool BeginRenderFrameGraphSession(SURFACE::RenderFrameSurfaceReceipt surfaces,
                                  RenderFrameGraphSessionReceipt& outReceipt) noexcept;
// Owner-thread-only validation. The receipt remains borrowed from the
// active surface publication and must not outlive that publication.
bool IsRenderFrameGraphSessionCurrent(RenderFrameGraphSessionReceipt receipt) noexcept;
bool ContinueRenderFrameGraphSession(RenderFrameGraphSessionReceipt receipt) noexcept;
bool EndRenderFrameGraphSession(RenderFrameGraphSessionReceipt receipt) noexcept;
bool AbortRenderFrameGraphSession(RenderFrameGraphSessionReceipt receipt) noexcept;
bool ShutdownRenderFrameGraphSessions() noexcept;

static_assert(std::is_standard_layout_v<RenderFrameGraphSessionReceipt>);
static_assert(std::is_trivially_copyable_v<RenderFrameGraphSessionReceipt>);

} // namespace HIKARI::RENDER3D::PIPELINE

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

GRAPH::RenderGraph* TryGetRenderFrameGraph(RenderFrameGraphSessionReceipt receipt) noexcept;
StageExecutionState* TryGetRenderFrameGraphStageState(RenderFrameGraphSessionReceipt receipt) noexcept;

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
