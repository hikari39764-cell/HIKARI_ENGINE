#pragma once

#include <memory>

namespace HIKARI::AUDIO {

struct IAudioBackend;

enum class BackendType {
    XAudio2,
    Null,
};

bool Initialize(BackendType type = BackendType::XAudio2);
void Shutdown();

int Load(const char* path);
int Play(int soundHandle, bool loop, float volume);
void Stop(int playHandle);
void Pause(int playHandle);
void Resume(int playHandle);
void SetVolume(int playHandle, float volume);
bool IsPlaying(int playHandle);

} // namespace HIKARI::AUDIO
