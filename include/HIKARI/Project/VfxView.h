#pragma once

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/VfxTypes.h"

namespace HIKARI {

    struct GameProjectSdkAccess;

    class VfxView {
    public:
        VfxView() = default;

        bool IsValid() const noexcept {
            return world_ != nullptr;
        }

        HIKARI_GAME_PROJECT_SDK_API GameProjectVfxPlaybackHandle Play(
            const GameProjectVfxPlayRequest& request) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Stop(
            GameProjectVfxPlaybackHandle handle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Update(
            GameProjectVfxPlaybackHandle handle,
            const GameProjectVfxUpdateRequest& request) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool IsActive(
            GameProjectVfxPlaybackHandle handle) const noexcept;

    private:
        friend struct GameProjectSdkAccess;

        explicit VfxView(void* world) noexcept
            : world_(world) {}

        void* world_ = nullptr;
    };

} // namespace HIKARI
