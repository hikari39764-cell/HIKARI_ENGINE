#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HIKARI {

inline constexpr uint32_t kGameProjectSpriteAnimationStateVersion = 1u;
inline constexpr uint32_t kGameProjectSpriteAnimationStateV1Size = 32u;
inline constexpr size_t kGameProjectSpriteAnimationAssetGuidMaximumBytes = 128u;
inline constexpr uint32_t kGameProjectSpriteAnimationClipInfoVersion = 1u;

// Copied facts from the currently bound asset. Reading never changes playback.
struct GameProjectSpriteAnimationClipInfo {
    uint32_t structSize = sizeof(GameProjectSpriteAnimationClipInfo);
    uint32_t version = kGameProjectSpriteAnimationClipInfoVersion;
    uint32_t frameCount = 0u; // authored timed frames, not unique atlas images
    float durationSeconds = 0.0f;
    uint32_t loop = 0u;
};
static_assert(sizeof(GameProjectSpriteAnimationClipInfo) == 20u);
static_assert(std::is_trivially_copyable_v<GameProjectSpriteAnimationClipInfo>);

enum class GameProjectSpriteAnimationPlaybackState : uint32_t {
    // The object has no bound .hsprite source.
    Unbound = 0u,
    // A sprite asset is bound but has no active clip.
    Stopped,
    Playing,
    Paused,
    // A non-looping clip reached its duration. Play or restart clears this state.
    Finished,
    Count,
};

enum class GameProjectSpriteAnimationStateFlag : uint32_t {
    Loop = 1u << 0u,
};

constexpr uint32_t GameProjectSpriteAnimationStateFlags(GameProjectSpriteAnimationStateFlag flags) noexcept {
    return static_cast<uint32_t>(flags);
}

inline constexpr uint32_t kGameProjectSpriteAnimationStateKnownFlags =
    GameProjectSpriteAnimationStateFlags(GameProjectSpriteAnimationStateFlag::Loop);

// V1 reports the clip-local playhead and total duration in seconds. frameIndex
// is the index in the bound sprite asset. Unknown flags are invalid.
struct GameProjectSpriteAnimationState {
    uint32_t structSize = kGameProjectSpriteAnimationStateV1Size;
    uint32_t version = kGameProjectSpriteAnimationStateVersion;
    GameProjectSpriteAnimationPlaybackState playbackState = GameProjectSpriteAnimationPlaybackState::Unbound;
    uint32_t frameIndex = 0u;
    float elapsedSeconds = 0.0f;
    float durationSeconds = 0.0f;
    float speed = 1.0f;
    uint32_t flags = 0u;
};

static_assert(std::is_standard_layout_v<GameProjectSpriteAnimationState>);
static_assert(std::is_trivially_copyable_v<GameProjectSpriteAnimationState>);
static_assert(sizeof(GameProjectSpriteAnimationState) == kGameProjectSpriteAnimationStateV1Size);
static_assert(offsetof(GameProjectSpriteAnimationState, playbackState) == 8u);
static_assert(offsetof(GameProjectSpriteAnimationState, flags) == 28u);

} // namespace HIKARI
