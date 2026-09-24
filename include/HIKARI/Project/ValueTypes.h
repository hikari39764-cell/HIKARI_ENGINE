#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HIKARI {

struct GameProjectFloat2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct GameProjectWorldHandle {
    uint64_t value = 0u;

    constexpr bool IsValid() const noexcept { return value != 0u; }

    constexpr explicit operator bool() const noexcept { return IsValid(); }

    friend constexpr bool operator==(GameProjectWorldHandle lhs, GameProjectWorldHandle rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(GameProjectWorldHandle lhs, GameProjectWorldHandle rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct GameProjectObjectHandle {
    GameProjectWorldHandle world{};
    uint64_t value = 0u;

    constexpr bool IsNull() const noexcept { return !world.IsValid() && value == 0u; }

    constexpr bool IsValid() const noexcept {
        return world.IsValid() && (value & 0xFFFFFFFFu) != 0u && (value >> 32u) != 0u;
    }

    constexpr explicit operator bool() const noexcept { return IsValid(); }

    friend constexpr bool operator==(GameProjectObjectHandle lhs, GameProjectObjectHandle rhs) noexcept {
        return lhs.world == rhs.world && lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(GameProjectObjectHandle lhs, GameProjectObjectHandle rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct GameProjectFloat3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct GameProjectFloat4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct GameProjectQuaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct GameProjectTransform {
    GameProjectFloat3 position{};
    GameProjectQuaternion rotation{};
    GameProjectFloat3 scale{1.0f, 1.0f, 1.0f};
};

enum class GameProjectBindingValueType : uint8_t {
    Bool,
    Int,
    Float,
    Float2,
    Float3,
    Float4,
    Color,
};

struct GameProjectBindingValue {
    GameProjectBindingValueType type = GameProjectBindingValueType::Float;
    bool boolean = false;
    int64_t integer = 0;
    float numeric[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct GameProjectEventRecord {
    uint64_t sequence = 0;
    uint64_t frameIndex = 0;
    GameProjectObjectHandle sourceObjectHandle{};
    bool hasPayload = false;
    GameProjectBindingValue payload{};
};

static_assert(std::is_standard_layout_v<GameProjectWorldHandle>);
static_assert(std::is_trivially_copyable_v<GameProjectWorldHandle>);
static_assert(sizeof(GameProjectWorldHandle) == 8u);
static_assert(offsetof(GameProjectWorldHandle, value) == 0u);
static_assert(std::is_standard_layout_v<GameProjectObjectHandle>);
static_assert(std::is_trivially_copyable_v<GameProjectObjectHandle>);
static_assert(sizeof(GameProjectObjectHandle) == 16u);
static_assert(offsetof(GameProjectObjectHandle, world) == 0u);
static_assert(offsetof(GameProjectObjectHandle, value) == 8u);
static_assert(sizeof(GameProjectFloat2) == 8u);
static_assert(sizeof(GameProjectFloat3) == 12u);
static_assert(sizeof(GameProjectFloat4) == 16u);
static_assert(sizeof(GameProjectQuaternion) == 16u);
static_assert(sizeof(GameProjectTransform) == 40u);
static_assert(offsetof(GameProjectTransform, position) == 0u);
static_assert(offsetof(GameProjectTransform, rotation) == 12u);
static_assert(offsetof(GameProjectTransform, scale) == 28u);
static_assert(sizeof(GameProjectBindingValue) == 32u);
static_assert(offsetof(GameProjectBindingValue, type) == 0u);
static_assert(offsetof(GameProjectBindingValue, boolean) == 1u);
static_assert(offsetof(GameProjectBindingValue, integer) == 8u);
static_assert(offsetof(GameProjectBindingValue, numeric) == 16u);
static_assert(sizeof(GameProjectEventRecord) == 72u);
static_assert(offsetof(GameProjectEventRecord, sourceObjectHandle) == 16u);
static_assert(offsetof(GameProjectEventRecord, hasPayload) == 32u);
static_assert(offsetof(GameProjectEventRecord, payload) == 40u);

} // namespace HIKARI
