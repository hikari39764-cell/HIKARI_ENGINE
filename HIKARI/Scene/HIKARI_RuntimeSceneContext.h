#pragma once

#include <optional>
#include <string>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    class SceneTransitionBus;
    class World;

    struct ResolvedSceneEntry {
        std::string sceneId{};
        std::string spawnPointId{};
        MATH::Vec3 worldPosition{};
    };

    class RuntimeSceneContext {
    public:
        static void SetTransitionBus(SceneTransitionBus* bus);
        static SceneTransitionBus* GetTransitionBus();

        static void SetPendingSceneEntry(std::string sceneId, std::string spawnPointId);

        static void SetCurrentWorld(World* world);
        static World* GetCurrentWorld();
        static void ResolvePendingSceneEntry(const World& world, const std::string& sceneId);
        static std::optional<ResolvedSceneEntry> GetResolvedSceneEntry();

    private:
        static SceneTransitionBus* transitionBus_;
        static std::string pendingSceneId_;
        static std::string pendingSpawnPointId_;
        static std::optional<ResolvedSceneEntry> resolvedEntry_;
        static World* currentWorld_;
    };

} // namespace HIKARI
