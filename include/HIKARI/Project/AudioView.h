#pragma once

#include <string_view>

#include "HIKARI/Project/AudioTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"

namespace HIKARI {

struct GameProjectSdkAccess;

// Process-wide mix buses and playback remain engine-owned. The view is a
// narrow handle-based port and never exposes XAudio2 objects to projects.
class AudioView {
public:
    AudioView() = default;
    bool IsValid() const noexcept { return world_ != nullptr; }

    HIKARI_GAME_PROJECT_SDK_API bool SetBusVolume(GameProjectAudioBus bus, float linearVolume) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API float GetBusVolume(GameProjectAudioBus bus) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectAudioSoundHandle Load(std::string_view projectRelativePath) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectAudioPlaybackHandle Play(GameProjectAudioSoundHandle sound,
                                                                    GameProjectAudioBus bus, uint8_t loop,
                                                                    float linearVolume) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Stop(GameProjectAudioPlaybackHandle playback) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Pause(GameProjectAudioPlaybackHandle playback) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Resume(GameProjectAudioPlaybackHandle playback) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetVolume(GameProjectAudioPlaybackHandle playback,
                                               float linearVolume) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool IsPlaying(GameProjectAudioPlaybackHandle playback) const noexcept;
    // Owner-thread query. Invalid, foreign, stopped or completed handles and
    // unsupported snapshot size/version return false without modifying output.
    HIKARI_GAME_PROJECT_SDK_API bool TryGetPlaybackSnapshot(
        GameProjectAudioPlaybackHandle playback, GameProjectAudioPlaybackSnapshot& outSnapshot) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectAudioPlaybackHandle
    Play(GameProjectAudioSoundHandle sound, const GameProjectAudioPlayOptions& options) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool UseCameraListener() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetListener(const GameProjectAudioListener& listener) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetSource(GameProjectAudioPlaybackHandle playback,
                                               const GameProjectAudioSource& source) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectAudioStats GetStats() const noexcept;

private:
    friend struct GameProjectSdkAccess;
    explicit AudioView(void* world) noexcept : world_(world) {}
    void* world_ = nullptr;
};

} // namespace HIKARI
