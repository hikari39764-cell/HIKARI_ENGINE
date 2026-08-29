#include "HIKARI/Project/WorldView.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <system_error>

#include "Assets/HIKARI_AssetGuid.h"
#include "Audio/HIKARI_Audio.h"
#include "Core/HIKARI_Logger.h"
#include "Gameplay/Dialogue/HIKARI_DialogueSessionService.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Services/HIKARI_GameProjectSaveStorageService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"
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

        RuntimeObjectHandle DecodeRuntimeHandle(uint64_t value) noexcept {
            if (value == 0u) {
                return {};
            }
            const uint32_t encodedSlot = static_cast<uint32_t>(
                value & 0xFFFFFFFFull);
            const uint32_t generation = static_cast<uint32_t>(
                value >> 32u);
            if (encodedSlot == 0u || generation == 0u) {
                return {};
            }
            RuntimeObjectHandle result{};
            result.slot = encodedSlot - 1u;
            result.generation = generation;
            return result.IsValid() ? result : RuntimeObjectHandle{};
        }

        AUDIO::AudioBus DecodeAudioBus(
            GameProjectAudioBus bus) noexcept {
            switch (bus) {
            case GameProjectAudioBus::Bgm:
                return AUDIO::AudioBus::Bgm;
            case GameProjectAudioBus::Voice:
                return AUDIO::AudioBus::Voice;
            case GameProjectAudioBus::Se:
            default:
                return AUDIO::AudioBus::Se;
            }
        }

        int DecodeAudioHandle(uint64_t value) noexcept {
            constexpr uint64_t kMaximumEncoded =
                static_cast<uint64_t>((std::numeric_limits<int>::max)()) +
                1u;
            return value == 0u || value > kMaximumEncoded
                ? -1
                : static_cast<int>(value - 1u);
        }

        uint64_t EncodeAudioHandle(int value) noexcept {
            return value < 0
                ? 0u
                : static_cast<uint64_t>(value) + 1u;
        }

        bool ResolveProjectAudioPath(
            std::string_view projectRelativePath,
            std::filesystem::path& outPath) {
            outPath.clear();
            if (projectRelativePath.empty() ||
                projectRelativePath.size() > 4096u ||
                projectRelativePath.find('\0') != std::string_view::npos) {
                return false;
            }
            const std::filesystem::path relative{
                std::string(projectRelativePath) };
            if (relative.empty() || relative.is_absolute() ||
                relative.has_root_name() || relative.has_root_directory()) {
                return false;
            }
            for (const std::filesystem::path& part : relative) {
                if (part == "..") {
                    return false;
                }
            }
            const ProjectContext& project = GetProjectContext();
            if (!project.IsOpen()) {
                return false;
            }
            const std::filesystem::path root =
                project.GetRoot().lexically_normal();
            const std::filesystem::path candidates[]{
                relative,
                std::filesystem::path("RuntimePayload") / relative,
            };
            for (const std::filesystem::path& candidate : candidates) {
                const std::filesystem::path resolved =
                    project.Resolve(candidate).lexically_normal();
                const std::filesystem::path check =
                    resolved.lexically_relative(root);
                if (check.empty() || *check.begin() == "..") {
                    continue;
                }
                std::error_code ec{};
                if (!std::filesystem::is_regular_file(resolved, ec) || ec) {
                    continue;
                }
                outPath = resolved;
                return true;
            }
            return false;
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

    DialogueView GameProjectSdkAccess::MakeDialogueView(
        World& world) noexcept {
        return DialogueView(&world);
    }

    AudioView GameProjectSdkAccess::MakeAudioView(
        World& world) noexcept {
        return AudioView(&world);
    }

    bool AudioView::SetBusVolume(
        GameProjectAudioBus bus,
        float linearVolume) const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr ||
                !std::isfinite(linearVolume)) {
                return false;
            }
            AUDIO::SetBusVolume(
                DecodeAudioBus(bus),
                linearVolume);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    float AudioView::GetBusVolume(
        GameProjectAudioBus bus) const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr) {
                return 0.0f;
            }
            return AUDIO::GetBusVolume(
                DecodeAudioBus(bus));
        }
        catch (...) {
            return 0.0f;
        }
    }

    GameProjectAudioSoundHandle AudioView::Load(
        std::string_view projectRelativePath) const noexcept {
        try {
            if (CurrentWorld(world_) == nullptr) {
                return {};
            }
            std::filesystem::path resolved{};
            if (!ResolveProjectAudioPath(projectRelativePath, resolved)) {
                return {};
            }
            const std::u8string utf8Path = resolved.u8string();
            const std::string encodedPath(
                reinterpret_cast<const char*>(utf8Path.data()),
                utf8Path.size());
            return { EncodeAudioHandle(AUDIO::Load(
                encodedPath.c_str())) };
        }
        catch (...) {
            return {};
        }
    }

    GameProjectAudioPlaybackHandle AudioView::Play(
        GameProjectAudioSoundHandle sound,
        GameProjectAudioBus bus,
        uint8_t loop,
        float linearVolume) const noexcept {
        try {
            const int decoded = DecodeAudioHandle(sound.value);
            if (CurrentWorld(world_) == nullptr || decoded < 0 ||
                !std::isfinite(linearVolume)) {
                return {};
            }
            return { EncodeAudioHandle(AUDIO::PlayOnBus(
                decoded,
                loop != 0u,
                linearVolume,
                DecodeAudioBus(bus))) };
        }
        catch (...) {
            return {};
        }
    }

    bool AudioView::Stop(
        GameProjectAudioPlaybackHandle playback) const noexcept {
        try {
            const int decoded = DecodeAudioHandle(playback.value);
            if (CurrentWorld(world_) == nullptr || decoded < 0) {
                return false;
            }
            AUDIO::Stop(decoded);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AudioView::Pause(
        GameProjectAudioPlaybackHandle playback) const noexcept {
        try {
            const int decoded = DecodeAudioHandle(playback.value);
            if (CurrentWorld(world_) == nullptr || decoded < 0 ||
                !AUDIO::IsPlaying(decoded)) {
                return false;
            }
            AUDIO::Pause(decoded);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AudioView::Resume(
        GameProjectAudioPlaybackHandle playback) const noexcept {
        try {
            const int decoded = DecodeAudioHandle(playback.value);
            if (CurrentWorld(world_) == nullptr || decoded < 0 ||
                !AUDIO::IsPlaying(decoded)) {
                return false;
            }
            AUDIO::Resume(decoded);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AudioView::SetVolume(
        GameProjectAudioPlaybackHandle playback,
        float linearVolume) const noexcept {
        try {
            const int decoded = DecodeAudioHandle(playback.value);
            if (CurrentWorld(world_) == nullptr || decoded < 0 ||
                !std::isfinite(linearVolume) ||
                !AUDIO::IsPlaying(decoded)) {
                return false;
            }
            AUDIO::SetVolume(decoded, linearVolume);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AudioView::IsPlaying(
        GameProjectAudioPlaybackHandle playback) const noexcept {
        try {
            const int decoded = DecodeAudioHandle(playback.value);
            return CurrentWorld(world_) != nullptr && decoded >= 0 &&
                AUDIO::IsPlaying(decoded);
        }
        catch (...) {
            return false;
        }
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

    bool DialogueView::Start(
        std::string_view assetGuid,
        uint64_t actorObjectHandle,
        std::string_view locale,
        uint8_t pauseWorld) const noexcept {
        try {
            World* world = CurrentWorld(world_);
            if (world == nullptr || !IsValidAssetGuid(assetGuid) ||
                actorObjectHandle == 0u || locale.size() > 32u) {
                HIKARI_LOG_WARN(
                    "[GameProject][Dialogue] Start rejected: invalid "
                    "world, asset GUID, actor, or locale.");
                return false;
            }
            DIALOGUE::DialogueSessionService* service =
                world->Services().Find<
                    DIALOGUE::DialogueSessionService>();
            const RuntimeObjectHandle actor = DecodeRuntimeHandle(
                actorObjectHandle);
            if (service == nullptr) {
                HIKARI_LOG_WARN(
                    "[GameProject][Dialogue] Start rejected: session "
                    "service is unavailable.");
                return false;
            }
            if (service->IsActive()) {
                HIKARI_LOG_WARN(
                    "[GameProject][Dialogue] Start rejected: another "
                    "session is active.");
                return false;
            }
            if (world->FindObject(actor) == nullptr) {
                HIKARI_LOG_WARN(
                    "[GameProject][Dialogue] Start rejected: actor "
                    "handle is not present in the current World.");
                return false;
            }
            const bool started = service->Start(
                *world,
                AssetGuid{ std::string(assetGuid) },
                std::string(locale),
                actor,
                actor,
                pauseWorld != 0u);
            if (!started) {
                HIKARI_LOG_WARN(
                    "[GameProject][Dialogue] Session rejected asset " +
                    std::string(assetGuid) + ".");
            }
            else {
                HIKARI_LOG_INFO(
                    "[GameProject][Dialogue] Session started asset " +
                    std::string(assetGuid) + ".");
            }
            return started;
        }
        catch (...) {
            return false;
        }
    }

    bool DialogueView::IsActive() const noexcept {
        try {
            World* world = CurrentWorld(world_);
            DIALOGUE::DialogueSessionService* service = world != nullptr
                ? world->Services().Find<
                    DIALOGUE::DialogueSessionService>()
                : nullptr;
            return service != nullptr && service->IsActive();
        }
        catch (...) {
            return false;
        }
    }

    bool DialogueView::Stop() const noexcept {
        try {
            World* world = CurrentWorld(world_);
            DIALOGUE::DialogueSessionService* service = world != nullptr
                ? world->Services().Find<
                    DIALOGUE::DialogueSessionService>()
                : nullptr;
            if (service == nullptr || !service->IsActive()) {
                return false;
            }
            service->Stop(*world, "game-project-stop");
            return true;
        }
        catch (...) {
            return false;
        }
    }

    uint32_t DialogueView::ConsumeOneShotTriggers() const noexcept {
        try {
            World* world = CurrentWorld(world_);
            if (world == nullptr) {
                return 0u;
            }
            uint32_t consumed = 0u;
            world->ForEachObjectWith<DialogueTriggerComponent>(
                [&consumed](GameObject&,
                    DialogueTriggerComponent& trigger) {
                    if (trigger.IsEnabled() && trigger.IsOneShot() &&
                        !trigger.IsConsumed()) {
                        trigger.MarkConsumed();
                        ++consumed;
                    }
                });
            return consumed;
        }
        catch (...) {
            return 0u;
        }
    }

    uint64_t DialogueView::CaptureOneShotTriggerMask() const noexcept {
        try {
            World* world = CurrentWorld(world_);
            if (world == nullptr) {
                return 0u;
            }
            struct Entry final {
                uint64_t handle = 0u;
                DialogueTriggerComponent* trigger = nullptr;
            };
            std::array<Entry, 64u> entries{};
            size_t count = 0u;
            world->ForEachObjectWith<DialogueTriggerComponent>(
                [&](GameObject& object,
                    DialogueTriggerComponent& trigger) {
                    if (count < entries.size() && trigger.IsEnabled() &&
                        trigger.IsOneShot()) {
                        entries[count++] = {
                            object.GetRuntimeHandle().ToValue(),
                            &trigger };
                    }
                });
            std::sort(
                entries.begin(),
                entries.begin() + static_cast<std::ptrdiff_t>(count),
                [](const Entry& lhs, const Entry& rhs) {
                    return lhs.handle < rhs.handle;
                });
            uint64_t mask = 0u;
            for (size_t index = 0u; index < count; ++index) {
                if (entries[index].trigger != nullptr &&
                    entries[index].trigger->IsConsumed()) {
                    mask |= uint64_t{ 1u } << index;
                }
            }
            return mask;
        }
        catch (...) {
            return 0u;
        }
    }

    uint32_t DialogueView::RestoreOneShotTriggerMask(
        uint64_t completedMask) const noexcept {
        try {
            World* world = CurrentWorld(world_);
            if (world == nullptr) {
                return 0u;
            }
            struct Entry final {
                uint64_t handle = 0u;
                DialogueTriggerComponent* trigger = nullptr;
            };
            std::array<Entry, 64u> entries{};
            size_t count = 0u;
            world->ForEachObjectWith<DialogueTriggerComponent>(
                [&](GameObject& object,
                    DialogueTriggerComponent& trigger) {
                    if (count < entries.size() && trigger.IsEnabled() &&
                        trigger.IsOneShot()) {
                        entries[count++] = {
                            object.GetRuntimeHandle().ToValue(),
                            &trigger };
                    }
                });
            std::sort(
                entries.begin(),
                entries.begin() + static_cast<std::ptrdiff_t>(count),
                [](const Entry& lhs, const Entry& rhs) {
                    return lhs.handle < rhs.handle;
                });
            uint32_t restored = 0u;
            for (size_t index = 0u; index < count; ++index) {
                const bool consumed =
                    (completedMask & (uint64_t{ 1u } << index)) != 0u;
                if (entries[index].trigger != nullptr) {
                    entries[index].trigger->SetConsumed(consumed);
                    restored += consumed ? 1u : 0u;
                }
            }
            return restored;
        }
        catch (...) {
            return 0u;
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

    bool SceneFlowView::RequestSceneWithHeldReveal(
        std::string_view sceneAssetGuid,
        std::string_view spawnPointId,
        std::string_view transitionProfileId,
        uint64_t userData) const noexcept {
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
            request.useTransition = true;
            request.holdTransitionIn = true;
            request.userData = userData;
            return transitionBus->RequestTransition(request);
        }
        catch (...) {
            return false;
        }
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
