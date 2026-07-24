#pragma once

namespace HIKARI::AUDIO {

struct IAudioBackend {
    virtual ~IAudioBackend() = default;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual int Load(const char* path) = 0;
    virtual int Play(int soundHandle, bool loop, float volume) = 0;
    virtual void Stop(int playHandle) = 0;
    virtual void Pause(int playHandle) = 0;
    virtual void Resume(int playHandle) = 0;
    virtual void SetVolume(int playHandle, float volume) = 0;
    virtual bool IsPlaying(int playHandle) = 0;
};

} // namespace HIKARI::AUDIO
