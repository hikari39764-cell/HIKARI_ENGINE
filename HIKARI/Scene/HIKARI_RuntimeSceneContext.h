#pragma once

#include <optional>
#include <cstdint>
#include <string>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    class SceneTransitionBus;
    class World;

    struct ResolvedSceneEntry {
        std::string sceneId{};
        std::string spawnPointId{};
        MATH::Vec3 worldPosition{};
        MATH::Quat worldRotation = MATH::Quat::Identity();
        uint64_t userData = 0u;
        bool hasWorldTransform = false;
    };

    class RuntimeSceneContext {
    public:
        static void SetTransitionBus(SceneTransitionBus* bus);
        static SceneTransitionBus* GetTransitionBus();

        static void SetPendingSceneEntry(
            std::string sceneId,
            std::string spawnPointId,
            uint64_t userData = 0u);
        static void ClearPendingSceneEntry();

        static void SetCurrentWorld(
            World* world,
            std::string sceneId = {});
        static World* GetCurrentWorld();
        static const std::string& GetCurrentSceneId();
        static void MarkCameraReady(const World& world) noexcept;
        static bool IsCameraReady(const World* world = nullptr) noexcept;
        static void ResolvePendingSceneEntry(const World& world, const std::string& sceneId);
        static std::optional<ResolvedSceneEntry> GetResolvedSceneEntry();
        static std::optional<ResolvedSceneEntry> ConsumeResolvedSceneEntry(
            const World& world);

    private:
        static SceneTransitionBus* transitionBus_;
        static std::string pendingSceneId_;
        static std::string pendingSpawnPointId_;
        static uint64_t pendingUserData_;
        static std::optional<ResolvedSceneEntry> resolvedEntry_;
        static World* currentWorld_;
        static std::string currentSceneId_;
        static bool cameraReady_;
    };

} // namespace HIKARI
