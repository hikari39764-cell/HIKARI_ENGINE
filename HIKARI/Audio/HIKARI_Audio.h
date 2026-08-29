#pragma once

#include <memory>

namespace HIKARI::AUDIO {

struct IAudioBackend;

enum class BackendType {
    XAudio2,
    Null,
};

enum class AudioBus {
    Bgm = 0,
    Se = 1,
    Voice = 2,
};

bool Initialize(BackendType type = BackendType::XAudio2);
void Shutdown();

int Load(const char* path);
int Play(int soundHandle, bool loop, float volume);
int PlayOnBus(
    int soundHandle,
    bool loop,
    float volume,
    AudioBus bus);
void Stop(int playHandle);
void Pause(int playHandle);
void Resume(int playHandle);
void SetVolume(int playHandle, float volume);
bool IsPlaying(int playHandle);
void SetBusVolume(AudioBus bus, float volume);
float GetBusVolume(AudioBus bus);

} // namespace HIKARI::AUDIO
