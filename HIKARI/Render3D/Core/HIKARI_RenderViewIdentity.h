#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D {

    struct RenderViewId {
        uint64_t value = 0u;

        bool operator==(const RenderViewId& rhs) const noexcept {
            return value == rhs.value;
        }
    };

    inline constexpr RenderViewId kPrimaryRenderViewId{ 1u };

} // namespace HIKARI::RENDER3D
