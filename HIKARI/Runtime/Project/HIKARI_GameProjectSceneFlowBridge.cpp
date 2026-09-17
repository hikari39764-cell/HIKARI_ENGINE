#include "HIKARI/Project/GameFlowViews.h"

#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectFixedText.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

namespace HIKARI {
    using PROJECT::INTERNAL::CurrentWorld;
    using PROJECT::DETAIL::CopyStringToReference;

    uint64_t SceneFlowView::RequestSceneTracked(
        std::string_view sceneAssetGuid, std::string_view spawnPointId,
        std::string_view transitionProfileId, uint64_t userData,
        uint8_t useTransition, uint8_t holdReveal) const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr || sceneAssetGuid.empty() ||
                sceneAssetGuid.size() >= kGameProjectSceneGuidCapacity ||
                spawnPointId.size() >= kGameProjectSpawnPointIdCapacity ||
                transitionProfileId.size() > 128) return 0;
            auto* bus = RuntimeSceneContext::GetTransitionBus();
            if (bus == nullptr) return 0;
            SceneTransitionRequest request;
            request.targetSceneAssetGuid = sceneAssetGuid;
            request.targetSpawnPointId = spawnPointId;
            request.transitionProfileId = transitionProfileId;
            request.userData = userData;
            request.useTransition = useTransition != 0;
            request.holdTransitionIn = holdReveal != 0 && request.useTransition;
            return bus->RequestTrackedTransition(request);
        } catch (...) {
            return 0;
        }
    }

    bool SceneFlowView::ReadRequestResult(
        uint64_t requestId, GameProjectSceneRequestResult& outResult) const noexcept {
        outResult = {};
        try {
            if (CurrentWorld(world_) == nullptr) return false;
            const auto* bus = RuntimeSceneContext::GetTransitionBus();
            SceneTransitionResult result;
            if (bus == nullptr || !bus->ReadResult(requestId, result)) return false;
            outResult.requestId = result.requestId;
            outResult.userData = result.userData;
            switch (result.outcome) {
            case SceneTransitionOutcome::Pending: outResult.state = GameProjectSceneRequestState::Pending; break;
            case SceneTransitionOutcome::Completed: outResult.state = GameProjectSceneRequestState::Completed; break;
            case SceneTransitionOutcome::Failed: outResult.state = GameProjectSceneRequestState::Failed; break;
            case SceneTransitionOutcome::Cancelled: outResult.state = GameProjectSceneRequestState::Cancelled; break;
            }
            if (!CopyStringToReference(result.targetSceneAssetGuid, outResult.sceneAssetGuid, sizeof(outResult.sceneAssetGuid)) ||
                !CopyStringToReference(result.message, outResult.message, sizeof(outResult.message))) {
                outResult = {};
                return false;
            }
            return true;
        } catch (...) {
            outResult = {};
            return false;
        }
    }

    bool SceneFlowView::CancelRequest(uint64_t requestId) const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr) return false;
            auto* bus = RuntimeSceneContext::GetTransitionBus();
            return bus != nullptr && bus->CancelRequest(requestId);
        } catch (...) {
            return false;
        }
    }

    bool SceneFlowView::GetCurrentSceneAssetGuid(
        char* outUtf8,
        uint32_t capacity,
        uint32_t& outLength) const noexcept {
        outLength = 0u;
        try {
            if (CurrentWorld(world_) == nullptr) {
                return false;
            }
            const auto& sceneId = RuntimeSceneContext::GetCurrentSceneId();
            outLength = static_cast<uint32_t>(sceneId.size());
            return CopyStringToReference(sceneId, outUtf8, capacity);
        }
        catch (...) {
            outLength = 0u;
            return false;
        }
    }

    bool SceneFlowView::IsTransitioning() const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr) {
                return false;
            }
            const SceneTransitionBus* bus =
                RuntimeSceneContext::GetTransitionBus();
            return bus != nullptr && bus->IsTransitioning();
        }
        catch (...) {
            return false;
        }
    }

    bool SceneFlowView::RequestScene(
        std::string_view sceneAssetGuid,
        std::string_view spawnPointId,
        std::string_view transitionProfileId,
        uint64_t userData,
        uint8_t useTransition) const noexcept {
        return RequestSceneTracked(sceneAssetGuid, spawnPointId, transitionProfileId,
            userData, useTransition, 0) != 0;
    }

    bool SceneFlowView::RequestSceneWithHeldReveal(
        std::string_view sceneAssetGuid,
        std::string_view spawnPointId,
        std::string_view transitionProfileId,
        uint64_t userData) const noexcept {
        return RequestSceneTracked(sceneAssetGuid, spawnPointId, transitionProfileId,
            userData, 1, 1) != 0;
    }

    bool SceneFlowView::ReleaseTransitionIn() const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr) {
                return false;
            }
            SceneTransitionBus* transitionBus =
                RuntimeSceneContext::GetTransitionBus();
            return transitionBus != nullptr &&
                transitionBus->ReleaseTransitionIn();
        }
        catch (...) {
            return false;
        }
    }

    bool SceneFlowView::ConsumeSceneEntry(
        GameProjectSceneEntry& outEntry) const noexcept {
        outEntry = {};
        try {
            World* world = CurrentWorld(world_);
            if (world == nullptr) {
                return false;
            }
            std::optional<ResolvedSceneEntry> entry =
                RuntimeSceneContext::ConsumeResolvedSceneEntry(*world);
            if (!entry.has_value() ||
                !CopyStringToReference(entry->sceneId, outEntry.sceneAssetGuid, sizeof(outEntry.sceneAssetGuid)) ||
                !CopyStringToReference(entry->spawnPointId, outEntry.spawnPointId, sizeof(outEntry.spawnPointId))) {
                return false;
            }
            outEntry.userData = entry->userData;
            outEntry.spawnPosition = {
                entry->worldPosition.x,
                entry->worldPosition.y,
                entry->worldPosition.z
            };
            outEntry.spawnRotation = {
                entry->worldRotation.x,
                entry->worldRotation.y,
                entry->worldRotation.z,
                entry->worldRotation.w
            };
            outEntry.hasSpawnTransform = entry->hasWorldTransform ? 1u : 0u;
            return true;
        }
        catch (...) {
            outEntry = {};
            return false;
        }
    }

} // namespace HIKARI
