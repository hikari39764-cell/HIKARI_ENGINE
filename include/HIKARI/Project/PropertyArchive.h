#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    struct GameProjectSdkAccess;

    class PropertyReader {
    public:
        HIKARI_GAME_PROJECT_SDK_API bool Contains(
            std::string_view name) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ReadBool(
            std::string_view name,
            bool fallback = false) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API int64_t ReadInt(
            std::string_view name,
            int64_t fallback = 0) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API float ReadFloat(
            std::string_view name,
            float fallback = 0.0f) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectFloat3 ReadFloat3(
            std::string_view name,
            GameProjectFloat3 fallback = {}) const noexcept;

        std::string ReadString(
            std::string_view name,
            std::string_view fallback = {}) const;

    private:
        friend struct GameProjectSdkAccess;

        explicit PropertyReader(const void* properties) noexcept
            : properties_(properties) {}

        HIKARI_GAME_PROJECT_SDK_API size_t GetStringSize(
            std::string_view name) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool CopyString(
            std::string_view name,
            char* destination,
            size_t capacity) const noexcept;

        const void* properties_ = nullptr;
    };

    class PropertyWriter {
    public:
        HIKARI_GAME_PROJECT_SDK_API void WriteBool(
            std::string_view name,
            bool value);
        HIKARI_GAME_PROJECT_SDK_API void WriteInt(
            std::string_view name,
            int64_t value);
        HIKARI_GAME_PROJECT_SDK_API void WriteFloat(
            std::string_view name,
            float value);
        HIKARI_GAME_PROJECT_SDK_API void WriteString(
            std::string_view name,
            std::string_view value);
        HIKARI_GAME_PROJECT_SDK_API void WriteFloat3(
            std::string_view name,
            GameProjectFloat3 value);

    private:
        friend struct GameProjectSdkAccess;

        explicit PropertyWriter(void* properties) noexcept
            : properties_(properties) {}

        void* properties_ = nullptr;
    };

} // namespace HIKARI
