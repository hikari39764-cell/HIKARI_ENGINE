#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/UiTypes.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/VfxTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

inline constexpr std::string_view kGameProjectAssetGuidArchiveMarker = "$hikari.assetGuid";

class PropertyReader {
public:
    HIKARI_GAME_PROJECT_SDK_API bool Contains(std::string_view name) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReadBool(std::string_view name, bool fallback = false) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API int64_t ReadInt(std::string_view name, int64_t fallback = 0) const noexcept;
    // Explicit scene references are remapped when duplicating or
    // instantiating a hierarchy. Only references written by WriteSceneObjectId are read.
    HIKARI_GAME_PROJECT_SDK_API uint64_t ReadSceneObjectId(std::string_view name, uint64_t fallback = 0) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API float ReadFloat(std::string_view name, float fallback = 0.0f) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectFloat3 ReadFloat3(std::string_view name,
                                                             GameProjectFloat3 fallback = {}) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReadVfxAssetReference(std::string_view name,
                                                           GameProjectVfxAssetReference& outReference) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReadTextureAssetReference(
        std::string_view name, GameProjectUiTextureReference& outReference) const noexcept;
    // Reads the explicit $hikari.assetGuid archive marker; legacy string values are not inferred.
    HIKARI_GAME_PROJECT_SDK_API bool ReadAssetGuid(std::string_view name, char* outUtf8,
                                                   size_t capacity) const noexcept;

    std::string ReadString(std::string_view name, std::string_view fallback = {}) const {
        if (!Contains(name)) {
            return std::string(fallback);
        }
        const size_t size = GetStringSize(name);
        if (size == 0u) {
            return {};
        }
        std::string value(size, '\0');
        if (!CopyString(name, value.data(), value.size() + 1u)) {
            return std::string(fallback);
        }
        return value;
    }

private:
    friend struct GameProjectSdkAccess;

    explicit PropertyReader(const void* properties) noexcept : properties_(properties) {}

    HIKARI_GAME_PROJECT_SDK_API size_t GetStringSize(std::string_view name) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool CopyString(std::string_view name, char* destination,
                                                size_t capacity) const noexcept;

    const void* properties_ = nullptr;
};

class PropertyWriter {
public:
    HIKARI_GAME_PROJECT_SDK_API void WriteBool(std::string_view name, bool value);
    HIKARI_GAME_PROJECT_SDK_API void WriteInt(std::string_view name, int64_t value);
    HIKARI_GAME_PROJECT_SDK_API void WriteSceneObjectId(std::string_view name, uint64_t value);
    HIKARI_GAME_PROJECT_SDK_API void WriteFloat(std::string_view name, float value);
    HIKARI_GAME_PROJECT_SDK_API void WriteString(std::string_view name, std::string_view value);
    HIKARI_GAME_PROJECT_SDK_API void WriteFloat3(std::string_view name, GameProjectFloat3 value);
    HIKARI_GAME_PROJECT_SDK_API void WriteVfxAssetReference(std::string_view name,
                                                            const GameProjectVfxAssetReference& reference);
    HIKARI_GAME_PROJECT_SDK_API void WriteTextureAssetReference(std::string_view name,
                                                                const GameProjectUiTextureReference& reference);
    HIKARI_GAME_PROJECT_SDK_API void WriteAssetGuid(std::string_view name, std::string_view assetGuid);

private:
    friend struct GameProjectSdkAccess;

    explicit PropertyWriter(void* properties) noexcept : properties_(properties) {}

    void* properties_ = nullptr;
};

} // namespace HIKARI
