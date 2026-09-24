#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D {

struct RenderViewId {
    uint64_t value = 0u;

    bool operator==(const RenderViewId& rhs) const noexcept { return value == rhs.value; }
};

inline constexpr RenderViewId kPrimaryRenderViewId{1u};
// Base identity for runtime-owned secondary captures. Editor tool views
// currently occupy 2..7; capture slots use the bounded range 8..11 and
// therefore never alias their temporal/debug state.
inline constexpr RenderViewId kRuntimeSceneCaptureRenderViewId{8u};

} // namespace HIKARI::RENDER3D
