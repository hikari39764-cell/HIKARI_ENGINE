#pragma once

#include <cstdint>
#include <type_traits>
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

enum class GameProjectWorldTextPatchField : uint32_t {
    Visible = 1u << 0u,
    Color = 1u << 1u,
    OutlineColor = 1u << 2u,
    OutlineWidth = 1u << 3u,
    Opacity = 1u << 4u,
    RevealAmount = 1u << 5u,
    LayoutSize = 1u << 6u,
    FontHeight = 1u << 7u,
};

constexpr uint32_t GameProjectWorldTextPatchFields(GameProjectWorldTextPatchField field) noexcept {
    return static_cast<uint32_t>(field);
}

inline constexpr uint32_t kGameProjectWorldTextPatchKnownFields = 0xffu;
inline constexpr uint32_t kGameProjectWorldTextStateVersion = 1u;
inline constexpr uint32_t kGameProjectWorldTextMaximumBytes = 65536u;

struct GameProjectWorldTextState {
    uint32_t structSize = sizeof(GameProjectWorldTextState);
    uint32_t version = kGameProjectWorldTextStateVersion;
    GameProjectFloat3 color{1.0f, 1.0f, 1.0f};
    GameProjectFloat3 outlineColor{};
    GameProjectFloat2 layoutSize{4.0f, 1.0f};
    float fontHeight = 0.5f;
    float outlineWidth = 0.04f;
    float opacity = 1.0f;
    float revealAmount = 1.0f;
    uint32_t visible = 1u;
    uint32_t reserved = 0u;
};

// 選択した値を検証してから一括適用する。Scene の保存データは変更しない。
struct GameProjectWorldTextPatch {
    uint32_t structSize = sizeof(GameProjectWorldTextPatch);
    uint32_t version = kGameProjectWorldTextStateVersion;
    uint32_t fieldMask = 0u;
    uint32_t reserved = 0u;
    GameProjectWorldTextState values{};
};

static_assert(sizeof(GameProjectWorldTextState) == 64u);
static_assert(sizeof(GameProjectWorldTextPatch) == 80u);
static_assert(std::is_trivially_copyable_v<GameProjectWorldTextState>);
static_assert(std::is_trivially_copyable_v<GameProjectWorldTextPatch>);

} // namespace HIKARI
