#include "HIKARI/Project/SequenceView.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectServiceBridgeInternal.h"

namespace HIKARI {
namespace {
using PROJECT::INTERNAL::ReadFixedUtf8;

template <size_t Capacity>
bool CopyFixedUtf8(std::string_view source, char (&destination)[Capacity]) noexcept {
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

template <size_t Capacity>
bool CopyFixedUtf8Truncated(std::string_view source, char (&destination)[Capacity]) noexcept {
    static_assert(Capacity > 0u);
    const size_t length = (std::min)(source.size(), Capacity - 1u);
    std::fill(std::begin(destination), std::end(destination), '\0');
    if (length != 0u) {
        std::memcpy(destination, source.data(), length);
    }
    destination[length] = '\0';
    return source.size() >= Capacity;
}

SEQUENCER::SequencePlaybackService* FindSequencePlaybackService(World& world) noexcept {
    const RuntimePlayStateService* playState = world.Services().Find<RuntimePlayStateService>();
    if (playState == nullptr || !playState->IsActive()) {
        return nullptr;
    }
    return world.Services().Find<SEQUENCER::SequencePlaybackService>();
}

GameProjectSequencePlaybackHandle ToProjectSequenceHandle(SEQUENCER::SequencePlaybackHandle handle) noexcept {
    return {handle.value, handle.epoch};
}

SEQUENCER::SequencePlaybackHandle ToRuntimeSequenceHandle(GameProjectSequencePlaybackHandle handle) noexcept {
    return {handle.value, handle.epoch};
}

template <class Operation>
bool InvokeSequence(GameProjectWorldHandle worldHandle, GameProjectSequencePlaybackHandle handle,
                    Operation&& operation) noexcept {
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(worldHandle);
        auto* service = world != nullptr ? FindSequencePlaybackService(*world) : nullptr;
        return service != nullptr && handle.IsValid() && operation(*service, ToRuntimeSequenceHandle(handle));
    } catch (...) {
        return false;
    }
}

bool DecodeSequenceChannelPolicy(GameProjectSequenceChannelPolicy policy,
                                 SEQUENCER::SequenceChannelPolicy& outPolicy) noexcept {
    switch (policy) {
    case GameProjectSequenceChannelPolicy::Parallel:
        outPolicy = SEQUENCER::SequenceChannelPolicy::Parallel;
        return true;
    case GameProjectSequenceChannelPolicy::Replace: outPolicy = SEQUENCER::SequenceChannelPolicy::Replace; return true;
    case GameProjectSequenceChannelPolicy::ReplaceIfHigherOrEqual:
        outPolicy = SEQUENCER::SequenceChannelPolicy::ReplaceIfHigherOrEqual;
        return true;
    case GameProjectSequenceChannelPolicy::RejectIfOccupied:
        outPolicy = SEQUENCER::SequenceChannelPolicy::RejectIfOccupied;
        return true;
    default: return false;
    }
}

GameProjectSequencePlaybackState ToProjectSequenceState(SEQUENCER::SequencePlaybackState state) noexcept {
    switch (state) {
    case SEQUENCER::SequencePlaybackState::Playing: return GameProjectSequencePlaybackState::Playing;
    case SEQUENCER::SequencePlaybackState::Paused: return GameProjectSequencePlaybackState::Paused;
    case SEQUENCER::SequencePlaybackState::Stopped:
    default: return GameProjectSequencePlaybackState::Stopped;
    }
}

GameProjectSequencePlaybackEventKind ToProjectSequenceEventKind(SEQUENCER::SequencePlaybackEventKind kind) noexcept {
    switch (kind) {
    case SEQUENCER::SequencePlaybackEventKind::Started: return GameProjectSequencePlaybackEventKind::Started;
    case SEQUENCER::SequencePlaybackEventKind::Paused: return GameProjectSequencePlaybackEventKind::Paused;
    case SEQUENCER::SequencePlaybackEventKind::Resumed: return GameProjectSequencePlaybackEventKind::Resumed;
    case SEQUENCER::SequencePlaybackEventKind::Seeked: return GameProjectSequencePlaybackEventKind::Seeked;
    case SEQUENCER::SequencePlaybackEventKind::Completed: return GameProjectSequencePlaybackEventKind::Completed;
    case SEQUENCER::SequencePlaybackEventKind::Stopped: return GameProjectSequencePlaybackEventKind::Stopped;
    case SEQUENCER::SequencePlaybackEventKind::Rejected:
    default: return GameProjectSequencePlaybackEventKind::Rejected;
    }
}

GameProjectSequenceStopReason ToProjectSequenceStopReason(SEQUENCER::SequenceStopReason reason) noexcept {
    switch (reason) {
    case SEQUENCER::SequenceStopReason::Replaced: return GameProjectSequenceStopReason::Replaced;
    case SEQUENCER::SequenceStopReason::Completed: return GameProjectSequenceStopReason::Completed;
    case SEQUENCER::SequenceStopReason::Disabled: return GameProjectSequenceStopReason::Disabled;
    case SEQUENCER::SequenceStopReason::OwnerDestroyed: return GameProjectSequenceStopReason::OwnerDestroyed;
    case SEQUENCER::SequenceStopReason::Reset: return GameProjectSequenceStopReason::Reset;
    case SEQUENCER::SequenceStopReason::Stopped:
    default: return GameProjectSequenceStopReason::Stopped;
    }
}

bool ValidateSequenceOptions(const GameProjectSequencePlayOptions& options) noexcept {
    if (options.structSize < kGameProjectSequencePlayOptionsV1Size ||
        options.version != kGameProjectSequencePlaybackVersion || !std::isfinite(options.startTimeSeconds) ||
        !std::isfinite(options.playbackRate) || std::abs(options.playbackRate) < 0.01f ||
        std::abs(options.playbackRate) > 8.0f || options.loop > 1u || options.holdAtEnd > 1u ||
        options.bindingCount > kGameProjectSequenceMaximumBindings ||
        (options.bindingCount != 0u && options.bindings == nullptr) || options.reserved0 != 0u) {
        return false;
    }
    for (uint32_t value : options.reserved) {
        if (value != 0u) {
            return false;
        }
    }
    SEQUENCER::SequenceChannelPolicy runtimePolicy{};
    return DecodeSequenceChannelPolicy(options.channelPolicy, runtimePolicy);
}

bool BuildSequencePlayRequest(World& world, std::string_view assetGuid, const GameProjectSequencePlayOptions& options,
                              SEQUENCER::SequencePlayRequest& outRequest) {
    outRequest = {};
    if (!IsValidAssetGuid(assetGuid) || assetGuid.find('\0') != std::string_view::npos ||
        !ValidateSequenceOptions(options)) {
        return false;
    }

    std::string channel{};
    if (!ReadFixedUtf8(options.channel, channel)) {
        return false;
    }
    outRequest.assetGuid = AssetGuid{std::string(assetGuid)};
    outRequest.options.loop = options.loop != 0u;
    outRequest.options.holdAtEnd = options.holdAtEnd != 0u;
    outRequest.options.playbackRate = options.playbackRate;
    outRequest.startTimeSeconds = options.startTimeSeconds;
    outRequest.channel = std::move(channel);
    outRequest.priority = options.priority;
    if (!DecodeSequenceChannelPolicy(options.channelPolicy, outRequest.channelPolicy)) {
        outRequest = {};
        return false;
    }

    for (uint32_t index = 0u; index < options.bindingCount; ++index) {
        const GameProjectSequenceBinding& binding = options.bindings[index];
        std::string slotName{};
        if (!ReadFixedUtf8(binding.slotName, slotName) || slotName.empty() ||
            !PROJECT::INTERNAL::IsObjectHandleForWorld(binding.object, world)) {
            outRequest = {};
            return false;
        }
        for (uint32_t prior = 0u; prior < index; ++prior) {
            std::string priorSlotName{};
            if (!ReadFixedUtf8(options.bindings[prior].slotName, priorSlotName) || priorSlotName == slotName) {
                outRequest = {};
                return false;
            }
        }
        GameObject* object = world.FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(binding.object));
        if (object == nullptr || object->GetDocumentId().value == 0u ||
            !outRequest.bindings.BindSlot(std::move(slotName), object->GetDocumentId())) {
            outRequest = {};
            return false;
        }
    }
    return true;
}

bool FillProjectSequenceSnapshot(const SEQUENCER::SequencePlaybackSnapshot& source,
                                 GameProjectSequencePlaybackSnapshot& destination) noexcept {
    destination = {};
    destination.handle = ToProjectSequenceHandle(source.handle);
    destination.state = ToProjectSequenceState(source.state);
    destination.timeSeconds = source.timeSeconds;
    destination.durationSeconds = source.durationSeconds;
    destination.priority = source.priority;
    return CopyFixedUtf8(source.assetGuid.value, destination.assetGuid) &&
           CopyFixedUtf8(source.channel, destination.channel);
}

void FillProjectSequenceEvent(const SEQUENCER::SequencePlaybackEvent& source,
                              GameProjectSequencePlaybackEvent& destination) noexcept {
    destination = {};
    destination.handle = ToProjectSequenceHandle(source.handle);
    destination.requestId = source.requestId;
    destination.timeSeconds = source.timeSeconds;
    destination.kind = ToProjectSequenceEventKind(source.kind);
    destination.stopReason = ToProjectSequenceStopReason(source.stopReason);
    (void)CopyFixedUtf8Truncated(std::string_view(source.assetGuid.value), destination.assetGuid);
    destination.messageTruncated =
        CopyFixedUtf8Truncated(std::string_view(source.message), destination.message) ? 1u : 0u;
}

} // namespace

SequenceView GameProjectSdkAccess::MakeSequenceView(World& world) noexcept {
    return SequenceView(GameProjectWorldHandle{world.GetRuntimeIdentity()});
}

SequenceView WorldView::GetSequence() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeSequenceView(*world) : SequenceView{};
}

bool SequenceView::IsValid() const noexcept { return PROJECT::INTERNAL::ResolveServiceWorld(world_) != nullptr; }

GameProjectSequencePlaybackHandle SequenceView::Play(std::string_view assetGuid,
                                                     const GameProjectSequencePlayOptions& options) const noexcept {
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        SEQUENCER::SequencePlaybackService* service = world != nullptr ? FindSequencePlaybackService(*world) : nullptr;
        if (service == nullptr) {
            return {};
        }
        SEQUENCER::SequencePlayRequest request{};
        if (!BuildSequencePlayRequest(*world, assetGuid, options, request)) {
            return {};
        }
        return ToProjectSequenceHandle(service->PlayDetailed(request).handle);
    } catch (...) {
        return {};
    }
}

bool SequenceView::Stop(GameProjectSequencePlaybackHandle handle) const noexcept {
    return InvokeSequence(world_, handle,
                          [](auto& service, auto runtimeHandle) { return service.Stop(runtimeHandle); });
}

bool SequenceView::Pause(GameProjectSequencePlaybackHandle handle) const noexcept {
    return InvokeSequence(world_, handle,
                          [](auto& service, auto runtimeHandle) { return service.Pause(runtimeHandle); });
}

bool SequenceView::Resume(GameProjectSequencePlaybackHandle handle) const noexcept {
    return InvokeSequence(world_, handle,
                          [](auto& service, auto runtimeHandle) { return service.Resume(runtimeHandle); });
}

bool SequenceView::SetPlaybackRate(GameProjectSequencePlaybackHandle handle, float playbackRate) const noexcept {
    return std::isfinite(playbackRate) && std::abs(playbackRate) >= 0.01f && std::abs(playbackRate) <= 8.0f &&
           InvokeSequence(world_, handle, [playbackRate](auto& service, auto runtimeHandle) {
               return service.SetPlaybackRate(runtimeHandle, playbackRate);
           });
}

bool SequenceView::Seek(GameProjectSequencePlaybackHandle handle, float timeSeconds) const noexcept {
    return std::isfinite(timeSeconds) &&
           InvokeSequence(world_, handle, [timeSeconds](auto& service, auto runtimeHandle) {
               return service.Seek(runtimeHandle, timeSeconds);
           });
}

bool SequenceView::SetUseUnscaledTime(GameProjectSequencePlaybackHandle handle, bool enabled) const noexcept {
    return InvokeSequence(world_, handle, [enabled](auto& service, auto runtimeHandle) {
        return service.SetUseUnscaledTime(runtimeHandle, enabled);
    });
}

bool SequenceView::IsActive(GameProjectSequencePlaybackHandle handle) const noexcept {
    return InvokeSequence(world_, handle,
                          [](auto& service, auto runtimeHandle) { return service.IsActive(runtimeHandle); });
}

bool SequenceView::IsPlaying(GameProjectSequencePlaybackHandle handle) const noexcept {
    return InvokeSequence(world_, handle,
                          [](auto& service, auto runtimeHandle) { return service.IsPlaying(runtimeHandle); });
}

bool SequenceView::IsPaused(GameProjectSequencePlaybackHandle handle) const noexcept {
    return InvokeSequence(world_, handle,
                          [](auto& service, auto runtimeHandle) { return service.IsPaused(runtimeHandle); });
}

uint32_t SequenceView::GetEventClips(GameProjectSequencePlaybackHandle handle, GameProjectSequenceEventClip* out,
                                     uint32_t capacity) const noexcept {
    try {
        // 読み取りだけなので Play の状態を問わない（編集器のプレビューにも使う）。
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        const auto* service = world != nullptr ? world->Services().Find<SEQUENCER::SequencePlaybackService>() : nullptr;
        const CinematicSequence* sequence =
            service != nullptr && handle.IsValid() ? service->GetSequence(ToRuntimeSequenceHandle(handle)) : nullptr;
        if (sequence == nullptr || (out == nullptr && capacity != 0u)) {
            return 0u;
        }
        const auto& clips = sequence->eventTrack.clips;
        for (uint32_t index = 0u; index < capacity && index < clips.size(); ++index) {
            out[index] = {};
            (void)CopyFixedUtf8Truncated(clips[index].name, out[index].name);
            out[index].startSeconds = clips[index].startSeconds;
            out[index].durationSeconds = clips[index].durationSeconds;
        }
        return static_cast<uint32_t>(clips.size());
    } catch (...) {
        return 0u;
    }
}

bool SequenceView::GetEditorPreview(GameProjectSequencePlaybackSnapshot& outSnapshot) const noexcept {
    outSnapshot = {};
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        if (world == nullptr) {
            return false;
        }
        const RuntimePlayStateService* playState = world->Services().Find<RuntimePlayStateService>();
        const auto* service = world->Services().Find<SEQUENCER::SequencePlaybackService>();
        if (service == nullptr || (playState != nullptr && playState->IsActive())) {
            return false;
        }
        SEQUENCER::SequencePlaybackSnapshot snapshot{};
        const auto handle = service->FindActiveByChannel(SEQUENCER::kEditorSequencePreviewChannel);
        return handle.IsValid() && service->TryGetSnapshot(handle, snapshot) &&
               FillProjectSequenceSnapshot(snapshot, outSnapshot);
    } catch (...) {
        outSnapshot = {};
        return false;
    }
}

bool SequenceView::GetSnapshot(GameProjectSequencePlaybackHandle handle,
                               GameProjectSequencePlaybackSnapshot& outSnapshot) const noexcept {
    outSnapshot = {};
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        SEQUENCER::SequencePlaybackService* service = world != nullptr ? FindSequencePlaybackService(*world) : nullptr;
        if (service == nullptr || !handle.IsValid()) {
            return false;
        }
        SEQUENCER::SequencePlaybackSnapshot snapshot{};
        if (!service->TryGetSnapshot(ToRuntimeSequenceHandle(handle), snapshot)) {
            return false;
        }
        return FillProjectSequenceSnapshot(snapshot, outSnapshot);
    } catch (...) {
        outSnapshot = {};
        return false;
    }
}

uint32_t SequenceView::ConsumeEvents(void* context, GameProjectSequencePlaybackEventCallback callback) const noexcept {
    if (callback == nullptr) {
        return 0u;
    }
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        SEQUENCER::SequencePlaybackService* service = world != nullptr ? FindSequencePlaybackService(*world) : nullptr;
        if (service == nullptr) {
            return 0u;
        }
        const std::vector<SEQUENCER::SequencePlaybackEvent> events = service->ConsumeEvents();
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
    } catch (...) {
        return 0u;
    }
}

} // namespace HIKARI
