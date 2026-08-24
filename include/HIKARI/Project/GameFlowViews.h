#pragma once

#include <cstdint>
#include <string_view>

#include "HIKARI/Project/GameFlowTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"

namespace HIKARI {

    struct GameProjectSdkAccess;

    class SceneFlowView {
    public:
        SceneFlowView() = default;
        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool GetCurrentSceneAssetGuid(
            char* outUtf8,
            uint32_t capacity,
            uint32_t& outLength) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool RequestScene(
            std::string_view sceneAssetGuid,
            std::string_view spawnPointId,
            std::string_view transitionProfileId,
            uint64_t userData = 0u,
            uint8_t useTransition = 1u) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ConsumeSceneEntry(
            GameProjectSceneEntry& outEntry) const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit SceneFlowView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

    class SaveGameView {
    public:
        SaveGameView() = default;
        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool Exists(
            std::string_view slotId) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint64_t GetSize(
            std::string_view slotId) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Read(
            std::string_view slotId,
            void* outBytes,
            uint64_t capacity,
            uint64_t& outSize) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool WriteAtomic(
            std::string_view slotId,
            const void* bytes,
            uint64_t size) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Delete(
            std::string_view slotId) const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit SaveGameView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

    class RuntimeSessionView {
    public:
        RuntimeSessionView() = default;
        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API GameProjectRuntimeHostKind
            GetHostKind() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool RequestEndSession() const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit RuntimeSessionView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

} // namespace HIKARI
