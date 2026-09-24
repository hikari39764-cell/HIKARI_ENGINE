#pragma once

#include <cstdint>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

enum class GameProjectNavigationStatus : uint8_t {
    Unavailable = 0,
    Success = 1,
    Partial = 2,
    Failed = 3,
    BufferTooSmall = 4,
};

enum class GameProjectNavigationPathPointFlags : uint8_t {
    None = 0,
    Start = 1u << 0u,
    End = 1u << 1u,
    OffMeshConnection = 1u << 2u,
};

struct GameProjectNavigationPathPoint {
    GameProjectFloat3 position{};
    GameProjectNavigationPathPointFlags flags = GameProjectNavigationPathPointFlags::None;
    uint8_t reserved[3]{};
};

struct GameProjectNavigationPathResult {
    GameProjectNavigationStatus status = GameProjectNavigationStatus::Unavailable;
    uint8_t partial = 0u;
    uint8_t reserved[2]{};
    uint32_t pointCount = 0u;
    float pathLength = 0.0f;
};

} // namespace HIKARI
