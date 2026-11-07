#pragma once

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/VfxTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

class VfxView {
public:
    VfxView() = default;

    bool IsValid() const noexcept { return world_ != nullptr; }

    HIKARI_GAME_PROJECT_SDK_API GameProjectVfxPlaybackHandle
    Play(const GameProjectVfxPlayRequest& request) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Stop(GameProjectVfxPlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Update(GameProjectVfxPlaybackHandle handle,
                                            const GameProjectVfxUpdateRequest& request) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool IsActive(GameProjectVfxPlaybackHandle handle) const noexcept;
    // Owner-thread instance time scale in [0,8]. Zero freezes simulation/age
    // while retaining the current geometry; parameter updates still apply.
    // One resumes at normal speed. This does not seek or supply a music clock.
    HIKARI_GAME_PROJECT_SDK_API bool SetTimeScale(GameProjectVfxPlaybackHandle handle, float scale) const noexcept;
    // Owner-thread world velocity [m/s] of the effect root: every frame the
    // root moves by velocity x instance time (the time scale applies),
    // starting from the transform given to Play or Update. A projectile
    // effect only needs Play + SetVelocity; its fluid injection sweeps the
    // path. Zero stops. Layers attached to the owner object keep following
    // the owner.
    HIKARI_GAME_PROJECT_SDK_API bool SetVelocity(GameProjectVfxPlaybackHandle handle,
                                                 const GameProjectFloat3& velocity) const noexcept;

private:
    friend struct GameProjectSdkAccess;

    explicit VfxView(void* world) noexcept : world_(world) {}

    void* world_ = nullptr;
};

} // namespace HIKARI
