#include "HIKARI_AudioBackend_XAudio2.h"

#include "Core/HIKARI_Logger.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <xaudio2.h>

namespace HIKARI::AUDIO {

namespace {

    constexpr uint32_t MakeFourCc(char a, char b, char c, char d)
    {
        return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
            (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
            (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
    }

    bool ReadU32(std::ifstream& file, uint32_t& outValue)
    {
        file.read(reinterpret_cast<char*>(&outValue), sizeof(outValue));
        return static_cast<bool>(file);
    }

    void ReleaseXAudio2(IXAudio2* engine)
    {
        if (engine) {
            engine->Release();
        }
    }

    bool IsWaveFormatSupported(const WAVEFORMATEX& format)
    {
        return format.wFormatTag == WAVE_FORMAT_PCM ||
            format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
            format.wFormatTag == WAVE_FORMAT_EXTENSIBLE;
    }

} // namespace

bool AudioBackendXAudio2::Initialize()
{
    IXAudio2* rawEngine = nullptr;
    const HRESULT createResult = XAudio2Create(&rawEngine, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(createResult) || !rawEngine) {
        HIKARI_LOG_ERROR("[Audio][XAudio2] XAudio2Create failed.");
        return false;
    }

    engine_ = std::unique_ptr<IXAudio2, void(*)(IXAudio2*)>(rawEngine, ReleaseXAudio2);

    const HRESULT masteringResult = engine_->CreateMasteringVoice(&masteringVoice_);
    if (FAILED(masteringResult) || !masteringVoice_) {
        HIKARI_LOG_ERROR("[Audio][XAudio2] CreateMasteringVoice failed.");
        Shutdown();
        return false;
    }

    HIKARI_LOG_INFO("[Audio][XAudio2] initialized.");
    return true;
}

void AudioBackendXAudio2::Shutdown()
{
    for (size_t i = 0; i < voices_.size(); ++i) {
        DestroyVoiceSlot(static_cast<int>(i));
    }
    voices_.clear();
    sounds_.clear();

    if (masteringVoice_) {
        masteringVoice_->DestroyVoice();
        masteringVoice_ = nullptr;
    }

    engine_.reset();
}

int AudioBackendXAudio2::Load(const char* path)
{
    if (!path || !engine_) {
        return -1;
    }

    SoundData sound{};
    if (!LoadWaveFile(path, sound)) {
        return -1;
    }

    const int handle = static_cast<int>(sounds_.size());
    sounds_.push_back(std::move(sound));
    return handle;
}

int AudioBackendXAudio2::Play(int soundHandle, bool loop, float volume)
{
    if (!engine_ || soundHandle < 0 || soundHandle >= static_cast<int>(sounds_.size())) {
        return -1;
    }

    CleanupFinishedVoices();

    const SoundData& sound = sounds_[static_cast<size_t>(soundHandle)];
    IXAudio2SourceVoice* voice = nullptr;
    const auto* format = reinterpret_cast<const WAVEFORMATEX*>(sound.formatBytes.data());
    const HRESULT createResult = engine_->CreateSourceVoice(&voice, format);
    if (FAILED(createResult) || !voice) {
        HIKARI_LOG_ERROR("[Audio][XAudio2] CreateSourceVoice failed: " + sound.path);
        return -1;
    }

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(sound.audioBytes.size());
    buffer.pAudioData = sound.audioBytes.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    const HRESULT submitResult = voice->SubmitSourceBuffer(&buffer);
    if (FAILED(submitResult)) {
        voice->DestroyVoice();
        HIKARI_LOG_ERROR("[Audio][XAudio2] SubmitSourceBuffer failed: " + sound.path);
        return -1;
    }

    voice->SetVolume(std::max(0.0f, volume));

    const HRESULT startResult = voice->Start();
    if (FAILED(startResult)) {
        voice->DestroyVoice();
        HIKARI_LOG_ERROR("[Audio][XAudio2] SourceVoice Start failed: " + sound.path);
        return -1;
    }

    PlayingVoice playing{};
    playing.voice = voice;
    playing.soundHandle = soundHandle;
    playing.active = true;

    for (size_t i = 0; i < voices_.size(); ++i) {
        if (!voices_[i].active) {
            voices_[i] = playing;
            return static_cast<int>(i);
        }
    }

    voices_.push_back(playing);
    return static_cast<int>(voices_.size() - 1);
}

void AudioBackendXAudio2::Stop(int playHandle)
{
    DestroyVoiceSlot(playHandle);
}

void AudioBackendXAudio2::Pause(int playHandle)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (!playing.active || !playing.voice) {
        return;
    }

    playing.voice->Stop();
    playing.paused = true;
}

void AudioBackendXAudio2::Resume(int playHandle)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (!playing.active || !playing.voice) {
        return;
    }

    playing.voice->Start();
    playing.paused = false;
}

void AudioBackendXAudio2::SetVolume(int playHandle, float volume)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (playing.active && playing.voice) {
        playing.voice->SetVolume(std::max(0.0f, volume));
    }
}

bool AudioBackendXAudio2::IsPlaying(int playHandle)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return false;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (!playing.active || !playing.voice) {
        return false;
    }

    XAUDIO2_VOICE_STATE state{};
    playing.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (state.BuffersQueued == 0) {
        DestroyVoiceSlot(playHandle);
        return false;
    }

    return true;
}

bool AudioBackendXAudio2::LoadWaveFile(const char* path, SoundData& outSound) const
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        HIKARI_LOG_ERROR(std::string("[Audio][XAudio2] wave file open failed: ") + path);
        return false;
    }

    uint32_t riff = 0;
    uint32_t riffSize = 0;
    uint32_t wave = 0;
    if (!ReadU32(file, riff) || !ReadU32(file, riffSize) || !ReadU32(file, wave) ||
        riff != MakeFourCc('R', 'I', 'F', 'F') ||
        wave != MakeFourCc('W', 'A', 'V', 'E')) {
        HIKARI_LOG_ERROR(std::string("[Audio][XAudio2] invalid wave header: ") + path);
        return false;
    }
    (void)riffSize;

    std::vector<uint8_t> formatBytes{};
    std::vector<uint8_t> audioBytes{};

    while (file.peek() != EOF) {
        uint32_t chunkId = 0;
        uint32_t chunkSize = 0;
        if (!ReadU32(file, chunkId) || !ReadU32(file, chunkSize)) {
            break;
        }

        if (chunkId == MakeFourCc('f', 'm', 't', ' ')) {
            // WAV の fmt は PCM だと 16 bytes のため、XAudio2 用にゼロ埋めして保持する。
            const size_t storedSize = std::max<size_t>(chunkSize, sizeof(WAVEFORMATEX));
            formatBytes.assign(storedSize, 0);
            file.read(reinterpret_cast<char*>(formatBytes.data()), chunkSize);
        }
        else if (chunkId == MakeFourCc('d', 'a', 't', 'a')) {
            audioBytes.resize(chunkSize);
            file.read(reinterpret_cast<char*>(audioBytes.data()), chunkSize);
        }
        else {
            file.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
        }

        if (!file) {
            HIKARI_LOG_ERROR(std::string("[Audio][XAudio2] broken wave chunk: ") + path);
            return false;
        }

        if ((chunkSize & 1U) != 0U) {
            file.seekg(1, std::ios::cur);
        }
    }

    if (formatBytes.empty() || audioBytes.empty()) {
        HIKARI_LOG_ERROR(std::string("[Audio][XAudio2] missing fmt/data chunk: ") + path);
        return false;
    }

    const WAVEFORMATEX* format = reinterpret_cast<const WAVEFORMATEX*>(formatBytes.data());
    if (!format || !IsWaveFormatSupported(*format) ||
        format->nChannels == 0 ||
        format->nSamplesPerSec == 0 ||
        format->nBlockAlign == 0) {
        HIKARI_LOG_ERROR(std::string("[Audio][XAudio2] unsupported wave format: ") + path);
        return false;
    }

    outSound.path = path;
    outSound.formatBytes = std::move(formatBytes);
    outSound.audioBytes = std::move(audioBytes);
    return true;
}

void AudioBackendXAudio2::DestroyVoiceSlot(int playHandle)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (playing.voice) {
        playing.voice->Stop();
        playing.voice->FlushSourceBuffers();
        playing.voice->DestroyVoice();
    }

    playing = PlayingVoice{};
}

void AudioBackendXAudio2::CleanupFinishedVoices()
{
    for (size_t i = 0; i < voices_.size(); ++i) {
        PlayingVoice& playing = voices_[i];
        if (!playing.active || !playing.voice || playing.paused) {
            continue;
        }

        XAUDIO2_VOICE_STATE state{};
        playing.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (state.BuffersQueued == 0) {
            DestroyVoiceSlot(static_cast<int>(i));
        }
    }
}

} // namespace HIKARI::AUDIO
