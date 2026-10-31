#pragma once

#include <memory>
#include <filesystem>
#include <string_view>
#include "HIKARI/Project/AudioTypes.h"

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
// 任意の先読み。Load の同期完了・失敗契約は変更しない。
void PreloadDirectory(const std::filesystem::path& root);
int Play(int soundHandle, bool loop, float volume);
int PlayOnBus(int soundHandle, bool loop, float volume, AudioBus bus);
void Stop(int playHandle);
void Pause(int playHandle);
void Resume(int playHandle);
void SetVolume(int playHandle, float volume);
bool IsPlaying(int playHandle);
bool TryGetPlaybackSnapshot(int playHandle, GameProjectAudioPlaybackSnapshot& outSnapshot);
void SetBusVolume(AudioBus bus, float volume);
float GetBusVolume(AudioBus bus);
int PlayCue(int soundHandle, const GameProjectAudioPlayOptions& options, uint64_t owner = 0u);
bool SetListener(const GameProjectAudioListener& listener);
void UseCameraListener();
void UpdateCameraListener(const GameProjectAudioListener& listener);
bool SetSource(int playHandle, const GameProjectAudioSource& source);
bool SetPlaybackRate(int playHandle, float rate);
// 読み込んだ音（Load の戻り値）の長さ（秒）。分からなければ 0。
float GetSoundSeconds(int soundHandle);
void StopOwner(uint64_t owner);
bool BelongsToOwner(int playHandle, uint64_t owner);
GameProjectAudioStats GetStats();

} // namespace HIKARI::AUDIO
