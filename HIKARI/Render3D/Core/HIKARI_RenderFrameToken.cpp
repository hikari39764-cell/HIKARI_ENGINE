#include "Render3D/Core/HIKARI_RenderFrameToken.h"

#include <atomic>

namespace HIKARI::RENDER3D {
namespace {

    std::atomic<uint64_t> gNextRenderFrameGeneration{ 1u };

} // namespace

    RenderFrameToken AllocateRenderFrameToken(
        uint64_t frameIndex,
        RenderViewId viewId) noexcept {

        if (viewId.value == 0u) {
            return {};
        }
        uint64_t generation = 0u;
        do {
            generation = gNextRenderFrameGeneration.fetch_add(
                1u,
                std::memory_order_relaxed);
        } while (generation == 0u);
        return RenderFrameToken{
            frameIndex,
            viewId,
            generation
        };
    }

} // namespace HIKARI::RENDER3D
