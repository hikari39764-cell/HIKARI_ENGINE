#pragma once

#include "../HIKARI_IAudioBackend.h"

#include <cstdint>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace HIKARI::AUDIO {

class AudioBackendXAudio2 final : public IAudioBackend {
public:
    AudioBackendXAudio2();
    ~AudioBackendXAudio2() override;
    bool Initialize() override;
    void Shutdown() override;

    int Load(const char* path) override;
    void PreloadDirectory(const std::filesystem::path& root) override;
    int Play(int soundHandle, bool loop, float volume) override;
    void Stop(int playHandle) override;
    void Pause(int playHandle) override;
    void Resume(int playHandle) override;
    void SetVolume(int playHandle, float volume) override;
    bool IsPlaying(int playHandle) override;

private:
    struct SoundData {
        std::string path{};
        std::vector<uint8_t> formatBytes{};
        std::vector<uint8_t> audioBytes{};
    };

    struct PlayingVoice {
        IXAudio2SourceVoice* voice = nullptr;
        int soundHandle = -1;
        bool active = false;
        bool paused = false;
    };

    bool DecodeSound(const char* path, SoundData& outSound, const std::atomic<bool>* cancel = nullptr) const;
    bool LoadWaveFile(const char* path, SoundData& outSound, const std::atomic<bool>* cancel) const;
    bool LoadMediaFile(const char* path, SoundData& outSound, const std::atomic<bool>* cancel) const;
    void DestroyVoiceSlot(int playHandle);
    void CleanupFinishedVoices();

    std::unique_ptr<IXAudio2, void(*)(IXAudio2*)> engine_{ nullptr, nullptr };
    IXAudio2MasteringVoice* masteringVoice_ = nullptr;

    std::vector<SoundData> sounds_{};
    std::vector<PlayingVoice> voices_{};
    struct DecodeCache;
    std::unique_ptr<DecodeCache> decodeCache_;
    bool mediaFoundationStarted_ = false;
    bool comInitialized_ = false;
};

} // namespace HIKARI::AUDIO
