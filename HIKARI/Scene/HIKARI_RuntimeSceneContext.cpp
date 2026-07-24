#include "HIKARI_RuntimeSceneContext.h"

#include <utility>

#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    SceneTransitionBus* RuntimeSceneContext::transitionBus_ = nullptr;
    std::string RuntimeSceneContext::pendingSceneId_{};
    std::string RuntimeSceneContext::pendingSpawnPointId_{};
    std::optional<ResolvedSceneEntry> RuntimeSceneContext::resolvedEntry_{};
    World* RuntimeSceneContext::currentWorld_ = nullptr;

    void RuntimeSceneContext::SetTransitionBus(SceneTransitionBus* bus) {
        transitionBus_ = bus;
    }

    SceneTransitionBus* RuntimeSceneContext::GetTransitionBus() {
        return transitionBus_;
    }


    void RuntimeSceneContext::SetCurrentWorld(World* world) {
        currentWorld_ = world;
    }

    World* RuntimeSceneContext::GetCurrentWorld() {
        return currentWorld_;
    }

    void RuntimeSceneContext::SetPendingSceneEntry(std::string sceneId, std::string spawnPointId) {
        pendingSceneId_ = std::move(sceneId);
        pendingSpawnPointId_ = std::move(spawnPointId);
        resolvedEntry_.reset();
    }

    void RuntimeSceneContext::ResolvePendingSceneEntry(const World& world, const std::string& sceneId) {
        if (pendingSpawnPointId_.empty() || pendingSceneId_ != sceneId) {
            return;
        }

        for (const auto& object : world.GetObjects()) {
            if (!object) {
                continue;
            }

            const auto* spawnPoint = object->GetComponent<SpawnPointComponent>();
            if (!spawnPoint || !spawnPoint->IsEnabled()) {
                continue;
            }
            if (spawnPoint->GetSpawnPointId() != pendingSpawnPointId_) {
                continue;
            }

            resolvedEntry_ = ResolvedSceneEntry{
                pendingSceneId_,
                pendingSpawnPointId_,
                object->GetTransform().position };
            pendingSceneId_.clear();
            pendingSpawnPointId_.clear();
            return;
        }
    }

    std::optional<ResolvedSceneEntry> RuntimeSceneContext::GetResolvedSceneEntry() {
        return resolvedEntry_;
    }

} // namespace HIKARI
