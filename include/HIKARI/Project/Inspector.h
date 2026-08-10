#pragma once

#include <cstddef>
#include <string_view>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    struct GameProjectSdkAccess;

    class GameProjectInspector {
    public:
        HIKARI_GAME_PROJECT_SDK_API void Text(
            std::string_view text);
        HIKARI_GAME_PROJECT_SDK_API bool Bool(
            std::string_view label,
            bool& value);
        HIKARI_GAME_PROJECT_SDK_API bool Int(
            std::string_view label,
            int& value);
        HIKARI_GAME_PROJECT_SDK_API bool Float(
            std::string_view label,
            float& value);
        HIKARI_GAME_PROJECT_SDK_API bool FloatRange(
            std::string_view label,
            float& value,
            float minimum,
            float maximum,
            float speed = 0.1f);
        HIKARI_GAME_PROJECT_SDK_API bool StringBuffer(
            std::string_view label,
            char* value,
            size_t capacity);
        HIKARI_GAME_PROJECT_SDK_API bool Vec2(
            std::string_view label,
            float& x,
            float& y);
        HIKARI_GAME_PROJECT_SDK_API bool VariableBinding(
            std::string_view label,
            GameProjectBindingValueType expectedType,
            char* value,
            size_t capacity);
        HIKARI_GAME_PROJECT_SDK_API bool EventBinding(
            std::string_view label,
            bool hasPayload,
            GameProjectBindingValueType payloadType,
            char* value,
            size_t capacity);

    private:
        friend struct GameProjectSdkAccess;

        explicit GameProjectInspector(void* builder) noexcept
            : builder_(builder) {}

        void* builder_ = nullptr;
    };

} // namespace HIKARI
