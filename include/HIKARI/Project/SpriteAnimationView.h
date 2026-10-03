#pragma once

#include <string_view>
#include <type_traits>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/SpriteAnimationTypes.h"
#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

class ObjectView;
struct GameProjectSpriteAnimationBridgeAccess;

// Non-owning runtime view over a WorldSpriteComponent animation player.
// Calls resolve the object on the World owner thread; the view does not keep
// the World or component alive and becomes invalid when either is destroyed.
// Invalid world/thread/object, unsupported component/Canvas, invalid request,
// unavailable SpriteAssetStore, invalid asset, and unknown clip are reported
// as the corresponding GameProjectWorldResultCode.
class SpriteAnimationView {
public:
    SpriteAnimationView() = default;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
    // Resolves and validates the asset before atomically changing the player.
    // On success the asset default clip (or first clip) starts at time zero.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult Bind(std::string_view spriteAssetGuid) const noexcept;
    // A different clip always starts at zero. For the current clip, restart
    // false retains the playhead and resumes it. The authored loop setting is used.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult Play(std::string_view clipId,
                                                            bool restart = true) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult Pause() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult Resume() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult Restart() const noexcept;
    // Speed must be finite and positive.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetSpeed(float speed) const noexcept;
    // Seeks in seconds from the beginning of the current clip. Looping clips
    // wrap into [0, duration); one-shot clips clamp to duration and finish.
    // Seeking preserves the current pause state.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult Seek(float elapsedSeconds) const noexcept;
    // outState must be initialized with the exact V1 size and version.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    TryGetState(GameProjectSpriteAnimationState& outState) const noexcept;
    // Exact size/version required; any failure leaves outInfo and player unchanged.
    // Unknown clip returns InvalidName; no bound native asset returns InvalidAsset.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    TryGetClipInfo(std::string_view clipId, GameProjectSpriteAnimationClipInfo& outInfo) const noexcept;

private:
    friend class ObjectView;
    friend struct GameProjectSpriteAnimationBridgeAccess;

    explicit SpriteAnimationView(GameProjectObjectHandle object) noexcept : object_(object) {}

    GameProjectObjectHandle object_{};
};

static_assert(std::is_standard_layout_v<SpriteAnimationView>);
static_assert(std::is_trivially_copyable_v<SpriteAnimationView>);

} // namespace HIKARI
