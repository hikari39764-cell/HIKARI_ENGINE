#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"

namespace HIKARI::RENDER3D::WATER {

// The visibility DSV clears to device depth 1.0.  Both visibility pixel
// shaders clamp a water sample that reaches the far clip plane to this
// value, so the optional view can distinguish water from an untouched
// texel without changing authoritative SceneDepth.
inline constexpr float kWaterVisibilityHorizonDepth = 0.99999f;

// Borrowed view of the renderer-owned water visibility depth for one
// frame/view. The water stage owns the resource and descriptor lifetime.
struct WaterVisibleDepthView {
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t frameIndex = 0;
    const ID3D12Resource* viewToken = nullptr;
    bool valid = false;

    bool IsValid() const noexcept { return valid && srv.ptr != 0 && width != 0 && height != 0 && viewToken != nullptr; }
};

// Draws the visible water surface into an independent depth resource.
// passResources.sceneDepth is read-only input; it is never used as the
// render target and remains the authoritative opaque SceneDepth.
WaterVisibleDepthView RenderWaterVisibleDepth(const GFX::Context& graphics,
                                              const HIKARI::MESHRENDERER::MeshPassResources& passResources,
                                              uint32_t width, uint32_t height, uint64_t frameIndex,
                                              const ID3D12Resource* viewToken);

void ShutdownWaterVisibleDepth();

} // namespace HIKARI::RENDER3D::WATER
