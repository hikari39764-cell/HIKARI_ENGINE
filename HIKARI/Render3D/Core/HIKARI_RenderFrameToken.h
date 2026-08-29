#pragma once

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

        bool Matches(RenderFrameToken expected) const noexcept {
            return IsValid() && expected.IsValid() &&
                frameIndex == expected.frameIndex &&
                viewId == expected.viewId &&
                generation == expected.generation;
        }

        bool MatchesFrameView(
            uint64_t expectedFrameIndex,
            RenderViewId expectedViewId) const noexcept {
            return IsValid() &&
                frameIndex == expectedFrameIndex &&
                viewId == expectedViewId;
        }

        bool operator==(const RenderFrameToken&) const noexcept = default;
    };

    // The engine module is the only publication-token issuer. Keeping the
    // counter out of this header prevents a project DLL from creating an
    // independent generation domain when this contract becomes public.
    RenderFrameToken AllocateRenderFrameToken(
        uint64_t frameIndex,
        RenderViewId viewId) noexcept;

    static_assert(std::is_standard_layout_v<RenderFrameToken>);
    static_assert(std::is_trivially_copyable_v<RenderFrameToken>);
    static_assert(sizeof(RenderFrameToken) == 24u);

} // namespace HIKARI::RENDER3D
