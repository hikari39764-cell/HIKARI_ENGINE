#pragma once

#include "HIKARI/Project/AudioTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"

namespace HIKARI {

    struct GameProjectSdkAccess;

    // Process-wide mix buses are engine-owned. The view is a narrow runtime
    // settings port; it neither exposes XAudio2 voices nor owns playback.
    class AudioView {
    public:
        AudioView() = default;
        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool SetBusVolume(
            GameProjectAudioBus bus,
            float linearVolume) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API float GetBusVolume(
            GameProjectAudioBus bus) const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit AudioView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

} // namespace HIKARI
