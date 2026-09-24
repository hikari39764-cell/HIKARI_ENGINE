#pragma once

#include <cstdint>
#include <memory>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::REFLECTION {

struct ScreenSpaceReflectionDebugState {
    bool requested = false;
    bool sceneSurfaceReady = false;
    bool currentHzbReady = false;
    bool pipelineReady = false;
    bool rendered = false;
    bool historyUsed = false;
    bool resolutionLimited = false;
    bool resizeDeferred = false;
    uint32_t residentViews = 0;
    uint64_t workingSetBytes = 0;
    uint32_t maxSteps = 0;
    uint32_t hzbMipCount = 0;
    uint32_t traceWidth = 0;
    uint32_t traceHeight = 0;
    float maxDistance = 0.0f;
    float maxRoughness = 0.0f;
};

class ScreenSpaceReflectionRenderer final {
public:
    ScreenSpaceReflectionRenderer();
    ~ScreenSpaceReflectionRenderer();
    ScreenSpaceReflectionRenderer(const ScreenSpaceReflectionRenderer&) = delete;
    ScreenSpaceReflectionRenderer& operator=(const ScreenSpaceReflectionRenderer&) = delete;
    bool Render(const GFX::Context& graphics, const PIPELINE::RenderTargetAccess& targetAccess,
                const SURFACE::SceneSurfaceSet& surfaces, const MESHRENDERER::CameraCB& camera,
                const ScreenSpaceReflectionSettings& settings, RenderDebugView debugView,
                const TEMPORAL::TemporalFrameState& temporal);
    void Release();

    const ScreenSpaceReflectionDebugState& GetDebugState() const { return debugState_; }

private:
    struct State;
    std::unique_ptr<State> state_;
    ScreenSpaceReflectionDebugState debugState_{};
};

} // namespace HIKARI::RENDER3D::REFLECTION
