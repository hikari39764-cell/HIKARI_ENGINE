#pragma once

#include <cstdint>

namespace HIKARI {

    struct GameProjectFloat3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
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
        GameProjectBindingValueType type =
            GameProjectBindingValueType::Float;
        bool boolean = false;
        int64_t integer = 0;
        float numeric[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct GameProjectEventRecord {
        uint64_t sequence = 0;
        uint64_t frameIndex = 0;
        uint64_t sourceObjectHandle = 0;
        bool hasPayload = false;
        GameProjectBindingValue payload{};
    };

} // namespace HIKARI
