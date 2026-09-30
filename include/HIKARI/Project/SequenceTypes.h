#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

// Sequence playback is a runtime-only service. These fixed buffers keep
// authored identifiers and diagnostics on the value side of the public
// SDK boundary; the engine never returns an allocator-owned string.
inline constexpr uint32_t kGameProjectSequencePlaybackVersion = 1u;
inline constexpr uint32_t kGameProjectSequenceAssetGuidCapacity = 33u;
inline constexpr uint32_t kGameProjectSequenceChannelCapacity = 64u;
inline constexpr uint32_t kGameProjectSequenceSlotNameCapacity = 64u;
inline constexpr uint32_t kGameProjectSequenceMessageCapacity = 256u;
inline constexpr uint32_t kGameProjectSequenceMaximumBindings = 32u;
inline constexpr uint32_t kGameProjectSequencePlayOptionsV1Size = 112u;
// A GameProject camera-marker component may use these canonical property
// ids to let the Cinematics workspace restore an external Sequence's
// editor-only preview camera. Runtime playback remains explicit through
// SequenceView; the editor hint never auto-plays the asset.
inline constexpr char kGameProjectSequencePreviewAssetGuidProperty[] = "sequenceAssetGuid";
inline constexpr char kGameProjectSequencePreviewSlotNameProperty[] = "sequenceSlotName";

struct GameProjectSequencePlaybackHandle {
    uint64_t value = 0u;
    uint64_t epoch = 0u;

    constexpr bool IsValid() const noexcept { return value != 0u && epoch != 0u; }

    friend constexpr bool operator==(GameProjectSequencePlaybackHandle lhs,
                                     GameProjectSequencePlaybackHandle rhs) noexcept {
        return lhs.value == rhs.value && lhs.epoch == rhs.epoch;
    }

    friend constexpr bool operator!=(GameProjectSequencePlaybackHandle lhs,
                                     GameProjectSequencePlaybackHandle rhs) noexcept {
        return !(lhs == rhs);
    }
};

enum class GameProjectSequencePlaybackState : uint8_t {
    Stopped = 0u,
    Playing = 1u,
    Paused = 2u,
};

enum class GameProjectSequencePlaybackEventKind : uint8_t {
    Started = 0u,
    Paused = 1u,
    Resumed = 2u,
    Seeked = 3u,
    Completed = 4u,
    Stopped = 5u,
    Rejected = 6u,
};

enum class GameProjectSequenceStopReason : uint8_t {
    Stopped = 0u,
    Replaced = 1u,
    Completed = 2u,
    Disabled = 3u,
    OwnerDestroyed = 4u,
    Reset = 5u,
};

enum class GameProjectSequenceChannelPolicy : uint8_t {
    Parallel = 0u,
    Replace = 1u,
    ReplaceIfHigherOrEqual = 2u,
    RejectIfOccupied = 3u,
};

// A slot override is borrowed only during SequenceView::Play. The
// referenced object must be a live GameObject in the same World with a
// World-unique SceneObjectId, including an object from an instantiated
// Prefab; the engine resolves it to that identity before returning.
struct GameProjectSequenceBinding {
    char slotName[kGameProjectSequenceSlotNameCapacity]{};
    GameProjectObjectHandle object{};
};

// V1 is an extensible value descriptor. The pointer and its array are
// borrowed only for the synchronous Play call and are never retained by
// the engine after it returns.
struct GameProjectSequencePlayOptions {
    uint32_t structSize = kGameProjectSequencePlayOptionsV1Size;
    uint32_t version = kGameProjectSequencePlaybackVersion;
    float startTimeSeconds = 0.0f;
    // Signed playback rate in [-8,-0.01] or [0.01,8]. Negative values
    // evaluate the same asset backwards from startTimeSeconds.
    float playbackRate = 1.0f;
    int32_t priority = 0;
    GameProjectSequenceChannelPolicy channelPolicy = GameProjectSequenceChannelPolicy::ReplaceIfHigherOrEqual;
    uint8_t loop = 0u;
    // Keep the evaluated camera/track state active at the reached edge.
    // The playback remains paused and may be resumed after changing rate.
    uint8_t holdAtEnd = 0u;
    uint8_t reserved0 = 0u;
    char channel[kGameProjectSequenceChannelCapacity]{};
    const GameProjectSequenceBinding* bindings = nullptr;
    uint32_t bindingCount = 0u;
    uint32_t reserved[3] = {0u, 0u, 0u};
};

// Snapshot strings are copied into the caller's value. The snapshot is
// valid only for the call and becomes unavailable once the handle is no
// longer active or the owning World is rebuilt.
struct GameProjectSequencePlaybackSnapshot {
    GameProjectSequencePlaybackHandle handle{};
    GameProjectSequencePlaybackState state = GameProjectSequencePlaybackState::Stopped;
    uint8_t reserved0[3] = {0u, 0u, 0u};
    float timeSeconds = 0.0f;
    float durationSeconds = 0.0f;
    int32_t priority = 0;
    char assetGuid[kGameProjectSequenceAssetGuidCapacity]{};
    char channel[kGameProjectSequenceChannelCapacity]{};
    uint8_t reserved[7] = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
};

// Event values are delivered synchronously by ConsumeEvents. The event
// pointer passed to the callback is borrowed only for that callback. A
// message longer than the public buffer is truncated and marked, so a
// rejected request can never disappear because of diagnostic length.
struct GameProjectSequencePlaybackEvent {
    GameProjectSequencePlaybackHandle handle{};
    uint64_t requestId = 0u;
    float timeSeconds = 0.0f;
    GameProjectSequencePlaybackEventKind kind = GameProjectSequencePlaybackEventKind::Rejected;
    GameProjectSequenceStopReason stopReason = GameProjectSequenceStopReason::Stopped;
    uint8_t messageTruncated = 0u;
    uint8_t reserved0 = 0u;
    char assetGuid[kGameProjectSequenceAssetGuidCapacity]{};
    char message[kGameProjectSequenceMessageCapacity]{};
    uint8_t reserved[7] = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
};

using GameProjectSequencePlaybackEventCallback = void (*)(void* context,
                                                          const GameProjectSequencePlaybackEvent* event) noexcept;

static_assert(std::is_standard_layout_v<GameProjectSequencePlaybackHandle>);
static_assert(std::is_trivially_copyable_v<GameProjectSequencePlaybackHandle>);
static_assert(sizeof(GameProjectSequencePlaybackHandle) == 16u);
static_assert(offsetof(GameProjectSequencePlaybackHandle, value) == 0u);
static_assert(offsetof(GameProjectSequencePlaybackHandle, epoch) == 8u);
static_assert(std::is_standard_layout_v<GameProjectSequenceBinding>);
static_assert(std::is_trivially_copyable_v<GameProjectSequenceBinding>);
static_assert(sizeof(GameProjectSequenceBinding) == 80u);
static_assert(offsetof(GameProjectSequenceBinding, slotName) == 0u);
static_assert(offsetof(GameProjectSequenceBinding, object) == 64u);
static_assert(std::is_standard_layout_v<GameProjectSequencePlayOptions>);
static_assert(std::is_trivially_copyable_v<GameProjectSequencePlayOptions>);
static_assert(sizeof(GameProjectSequencePlayOptions) == kGameProjectSequencePlayOptionsV1Size);
static_assert(offsetof(GameProjectSequencePlayOptions, structSize) == 0u);
static_assert(offsetof(GameProjectSequencePlayOptions, version) == 4u);
static_assert(offsetof(GameProjectSequencePlayOptions, channel) == 24u);
static_assert(offsetof(GameProjectSequencePlayOptions, bindings) == 88u);
static_assert(offsetof(GameProjectSequencePlayOptions, bindingCount) == 96u);
static_assert(offsetof(GameProjectSequencePlayOptions, reserved) == 100u);
static_assert(std::is_standard_layout_v<GameProjectSequencePlaybackSnapshot>);
static_assert(std::is_trivially_copyable_v<GameProjectSequencePlaybackSnapshot>);
static_assert(sizeof(GameProjectSequencePlaybackSnapshot) == 136u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, handle) == 0u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, state) == 16u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, timeSeconds) == 20u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, assetGuid) == 32u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, channel) == 65u);
static_assert(std::is_standard_layout_v<GameProjectSequencePlaybackEvent>);
static_assert(std::is_trivially_copyable_v<GameProjectSequencePlaybackEvent>);
static_assert(sizeof(GameProjectSequencePlaybackEvent) == 328u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, handle) == 0u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, requestId) == 16u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, kind) == 28u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, assetGuid) == 32u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, message) == 65u);

} // namespace HIKARI
