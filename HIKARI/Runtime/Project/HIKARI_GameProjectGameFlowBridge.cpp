#include "HIKARI/Project/WorldView.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <span>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Services/HIKARI_GameProjectSaveStorageService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {

        World* CurrentWorld(void* world) noexcept {
            auto* runtimeWorld = static_cast<World*>(world);
            return runtimeWorld != nullptr &&
                    RuntimeSceneContext::GetCurrentWorld() == runtimeWorld
                ? runtimeWorld
                : nullptr;
        }

        bool CopyUtf8(
            std::string_view source,
            char* destination,
            uint32_t capacity,
            uint32_t& outLength) noexcept {
            outLength = static_cast<uint32_t>((std::min)(
                source.size(),
                static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
            if (destination == nullptr || capacity == 0u ||
                source.size() + 1u > static_cast<size_t>(capacity)) {
                return false;
            }
            if (!source.empty()) {
                std::memcpy(destination, source.data(), source.size());
            }
            destination[source.size()] = '\0';
            return true;
        }

        template<size_t Capacity>
        bool CopyFixedUtf8(
            std::string_view source,
            char (&destination)[Capacity]) noexcept {
            static_assert(Capacity > 0u);
            if (source.size() + 1u > Capacity) {
                return false;
            }
            std::fill(std::begin(destination), std::end(destination), '\0');
            if (!source.empty()) {
                std::memcpy(destination, source.data(), source.size());
            }
            return true;
        }

    } // namespace

    SceneFlowView GameProjectSdkAccess::MakeSceneFlowView(
        World& world) noexcept {
        return SceneFlowView(&world);
    }

    SaveGameView GameProjectSdkAccess::MakeSaveGameView(
        World& world) noexcept {
        return SaveGameView(&world);
    }

    RuntimeSessionView GameProjectSdkAccess::MakeRuntimeSessionView(
        World& world) noexcept {
        return RuntimeSessionView(&world);
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
            return CopyUtf8(
                RuntimeSceneContext::GetCurrentSceneId(),
                outUtf8,
                capacity,
                outLength);
        }
        catch (...) {
            outLength = 0u;
            return false;
        }
    }

    bool SceneFlowView::RequestScene(
        std::string_view sceneAssetGuid,
        std::string_view spawnPointId,
        std::string_view transitionProfileId,
        uint64_t userData,
        uint8_t useTransition) const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr ||
                sceneAssetGuid.empty() ||
                sceneAssetGuid.size() >= kGameProjectSceneGuidCapacity ||
                spawnPointId.size() >= kGameProjectSpawnPointIdCapacity ||
                transitionProfileId.size() > 128u) {
                return false;
            }
            const std::string sceneGuid(sceneAssetGuid);
            if (!IsValidAssetGuid(sceneGuid) ||
                !SERVICES::IsRuntimeSceneGuidAllowed(sceneGuid)) {
                return false;
            }
            SceneTransitionBus* transitionBus =
                RuntimeSceneContext::GetTransitionBus();
            if (transitionBus == nullptr || transitionBus->IsTransitioning()) {
                return false;
            }
            SceneTransitionRequest request{};
            request.targetSceneAssetGuid = sceneGuid;
            request.targetSpawnPointId = std::string(spawnPointId);
            request.transitionProfileId = transitionProfileId.empty()
                ? "noise_wipe"
                : std::string(transitionProfileId);
            request.useTransition = useTransition != 0u;
            request.userData = userData;
            return transitionBus->RequestTransition(request);
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
                !CopyFixedUtf8(entry->sceneId, outEntry.sceneAssetGuid) ||
                !CopyFixedUtf8(entry->spawnPointId, outEntry.spawnPointId)) {
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

    bool SaveGameView::Exists(std::string_view slotId) const noexcept {
        try {
            return CurrentWorld(world_) != nullptr &&
                SERVICES::GetGameProjectSaveStorageService().Exists(slotId);
        }
        catch (...) {
            return false;
        }
    }

    uint64_t SaveGameView::GetSize(std::string_view slotId) const noexcept {
        try {
            return CurrentWorld(world_) != nullptr
                ? static_cast<uint64_t>(
                    SERVICES::GetGameProjectSaveStorageService().Size(slotId))
                : 0u;
        }
        catch (...) {
            return 0u;
        }
    }

    bool SaveGameView::Read(
        std::string_view slotId,
        void* outBytes,
        uint64_t capacity,
        uint64_t& outSize) const noexcept {
        outSize = 0u;
        try {
            if (CurrentWorld(world_) == nullptr ||
                capacity > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
                (capacity != 0u && outBytes == nullptr)) {
                return false;
            }
            size_t bytesRead = 0u;
            const bool result = SERVICES::GetGameProjectSaveStorageService().Read(
                slotId,
                std::span<std::byte>(
                    static_cast<std::byte*>(outBytes),
                    static_cast<size_t>(capacity)),
                bytesRead);
            outSize = static_cast<uint64_t>(bytesRead);
            return result;
        }
        catch (...) {
            outSize = 0u;
            return false;
        }
    }

    bool SaveGameView::WriteAtomic(
        std::string_view slotId,
        const void* bytes,
        uint64_t size) const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr ||
                size > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
                (size != 0u && bytes == nullptr)) {
                return false;
            }
            return SERVICES::GetGameProjectSaveStorageService().Write(
                slotId,
                std::span<const std::byte>(
                    static_cast<const std::byte*>(bytes),
                    static_cast<size_t>(size)));
        }
        catch (...) {
            return false;
        }
    }

    bool SaveGameView::Delete(std::string_view slotId) const noexcept {
        try {
            return CurrentWorld(world_) != nullptr &&
                SERVICES::GetGameProjectSaveStorageService().Delete(slotId);
        }
        catch (...) {
            return false;
        }
    }

    GameProjectRuntimeHostKind
        RuntimeSessionView::GetHostKind() const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr) {
                return GameProjectRuntimeHostKind::Unknown;
            }
            if (SERVICES::IsEditorHost()) {
                return GameProjectRuntimeHostKind::EditorPreview;
            }
            if (SERVICES::IsStandaloneGameHost()) {
                return GameProjectRuntimeHostKind::StandaloneGame;
            }
            return GameProjectRuntimeHostKind::Unknown;
        }
        catch (...) {
            return GameProjectRuntimeHostKind::Unknown;
        }
    }

    bool RuntimeSessionView::RequestEndSession() const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr) {
                return false;
            }
            SERVICES::RequestRuntimeSessionEnd();
            return true;
        }
        catch (...) {
            return false;
        }
    }

} // namespace HIKARI
