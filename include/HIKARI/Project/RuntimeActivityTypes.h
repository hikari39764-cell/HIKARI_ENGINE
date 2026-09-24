#pragma once

#include <cstdint>

namespace HIKARI {

// Runtime-only activity hint for project-owned actors. The engine keeps
// ownership of animation, rendering, physics, and terrain services.
enum class GameProjectRuntimeActivity : uint8_t { Active = 0, FrozenVisible = 1, SuspendedHidden = 2 };

} // namespace HIKARI
