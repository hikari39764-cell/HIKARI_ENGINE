#pragma once

#include <cstdint>
#include "HIKARI/Project/UiTypes.h"
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {
inline constexpr uint32_t kGameProjectUiCanvasVersion = 1;
inline constexpr uint32_t kGameProjectUiCanvasMaximumCount = 4;
inline constexpr uint32_t kGameProjectUiCanvasMaximumDraws = 512;

struct GameProjectUiCanvasHandle {
    uint64_t value{}, epoch{}, worldIdentity{};
    bool IsValid() const noexcept { return value != 0 && epoch != 0 && worldIdentity != 0; }
    bool operator==(const GameProjectUiCanvasHandle&) const noexcept = default;
};

struct GameProjectUiCanvasDesc {
    uint32_t structSize = sizeof(GameProjectUiCanvasDesc);
    uint32_t version = kGameProjectUiCanvasVersion;
    uint32_t width = 768, height = 512;
    GameProjectUiColor clearColor{0, 0, 0, 1};
};

struct GameProjectUiCanvasSurfaceBinding {
    uint32_t structSize = sizeof(GameProjectUiCanvasSurfaceBinding);
    uint32_t version = kGameProjectUiCanvasVersion;
    GameProjectUiCanvasHandle canvas{};
    GameProjectObjectHandle modelObject{};
    char materialSlotName[96]{};
    float x = 0, y = 0, width = 1, height = 1;
    uint32_t flipX = 0, flipY = 0, quarterTurns = 0;
};

struct GameProjectUiCanvasSnapshot {
    uint32_t width{}, height{};
    uint64_t contentRevision{}, renderedRevision{}, renderedFrame{};
    uint32_t ready{}, updating{};
};
} // namespace HIKARI
