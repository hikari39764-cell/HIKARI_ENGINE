#pragma once

#include <cstdint>
#include <string_view>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/SequenceTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

// Runtime sequence control is a non-owning view of the current World.
// Every operation resolves the generation-checked World identity again;
// retaining this view does not keep a World alive. Calls are valid only on
// the owning runtime thread while that World is current and not closing.
class SequenceView {
public:
    SequenceView() = default;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;

    // Play loads the sequence by its cooked/source Asset GUID and returns
    // a stable value+epoch handle. A zero handle means the request was
    // rejected. Play is synchronous on the owner thread; no worker queue
    // or hidden cross-thread handoff is performed.
    HIKARI_GAME_PROJECT_SDK_API GameProjectSequencePlaybackHandle
    Play(std::string_view assetGuid, const GameProjectSequencePlayOptions& options = {}) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Stop(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Pause(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Resume(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetPlaybackRate(GameProjectSequencePlaybackHandle handle,
                                                     float playbackRate) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Seek(GameProjectSequencePlaybackHandle handle, float timeSeconds) const noexcept;

    HIKARI_GAME_PROJECT_SDK_API bool IsActive(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool IsPlaying(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool IsPaused(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetSnapshot(GameProjectSequencePlaybackHandle handle,
                                                 GameProjectSequencePlaybackSnapshot& outSnapshot) const noexcept;

    // Consumes all events currently owned by this World. The callback and
    // its event reference are valid only during this call and execute on
    // the owning runtime thread. Null callbacks do not consume anything.
    // Completed and Rejected events are represented by the same fixed POD
    // contract as Started/Stopped/Pause/Resume/Seeked events.
    HIKARI_GAME_PROJECT_SDK_API uint32_t
    ConsumeEvents(void* context, GameProjectSequencePlaybackEventCallback callback) const noexcept;

private:
    friend struct GameProjectSdkAccess;

    explicit SequenceView(GameProjectWorldHandle world) noexcept : world_(world) {}

    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
