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
        // Returns true only when the idle transition gate accepts the
        // request. Scene loading completes later and may still fail.
        HIKARI_GAME_PROJECT_SDK_API bool RequestScene(
            std::string_view sceneAssetGuid,
            std::string_view spawnPointId,
            std::string_view transitionProfileId,
            uint64_t userData = 0u,
            uint8_t useTransition = 1u) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool RequestSceneWithHeldReveal(
            std::string_view sceneAssetGuid,
            std::string_view spawnPointId,
            std::string_view transitionProfileId,
            uint64_t userData = 0u) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ReleaseTransitionIn() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ConsumeSceneEntry(
            GameProjectSceneEntry& outEntry) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool IsTransitioning() const noexcept;
        // Zero means rejection (busy, invalid request or no active runtime).
        // Reacquire this callback-scoped View after a World/scene change.
        HIKARI_GAME_PROJECT_SDK_API uint64_t RequestSceneTracked(
            std::string_view sceneAssetGuid, std::string_view spawnPointId = {},
            std::string_view transitionProfileId = {}, uint64_t userData = 0,
            uint8_t useTransition = 1, uint8_t holdReveal = 0) const noexcept;
        // Non-consuming, owner-thread query; the transition service retains
        // eight admitted requests across World switches. Zero reads the latest
        // admitted request; expired tokens return false. Match userData when
        // more than one gameplay owner can request scene changes.
        HIKARI_GAME_PROJECT_SDK_API bool ReadRequestResult(
            uint64_t requestId, GameProjectSceneRequestResult& outResult) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool CancelRequest(uint64_t requestId) const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit SceneFlowView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

    class DialogueView {
    public:
        DialogueView() = default;
        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool Start(
            std::string_view assetGuid,
            uint64_t actorObjectHandle,
            std::string_view locale = "ja-JP",
            uint8_t pauseWorld = 1u) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool IsActive() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Stop() const noexcept;
        // Consumes the oldest retained event. The World keeps at most
        // kGameProjectDialogueEventQueueCapacity events and discards the
        // oldest entry on overflow. Ending a dialogue does not clear events.
        // This owner-thread-only call clears outEvent on failure.
        HIKARI_GAME_PROJECT_SDK_API bool ConsumeEvent(
            GameProjectDialogueEvent& outEvent) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint32_t
            GetPendingEventCount() const noexcept;
        // Saturating count of events discarded since this World's dialogue
        // service was reset. Consumers can compare snapshots to detect loss.
        HIKARI_GAME_PROJECT_SDK_API uint64_t
            GetDroppedEventCount() const noexcept;
        // Legacy bulk gate: marks every one-shot DialogueTrigger in the
        // current World consumed. Persistence code should use the mask APIs
        // below so completing one dialogue cannot suppress unrelated ones.
        HIKARI_GAME_PROJECT_SDK_API uint32_t
            ConsumeOneShotTriggers() const noexcept;
        // Maps enabled one-shot DialogueTriggers to stable handle order and
        // captures only the triggers already consumed in this World.
        HIKARI_GAME_PROJECT_SDK_API uint64_t
            CaptureOneShotTriggerMask() const noexcept;
        // Restores the exact per-trigger completion mask into a rebuilt
        // World. Unlike ConsumeOneShotTriggers this never consumes unrelated
        // dialogue volumes.
        HIKARI_GAME_PROJECT_SDK_API uint32_t
            RestoreOneShotTriggerMask(uint64_t completedMask) const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit DialogueView(void* world) noexcept : world_(world) {}
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
        // True only while the current World is executing Play/Standalone
        // gameplay. Editor authoring frames return false.
        HIKARI_GAME_PROJECT_SDK_API bool IsPlayActive() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool RequestEndSession() const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit RuntimeSessionView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

} // namespace HIKARI
