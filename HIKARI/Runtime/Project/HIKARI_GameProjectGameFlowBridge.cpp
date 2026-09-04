#include "HIKARI/Project/AudioView.h"
#include "HIKARI/Project/GameFlowViews.h"
#include "HIKARI/Project/SceneCaptureView.h"
#include "HIKARI/Project/SequenceView.h"
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
#include <vector>

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
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Scene/Capture/HIKARI_SceneCaptureService.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"

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

        template<size_t Capacity>
        bool ReadFixedUtf8(
            const char (&source)[Capacity],
            std::string& out) {
            static_assert(Capacity > 0u);
            const auto end = std::find(
                std::begin(source),
                std::end(source),
                '\0');
            if (end == std::end(source)) {
                out.clear();
                return false;
            }
            out.assign(source, end);
            return true;
        }

        template<size_t Capacity>
        bool CopyFixedUtf8Truncated(
            std::string_view source,
            char (&destination)[Capacity]) noexcept {
            static_assert(Capacity > 0u);
            const size_t length = (std::min)(
                source.size(),
                Capacity - 1u);
            std::fill(
                std::begin(destination),
                std::end(destination),
                '\0');
            if (length != 0u) {
                std::memcpy(destination, source.data(), length);
            }
            destination[length] = '\0';
            return source.size() >= Capacity;
        }

        World* ResolveSequenceWorld(
            GameProjectWorldHandle worldHandle,
            RuntimeWorldAccessResult* outAccess = nullptr) noexcept {
            RuntimeWorldAccessResult access =
                RuntimeWorldAccessResult::InvalidWorld;
            World* world = World::ResolveRuntimeWorld(
                worldHandle.value,
                &access);
            if (outAccess != nullptr) {
                *outAccess = access;
            }
            if (world == nullptr || !world->IsOwnerThread()) {
                if (world != nullptr && outAccess != nullptr) {
                    *outAccess = RuntimeWorldAccessResult::WrongThread;
                }
                return nullptr;
            }
            if (RuntimeSceneContext::GetCurrentWorld() != world ||
                GameProjectSdkAccess::IsWorldClosing(*world)) {
                if (outAccess != nullptr) {
                    *outAccess = RuntimeWorldAccessResult::InvalidWorld;
                }
                return nullptr;
            }
            return world;
        }

        SEQUENCER::SequencePlaybackService* FindSequencePlaybackService(
            World& world) noexcept {
            if (!world.IsOwnerThread() ||
                GameProjectSdkAccess::IsWorldClosing(world)) {
                return nullptr;
            }
            const RuntimePlayStateService* playState =
                world.Services().Find<RuntimePlayStateService>();
            if (playState == nullptr || !playState->IsActive()) {
                return nullptr;
            }
            return world.Services().Find<
                SEQUENCER::SequencePlaybackService>();
        }

        GameProjectSequencePlaybackHandle ToProjectSequenceHandle(
            SEQUENCER::SequencePlaybackHandle handle) noexcept {
            return { handle.value, handle.epoch };
        }

        SEQUENCER::SequencePlaybackHandle ToRuntimeSequenceHandle(
            GameProjectSequencePlaybackHandle handle) noexcept {
            return { handle.value, handle.epoch };
        }

        bool DecodeSequenceChannelPolicy(
            GameProjectSequenceChannelPolicy policy,
            SEQUENCER::SequenceChannelPolicy& outPolicy) noexcept {
            switch (policy) {
            case GameProjectSequenceChannelPolicy::Parallel:
                outPolicy = SEQUENCER::SequenceChannelPolicy::Parallel;
                return true;
            case GameProjectSequenceChannelPolicy::Replace:
                outPolicy = SEQUENCER::SequenceChannelPolicy::Replace;
                return true;
            case GameProjectSequenceChannelPolicy::ReplaceIfHigherOrEqual:
                outPolicy =
                    SEQUENCER::SequenceChannelPolicy::ReplaceIfHigherOrEqual;
                return true;
            case GameProjectSequenceChannelPolicy::RejectIfOccupied:
                outPolicy =
                    SEQUENCER::SequenceChannelPolicy::RejectIfOccupied;
                return true;
            default:
                return false;
            }
        }

        GameProjectSequencePlaybackState ToProjectSequenceState(
            SEQUENCER::SequencePlaybackState state) noexcept {
            switch (state) {
            case SEQUENCER::SequencePlaybackState::Playing:
                return GameProjectSequencePlaybackState::Playing;
            case SEQUENCER::SequencePlaybackState::Paused:
                return GameProjectSequencePlaybackState::Paused;
            case SEQUENCER::SequencePlaybackState::Stopped:
            default:
                return GameProjectSequencePlaybackState::Stopped;
            }
        }

        GameProjectSequencePlaybackEventKind ToProjectSequenceEventKind(
            SEQUENCER::SequencePlaybackEventKind kind) noexcept {
            switch (kind) {
            case SEQUENCER::SequencePlaybackEventKind::Started:
                return GameProjectSequencePlaybackEventKind::Started;
            case SEQUENCER::SequencePlaybackEventKind::Paused:
                return GameProjectSequencePlaybackEventKind::Paused;
            case SEQUENCER::SequencePlaybackEventKind::Resumed:
                return GameProjectSequencePlaybackEventKind::Resumed;
            case SEQUENCER::SequencePlaybackEventKind::Seeked:
                return GameProjectSequencePlaybackEventKind::Seeked;
            case SEQUENCER::SequencePlaybackEventKind::Completed:
                return GameProjectSequencePlaybackEventKind::Completed;
            case SEQUENCER::SequencePlaybackEventKind::Stopped:
                return GameProjectSequencePlaybackEventKind::Stopped;
            case SEQUENCER::SequencePlaybackEventKind::Rejected:
            default:
                return GameProjectSequencePlaybackEventKind::Rejected;
            }
        }

        GameProjectSequenceStopReason ToProjectSequenceStopReason(
            SEQUENCER::SequenceStopReason reason) noexcept {
            switch (reason) {
            case SEQUENCER::SequenceStopReason::Replaced:
                return GameProjectSequenceStopReason::Replaced;
            case SEQUENCER::SequenceStopReason::Completed:
                return GameProjectSequenceStopReason::Completed;
            case SEQUENCER::SequenceStopReason::Disabled:
                return GameProjectSequenceStopReason::Disabled;
            case SEQUENCER::SequenceStopReason::OwnerDestroyed:
                return GameProjectSequenceStopReason::OwnerDestroyed;
            case SEQUENCER::SequenceStopReason::Reset:
                return GameProjectSequenceStopReason::Reset;
            case SEQUENCER::SequenceStopReason::Stopped:
            default:
                return GameProjectSequenceStopReason::Stopped;
            }
        }

        bool ValidateSequenceOptions(
            const GameProjectSequencePlayOptions& options) noexcept {
            if (options.structSize < kGameProjectSequencePlayOptionsV1Size ||
                options.version != kGameProjectSequencePlaybackVersion ||
                !std::isfinite(options.startTimeSeconds) ||
                !std::isfinite(options.playbackRate) ||
                std::abs(options.playbackRate) < 0.01f ||
                std::abs(options.playbackRate) > 8.0f ||
                options.loop > 1u ||
                options.holdAtEnd > 1u ||
                options.bindingCount > kGameProjectSequenceMaximumBindings ||
                (options.bindingCount != 0u && options.bindings == nullptr) ||
                options.reserved0 != 0u) {
                return false;
            }
            for (uint32_t value : options.reserved) {
                if (value != 0u) {
                    return false;
                }
            }
            SEQUENCER::SequenceChannelPolicy runtimePolicy{};
            return DecodeSequenceChannelPolicy(
                options.channelPolicy,
                runtimePolicy);
        }

        bool BuildSequencePlayRequest(
            World& world,
            std::string_view assetGuid,
            const GameProjectSequencePlayOptions& options,
            SEQUENCER::SequencePlayRequest& outRequest) {
            outRequest = {};
            if (!IsValidAssetGuid(assetGuid) ||
                assetGuid.find('\0') != std::string_view::npos ||
                !ValidateSequenceOptions(options)) {
                return false;
            }

            std::string channel{};
            if (!ReadFixedUtf8(options.channel, channel)) {
                return false;
            }
            outRequest.assetGuid = AssetGuid{ std::string(assetGuid) };
            outRequest.options.loop = options.loop != 0u;
            outRequest.options.holdAtEnd = options.holdAtEnd != 0u;
            outRequest.options.playbackRate = options.playbackRate;
            outRequest.startTimeSeconds = options.startTimeSeconds;
            outRequest.channel = std::move(channel);
            outRequest.priority = options.priority;
            if (!DecodeSequenceChannelPolicy(
                    options.channelPolicy,
                    outRequest.channelPolicy)) {
                outRequest = {};
                return false;
            }

            for (uint32_t index = 0u;
                index < options.bindingCount;
                ++index) {
                const GameProjectSequenceBinding& binding =
                    options.bindings[index];
                std::string slotName{};
                if (!ReadFixedUtf8(binding.slotName, slotName) ||
                    slotName.empty() ||
                    !PROJECT::INTERNAL::IsObjectHandleForWorld(
                        binding.object,
                        world)) {
                    outRequest = {};
                    return false;
                }
                for (uint32_t prior = 0u; prior < index; ++prior) {
                    std::string priorSlotName{};
                    if (!ReadFixedUtf8(
                            options.bindings[prior].slotName,
                            priorSlotName) ||
                        priorSlotName == slotName) {
                        outRequest = {};
                        return false;
                    }
                }
                GameObject* object = world.FindObject(
                    PROJECT::INTERNAL::DecodeRuntimeHandle(binding.object));
                if (object == nullptr || object->GetDocumentId().value == 0u ||
                    !outRequest.bindings.BindSlot(
                        std::move(slotName),
                        object->GetDocumentId())) {
                    outRequest = {};
                    return false;
                }
            }
            return true;
        }

        bool FillProjectSequenceSnapshot(
            const SEQUENCER::SequencePlaybackSnapshot& source,
            GameProjectSequencePlaybackSnapshot& destination) noexcept {
            destination = {};
            destination.handle = ToProjectSequenceHandle(source.handle);
            destination.state = ToProjectSequenceState(source.state);
            destination.timeSeconds = source.timeSeconds;
            destination.durationSeconds = source.durationSeconds;
            destination.priority = source.priority;
            return CopyFixedUtf8(
                    source.assetGuid.value,
                    destination.assetGuid) &&
                CopyFixedUtf8(
                    source.channel,
                    destination.channel);
        }

        void FillProjectSequenceEvent(
            const SEQUENCER::SequencePlaybackEvent& source,
            GameProjectSequencePlaybackEvent& destination) noexcept {
            destination = {};
            destination.handle = ToProjectSequenceHandle(source.handle);
            destination.requestId = source.requestId;
            destination.timeSeconds = source.timeSeconds;
            destination.kind = ToProjectSequenceEventKind(source.kind);
            destination.stopReason = ToProjectSequenceStopReason(
                source.stopReason);
            (void)CopyFixedUtf8Truncated(
                std::string_view(source.assetGuid.value),
                destination.assetGuid);
            destination.messageTruncated =
                CopyFixedUtf8Truncated(
                    std::string_view(source.message),
                    destination.message)
                ? 1u
                : 0u;
        }

        template<size_t Count>
        bool HasNonZeroSceneCaptureReserved(
            const uint32_t (&values)[Count]) noexcept {
            for (uint32_t value : values) {
                if (value != 0u) {
                    return true;
                }
            }
            return false;
        }

        World* ResolveSceneCaptureWorld(
            GameProjectWorldHandle worldHandle) noexcept {
            RuntimeWorldAccessResult access =
                RuntimeWorldAccessResult::InvalidWorld;
            World* world = World::ResolveRuntimeWorld(
                worldHandle.value,
                &access);
            return world != nullptr && world->IsOwnerThread() &&
                    RuntimeSceneContext::GetCurrentWorld() == world &&
                    !GameProjectSdkAccess::IsWorldClosing(*world)
                ? world
                : nullptr;
        }

        bool ValidateSceneCaptureDesc(
            const GameProjectSceneCaptureDesc& desc) noexcept {
            constexpr uint32_t knownFlags =
                GameProjectSceneCaptureFlags(
                    GameProjectSceneCaptureFlag::Enabled) |
                GameProjectSceneCaptureFlags(
                    GameProjectSceneCaptureFlag::RenderSky) |
                GameProjectSceneCaptureFlags(
                    GameProjectSceneCaptureFlag::RenderTransparent) |
                GameProjectSceneCaptureFlags(
                    GameProjectSceneCaptureFlag::RenderVfx);
            return desc.structSize ==
                    kGameProjectSceneCaptureDescV1Size &&
                desc.version == kGameProjectSceneCaptureVersion &&
                !HasNonZeroSceneCaptureReserved(desc.reserved) &&
                desc.cameraObject.IsValid() &&
                desc.width >= kGameProjectSceneCaptureMinimumExtent &&
                desc.width <= kGameProjectSceneCaptureMaximumExtent &&
                desc.height >= kGameProjectSceneCaptureMinimumExtent &&
                desc.height <= kGameProjectSceneCaptureMaximumExtent &&
                (desc.flags & ~knownFlags) == 0u;
        }

        bool ValidateSceneCaptureSourceRect(
            const GameProjectSceneCaptureSourceRect& rect) noexcept {
            if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
                !std::isfinite(rect.width) ||
                !std::isfinite(rect.height) ||
                rect.x < 0.0f || rect.y < 0.0f ||
                rect.width <= 0.0f || rect.height <= 0.0f) {
                return false;
            }
            constexpr float boundaryTolerance = 0.00001f;
            return rect.x + rect.width <= 1.0f + boundaryTolerance &&
                rect.y + rect.height <= 1.0f + boundaryTolerance;
        }

        RuntimeObjectHandle ResolveSceneCaptureObject(
            World& world,
            GameProjectObjectHandle handle) noexcept {
            return PROJECT::INTERNAL::IsObjectHandleForWorld(
                    handle,
                    world)
                ? PROJECT::INTERNAL::DecodeRuntimeHandle(handle)
                : RuntimeObjectHandle{};
        }

    } // namespace

    SceneFlowView GameProjectSdkAccess::MakeSceneFlowView(
        World& world) noexcept {
        return SceneFlowView(&world);
    }

    SceneCaptureView GameProjectSdkAccess::MakeSceneCaptureView(
        World& world) noexcept {
        return SceneCaptureView(GameProjectWorldHandle{
            world.GetRuntimeIdentity()
        });
    }

    SequenceView GameProjectSdkAccess::MakeSequenceView(
        World& world) noexcept {
        return SequenceView(GameProjectWorldHandle{
            world.GetRuntimeIdentity()
        });
    }

    DialogueView GameProjectSdkAccess::MakeDialogueView(
        World& world) noexcept {
        return DialogueView(&world);
    }

    AudioView GameProjectSdkAccess::MakeAudioView(
        World& world) noexcept {
        return AudioView(&world);
    }

    bool SequenceView::IsValid() const noexcept {
        try {
            return ResolveSequenceWorld(world_) != nullptr;
        }
        catch (...) {
            return false;
        }
    }

    GameProjectSequencePlaybackHandle SequenceView::Play(
        std::string_view assetGuid,
        const GameProjectSequencePlayOptions& options) const noexcept {
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            if (service == nullptr) {
                return {};
            }
            SEQUENCER::SequencePlayRequest request{};
            if (!BuildSequencePlayRequest(
                    *world,
                    assetGuid,
                    options,
                    request)) {
                return {};
            }
            return ToProjectSequenceHandle(
                service->PlayDetailed(request).handle);
        }
        catch (...) {
            return {};
        }
    }

    bool SequenceView::Stop(
        GameProjectSequencePlaybackHandle handle) const noexcept {
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            return service != nullptr && handle.IsValid() &&
                service->Stop(ToRuntimeSequenceHandle(handle));
        }
        catch (...) {
            return false;
        }
    }

    bool SequenceView::Pause(
        GameProjectSequencePlaybackHandle handle) const noexcept {
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            return service != nullptr && handle.IsValid() &&
                service->Pause(ToRuntimeSequenceHandle(handle));
        }
        catch (...) {
            return false;
        }
    }

    bool SequenceView::Resume(
        GameProjectSequencePlaybackHandle handle) const noexcept {
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            return service != nullptr && handle.IsValid() &&
                service->Resume(ToRuntimeSequenceHandle(handle));
        }
        catch (...) {
            return false;
        }
    }

    bool SequenceView::SetPlaybackRate(
        GameProjectSequencePlaybackHandle handle,
        float playbackRate) const noexcept {
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            return service != nullptr && handle.IsValid() &&
                std::isfinite(playbackRate) &&
                std::abs(playbackRate) >= 0.01f &&
                std::abs(playbackRate) <= 8.0f &&
                service->SetPlaybackRate(
                    ToRuntimeSequenceHandle(handle),
                    playbackRate);
        }
        catch (...) {
            return false;
        }
    }

    bool SequenceView::Seek(
        GameProjectSequencePlaybackHandle handle,
        float timeSeconds) const noexcept {
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            return service != nullptr && handle.IsValid() &&
                std::isfinite(timeSeconds) &&
                service->Seek(
                    ToRuntimeSequenceHandle(handle),
                    timeSeconds);
        }
        catch (...) {
            return false;
        }
    }

    bool SequenceView::IsActive(
        GameProjectSequencePlaybackHandle handle) const noexcept {
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            return service != nullptr && handle.IsValid() &&
                service->IsActive(ToRuntimeSequenceHandle(handle));
        }
        catch (...) {
            return false;
        }
    }

    bool SequenceView::IsPlaying(
        GameProjectSequencePlaybackHandle handle) const noexcept {
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            return service != nullptr && handle.IsValid() &&
                service->IsPlaying(ToRuntimeSequenceHandle(handle));
        }
        catch (...) {
            return false;
        }
    }

    bool SequenceView::IsPaused(
        GameProjectSequencePlaybackHandle handle) const noexcept {
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            return service != nullptr && handle.IsValid() &&
                service->IsPaused(ToRuntimeSequenceHandle(handle));
        }
        catch (...) {
            return false;
        }
    }

    bool SequenceView::GetSnapshot(
        GameProjectSequencePlaybackHandle handle,
        GameProjectSequencePlaybackSnapshot& outSnapshot) const noexcept {
        outSnapshot = {};
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            if (service == nullptr || !handle.IsValid()) {
                return false;
            }
            SEQUENCER::SequencePlaybackSnapshot snapshot{};
            if (!service->TryGetSnapshot(
                    ToRuntimeSequenceHandle(handle),
                    snapshot)) {
                return false;
            }
            return FillProjectSequenceSnapshot(snapshot, outSnapshot);
        }
        catch (...) {
            outSnapshot = {};
            return false;
        }
    }

    uint32_t SequenceView::ConsumeEvents(
        void* context,
        GameProjectSequencePlaybackEventCallback callback) const noexcept {
        if (callback == nullptr) {
            return 0u;
        }
        try {
            World* world = ResolveSequenceWorld(world_);
            SEQUENCER::SequencePlaybackService* service = world != nullptr
                ? FindSequencePlaybackService(*world)
                : nullptr;
            if (service == nullptr) {
                return 0u;
            }
            const std::vector<SEQUENCER::SequencePlaybackEvent> events =
                service->ConsumeEvents();
            uint32_t delivered = 0u;
            for (const SEQUENCER::SequencePlaybackEvent& source : events) {
                GameProjectSequencePlaybackEvent event{};
                FillProjectSequenceEvent(source, event);
                callback(context, &event);
                if (delivered != (std::numeric_limits<uint32_t>::max)()) {
                    ++delivered;
                }
            }
            return delivered;
        }
        catch (...) {
            return 0u;
        }
    }

    bool SceneCaptureView::IsValid() const noexcept {
        try {
            return ResolveSceneCaptureWorld(world_) != nullptr;
        }
        catch (...) {
            return false;
        }
    }

    GameProjectSceneCaptureHandle SceneCaptureView::Create(
        const GameProjectSceneCaptureDesc& desc) const noexcept {
        try {
            World* world = ResolveSceneCaptureWorld(world_);
            if (world == nullptr || !ValidateSceneCaptureDesc(desc)) {
                return {};
            }
            const RuntimeObjectHandle camera =
                ResolveSceneCaptureObject(*world, desc.cameraObject);
            GameObject* cameraObject = world->FindObject(camera);
            if (cameraObject == nullptr ||
                cameraObject->GetComponent<CameraComponent>() == nullptr) {
                return {};
            }
            SCENE_CAPTURE::SceneCaptureService* service =
                world->Services().Find<
                    SCENE_CAPTURE::SceneCaptureService>();
            return service != nullptr
                ? service->Create(camera, desc)
                : GameProjectSceneCaptureHandle{};
        }
        catch (...) {
            return {};
        }
    }

    bool SceneCaptureView::Update(
        GameProjectSceneCaptureHandle capture,
        const GameProjectSceneCaptureDesc& desc) const noexcept {
        try {
            World* world = ResolveSceneCaptureWorld(world_);
            if (world == nullptr || !capture.IsValid() ||
                !ValidateSceneCaptureDesc(desc)) {
                return false;
            }
            const RuntimeObjectHandle camera =
                ResolveSceneCaptureObject(*world, desc.cameraObject);
            GameObject* cameraObject = world->FindObject(camera);
            SCENE_CAPTURE::SceneCaptureService* service =
                world->Services().Find<
                    SCENE_CAPTURE::SceneCaptureService>();
            return service != nullptr && cameraObject != nullptr &&
                cameraObject->GetComponent<CameraComponent>() != nullptr &&
                service->Update(capture, camera, desc);
        }
        catch (...) {
            return false;
        }
    }

    bool SceneCaptureView::Destroy(
        GameProjectSceneCaptureHandle capture) const noexcept {
        try {
            World* world = ResolveSceneCaptureWorld(world_);
            SCENE_CAPTURE::SceneCaptureService* service = world != nullptr
                ? world->Services().Find<
                    SCENE_CAPTURE::SceneCaptureService>()
                : nullptr;
            return service != nullptr && capture.IsValid() &&
                service->Destroy(capture);
        }
        catch (...) {
            return false;
        }
    }

    bool SceneCaptureView::BindMaterial(
        const GameProjectSceneCaptureMaterialBindingDesc& binding)
        const noexcept {
        try {
            World* world = ResolveSceneCaptureWorld(world_);
            if (world == nullptr ||
                binding.structSize !=
                    kGameProjectSceneCaptureMaterialBindingDescV1Size ||
                binding.version != kGameProjectSceneCaptureVersion ||
                !binding.capture.IsValid() ||
                !binding.modelObject.IsValid() ||
                HasNonZeroSceneCaptureReserved(binding.reserved) ||
                (binding.flags &
                    ~kGameProjectSceneCaptureMaterialBindingKnownFlags) !=
                        0u) {
                return false;
            }
            std::string materialSlotName{};
            if (!ReadFixedUtf8(
                    binding.materialSlotName,
                    materialSlotName) ||
                (materialSlotName.empty() &&
                 binding.fallbackMaterialIndex ==
                    kGameProjectInvalidMaterialIndex)) {
                return false;
            }
            const RuntimeObjectHandle modelHandle =
                ResolveSceneCaptureObject(*world, binding.modelObject);
            GameObject* modelObject = world->FindObject(modelHandle);
            SCENE_CAPTURE::SceneCaptureService* service =
                world->Services().Find<
                    SCENE_CAPTURE::SceneCaptureService>();
            return service != nullptr && modelObject != nullptr &&
                modelObject->GetComponent<ModelComponent>() != nullptr &&
                service->BindMaterial({
                    binding.capture,
                    modelHandle,
                    std::move(materialSlotName),
                    binding.fallbackMaterialIndex,
                    binding.flags,
                });
        }
        catch (...) {
            return false;
        }
    }

    bool SceneCaptureView::BindMaterialRegion(
        const GameProjectSceneCaptureMaterialRegionBindingDesc& binding)
        const noexcept {
        try {
            World* world = ResolveSceneCaptureWorld(world_);
            if (world == nullptr ||
                binding.structSize !=
                    kGameProjectSceneCaptureMaterialRegionBindingDescV1Size ||
                binding.version !=
                    kGameProjectSceneCaptureMaterialRegionBindingVersion ||
                !binding.capture.IsValid() ||
                !binding.modelObject.IsValid() ||
                HasNonZeroSceneCaptureReserved(binding.reserved) ||
                (binding.flags &
                    ~kGameProjectSceneCaptureMaterialBindingKnownFlags) !=
                        0u ||
                binding.quarterTurns >= 4u ||
                !ValidateSceneCaptureSourceRect(binding.sourceRect)) {
                return false;
            }
            std::string materialSlotName{};
            if (!ReadFixedUtf8(
                    binding.materialSlotName,
                    materialSlotName) ||
                (materialSlotName.empty() &&
                 binding.fallbackMaterialIndex ==
                    kGameProjectInvalidMaterialIndex)) {
                return false;
            }
            const RuntimeObjectHandle modelHandle =
                ResolveSceneCaptureObject(*world, binding.modelObject);
            GameObject* modelObject = world->FindObject(modelHandle);
            SCENE_CAPTURE::SceneCaptureService* service =
                world->Services().Find<
                    SCENE_CAPTURE::SceneCaptureService>();
            return service != nullptr && modelObject != nullptr &&
                modelObject->GetComponent<ModelComponent>() != nullptr &&
                service->BindMaterial({
                    binding.capture,
                    modelHandle,
                    std::move(materialSlotName),
                    binding.fallbackMaterialIndex,
                    binding.flags,
                    binding.sourceRect,
                    binding.quarterTurns,
                });
        }
        catch (...) {
            return false;
        }
    }

    bool SceneCaptureView::ClearMaterialBinding(
        GameProjectSceneCaptureHandle capture,
        GameProjectObjectHandle modelObject) const noexcept {
        try {
            World* world = ResolveSceneCaptureWorld(world_);
            if (world == nullptr || !capture.IsValid()) {
                return false;
            }
            const RuntimeObjectHandle model =
                ResolveSceneCaptureObject(*world, modelObject);
            SCENE_CAPTURE::SceneCaptureService* service =
                world->Services().Find<
                    SCENE_CAPTURE::SceneCaptureService>();
            return service != nullptr && model.IsValid() &&
                service->ClearMaterialBinding(capture, model);
        }
        catch (...) {
            return false;
        }
    }

    bool SceneCaptureView::GetSnapshot(
        GameProjectSceneCaptureHandle capture,
        GameProjectSceneCaptureSnapshot& outSnapshot) const noexcept {
        outSnapshot = {};
        try {
            World* world = ResolveSceneCaptureWorld(world_);
            SCENE_CAPTURE::SceneCaptureService* service = world != nullptr
                ? world->Services().Find<
                    SCENE_CAPTURE::SceneCaptureService>()
                : nullptr;
            return service != nullptr && capture.IsValid() &&
                service->TryGetSnapshot(capture, outSnapshot);
        }
        catch (...) {
            outSnapshot = {};
            return false;
        }
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

    AudioView WorldView::GetAudio() const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? GameProjectSdkAccess::MakeAudioView(*world)
            : AudioView{};
    }

    SequenceView WorldView::GetSequence() const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? GameProjectSdkAccess::MakeSequenceView(*world)
            : SequenceView{};
    }

    SceneCaptureView WorldView::GetSceneCapture() const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? GameProjectSdkAccess::MakeSceneCaptureView(*world)
            : SceneCaptureView{};
    }

    SceneFlowView WorldView::GetSceneFlow() const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? GameProjectSdkAccess::MakeSceneFlowView(*world)
            : SceneFlowView{};
    }

    DialogueView WorldView::GetDialogue() const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? GameProjectSdkAccess::MakeDialogueView(*world)
            : DialogueView{};
    }

    SaveGameView WorldView::GetSaveGame() const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? GameProjectSdkAccess::MakeSaveGameView(*world)
            : SaveGameView{};
    }

    RuntimeSessionView WorldView::GetRuntimeSession() const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? GameProjectSdkAccess::MakeRuntimeSessionView(*world)
            : RuntimeSessionView{};
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
            SceneTransitionBus* transitionBus =
                RuntimeSceneContext::GetTransitionBus();
            if (transitionBus == nullptr) {
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
            SceneTransitionBus* transitionBus =
                RuntimeSceneContext::GetTransitionBus();
            if (transitionBus == nullptr) {
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

    bool RuntimeSessionView::IsPlayActive() const noexcept {
        try {
            World* world = CurrentWorld(world_);
            const RuntimePlayStateService* playState =
                world != nullptr
                    ? world->Services().Find<RuntimePlayStateService>()
                    : nullptr;
            return playState != nullptr && playState->IsActive();
        }
        catch (...) {
            return false;
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
