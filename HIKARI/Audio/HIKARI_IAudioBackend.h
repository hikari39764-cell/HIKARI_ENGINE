#pragma once

#include <filesystem>
#include "HIKARI/Project/AudioTypes.h"

namespace HIKARI::AUDIO {

struct IAudioBackend {
    virtual ~IAudioBackend() = default;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual int Load(const char* path) = 0;
    virtual void PreloadDirectory(const std::filesystem::path&) {}
    virtual int Play(int soundHandle, bool loop, float volume, bool paused = false, float startSeconds = 0.0f, float rate = 1.0f) = 0;
    virtual void Stop(int playHandle) = 0;
    virtual void Pause(int playHandle) = 0;
    virtual void Resume(int playHandle) = 0;
    virtual void SetVolume(int playHandle, float volume) = 0;
    virtual bool IsPlaying(int playHandle) = 0;
    virtual bool SetSpatial(int, const GameProjectAudioListener&,
        const GameProjectAudioSource&) { return false; }
    virtual bool SetPlaybackRate(int, float) { return false; }
    virtual GameProjectAudioStats GetStats() const { return {}; }
};

} // namespace HIKARI::AUDIO
