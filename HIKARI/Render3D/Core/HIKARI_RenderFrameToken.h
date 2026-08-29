#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

#include "Render3D/Core/HIKARI_RenderViewIdentity.h"

namespace HIKARI::RENDER3D {

    // Stable identity for one publication of one render view's frame-local
    // resources. Handles and borrowed surface views must not outlive it.
    struct RenderFrameToken {
        uint64_t frameIndex = 0u;
        RenderViewId viewId{};
        uint64_t generation = 0u;

        bool IsValid() const noexcept {
            return viewId.value != 0u && generation != 0u;
        }

        bool Matches(
            uint64_t expectedFrameIndex,
            RenderViewId expectedViewId) const noexcept {
            return IsValid() &&
                frameIndex == expectedFrameIndex &&
                viewId == expectedViewId;
        }

        bool operator==(const RenderFrameToken&) const noexcept = default;
    };

    // Publication generations are monotonic within the owning engine module
    // until uint64 wrap. The frame/view tuple remains part of identity, and
    // generation zero is permanently reserved for invalid/default tokens.
    inline RenderFrameToken AllocateRenderFrameToken(
        uint64_t frameIndex,
        RenderViewId viewId) noexcept {
        static std::atomic<uint64_t> nextGeneration{ 1u };
        uint64_t generation = 0u;
        do {
            generation = nextGeneration.fetch_add(
                1u,
                std::memory_order_relaxed);
        } while (generation == 0u);
        return RenderFrameToken{
            frameIndex,
            viewId,
            generation
        };
    }

    static_assert(std::is_standard_layout_v<RenderFrameToken>);
    static_assert(std::is_trivially_copyable_v<RenderFrameToken>);
    static_assert(sizeof(RenderFrameToken) == 24u);

} // namespace HIKARI::RENDER3D
