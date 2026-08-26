#include "HIKARI_RuntimeSceneContext.h"

#include <utility>

#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    SceneTransitionBus* RuntimeSceneContext::transitionBus_ = nullptr;
    std::string RuntimeSceneContext::pendingSceneId_{};
    std::string RuntimeSceneContext::pendingSpawnPointId_{};
    uint64_t RuntimeSceneContext::pendingUserData_ = 0u;
    std::optional<ResolvedSceneEntry> RuntimeSceneContext::resolvedEntry_{};
    World* RuntimeSceneContext::currentWorld_ = nullptr;
    std::string RuntimeSceneContext::currentSceneId_{};
    bool RuntimeSceneContext::cameraReady_ = false;

    void RuntimeSceneContext::SetTransitionBus(SceneTransitionBus* bus) {
        transitionBus_ = bus;
        if (bus == nullptr) {
            ClearPendingSceneEntry();
            resolvedEntry_.reset();
        }
    }

    SceneTransitionBus* RuntimeSceneContext::GetTransitionBus() {
        return transitionBus_;
    }


    void RuntimeSceneContext::SetCurrentWorld(
        World* world,
        std::string sceneId) {
        currentWorld_ = world;
        currentSceneId_ = world != nullptr ? std::move(sceneId) : std::string{};
        cameraReady_ = false;
        if (world == nullptr) {
            // Resolved entries contain runtime-world placement. They must not
            // survive a teardown and later be consumed by a rebuilt World
            // that happens to reuse the same address.
            resolvedEntry_.reset();
        }
    }

    World* RuntimeSceneContext::GetCurrentWorld() {
        return currentWorld_;
    }

    const std::string& RuntimeSceneContext::GetCurrentSceneId() {
        return currentSceneId_;
    }

    void RuntimeSceneContext::MarkCameraReady(
        const World& world) noexcept {
        if (&world == currentWorld_) {
            cameraReady_ = true;
        }
    }

    bool RuntimeSceneContext::IsCameraReady(
        const World* world) noexcept {
        return cameraReady_ && currentWorld_ != nullptr &&
            (world == nullptr || world == currentWorld_);
    }

    void RuntimeSceneContext::SetPendingSceneEntry(
        std::string sceneId,
        std::string spawnPointId,
        uint64_t userData) {
        pendingSceneId_ = std::move(sceneId);
        pendingSpawnPointId_ = std::move(spawnPointId);
        pendingUserData_ = userData;
        resolvedEntry_.reset();
    }

    void RuntimeSceneContext::ClearPendingSceneEntry() {
        pendingSceneId_.clear();
        pendingSpawnPointId_.clear();
        pendingUserData_ = 0u;
    }

    void RuntimeSceneContext::ResolvePendingSceneEntry(const World& world, const std::string& sceneId) {
        if (pendingSceneId_ != sceneId) {
            return;
        }

        ResolvedSceneEntry entry{};
        entry.sceneId = pendingSceneId_;
        entry.spawnPointId = pendingSpawnPointId_;
        entry.userData = pendingUserData_;

        if (pendingSpawnPointId_.empty()) {
            resolvedEntry_ = std::move(entry);
            ClearPendingSceneEntry();
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

            const Transform3D& transform = object->GetTransform();
            MATH::Vec3 position{};
            MATH::Quat rotation = MATH::Quat::Identity();
            MATH::Vec3 scale{};
            if (!MATH::DecomposeTRS(
                    transform.GetWorldMatrix(),
                    position,
                    rotation,
                    scale)) {
                position = transform.position;
                rotation = transform.rotation;
            }
            entry.worldPosition = position;
            entry.worldRotation = rotation;
            entry.hasWorldTransform = true;
            resolvedEntry_ = std::move(entry);
            ClearPendingSceneEntry();
            return;
        }

        // A missing authored spawn point is a recoverable entry failure. The
        // target scene still opens at its own default placement and the
        // GameProject receives an entry token with hasWorldTransform=false.
        resolvedEntry_ = std::move(entry);
        ClearPendingSceneEntry();
    }

    std::optional<ResolvedSceneEntry> RuntimeSceneContext::GetResolvedSceneEntry() {
        return resolvedEntry_;
    }

    std::optional<ResolvedSceneEntry>
        RuntimeSceneContext::ConsumeResolvedSceneEntry(
            const World& world) {
        if (&world != currentWorld_ || !resolvedEntry_.has_value()) {
            return std::nullopt;
        }
        std::optional<ResolvedSceneEntry> result =
            std::move(resolvedEntry_);
        resolvedEntry_.reset();
        return result;
    }

} // namespace HIKARI
