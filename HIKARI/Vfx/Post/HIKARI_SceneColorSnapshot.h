#pragma once

#include <cstdint>

namespace HIKARI::POST {

    enum class SceneColorSnapshotKind : uint8_t {
        Opaque = 0u,
        PostOpaque,
        Final,
        Count,
    };

} // namespace HIKARI::POST
