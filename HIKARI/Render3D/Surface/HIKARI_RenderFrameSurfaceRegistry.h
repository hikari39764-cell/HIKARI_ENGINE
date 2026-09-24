#pragma once

#include <cstdint>
#include <type_traits>

#include "Render3D/Core/HIKARI_RenderFrameToken.h"

namespace HIKARI::RENDER3D::SURFACE {

inline constexpr uint32_t kRenderFrameSurfaceMaximumResidentViewCount = 2u;

struct RenderFrameSurfaceReceipt {
    RenderFrameToken frame{};
    uint64_t deviceGeneration = 0u;
    uint64_t registryGeneration = 0u;

    bool IsValid() const noexcept { return frame.IsValid() && deviceGeneration != 0u && registryGeneration != 0u; }
    bool Matches(RenderFrameSurfaceReceipt expected) const noexcept {
        return IsValid() && expected.IsValid() && frame.Matches(expected.frame) &&
               deviceGeneration == expected.deviceGeneration && registryGeneration == expected.registryGeneration;
    }
};

struct SceneSurfaceSet;
bool BeginRenderFrameSurfaces(RenderFrameToken frame, uint64_t deviceGeneration,
                              RenderFrameSurfaceReceipt& outReceipt) noexcept;
// Owner-thread-only borrow. Invalid after Begin replacement, End, Abort,
// or Shutdown; callers must retain the receipt, never the returned pointer.
SceneSurfaceSet* TryGetRenderFrameSurfaces(RenderFrameSurfaceReceipt receipt) noexcept;
bool EndRenderFrameSurfaces(RenderFrameSurfaceReceipt receipt) noexcept;
bool AbortRenderFrameSurfaces(RenderFrameSurfaceReceipt receipt) noexcept;
bool ShutdownRenderFrameSurfaceRegistry() noexcept;

static_assert(std::is_standard_layout_v<RenderFrameSurfaceReceipt>);
static_assert(std::is_trivially_copyable_v<RenderFrameSurfaceReceipt>);

} // namespace HIKARI::RENDER3D::SURFACE
