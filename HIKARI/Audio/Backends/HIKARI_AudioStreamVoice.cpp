#include "HIKARI_AudioStreamVoice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <thread>
#include <windows.h>
#include <xaudio2.h>

namespace HIKARI::AUDIO {
    struct AudioStreamVoice::State final : IXAudio2VoiceCallback {
        static constexpr size_t kBufferCount = 3u;
        static constexpr size_t kBufferBytes = 65536u;
        std::array<std::array<uint8_t, kBufferBytes>, kBufferCount> buffers{};
        std::ifstream file;
        AudioWaveInfo info;
        IXAudio2SourceVoice* voice = nullptr;
        HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        std::atomic<bool> feeding{false};
        std::atomic<bool> failed{false};
        std::atomic<bool> cancelled{false};
        uint32_t cursor = 0u;
        size_t nextBuffer = 0u;
        bool loop = false;
        std::thread worker;

        ~State() {
            cancelled.store(true);
            if (changed) SetEvent(changed);
            if (worker.joinable()) worker.join();
            // worker と XAudio2 callback の終了まで storage を保持する。
            if (voice) voice->DestroyVoice();
            if (changed) CloseHandle(changed);
        }
        bool SubmitNext() {
            if (cursor == info.dataBytes) {
                if (!loop) { feeding.store(false); return true; }
                cursor = 0u;
            }
            const auto& format = *reinterpret_cast<const WAVEFORMATEX*>(info.format.data());
            const auto capacity = static_cast<uint32_t>(kBufferBytes / format.nBlockAlign * format.nBlockAlign);
            const uint32_t count = (std::min)(capacity, info.dataBytes - cursor);
            file.seekg(static_cast<std::streamoff>(info.dataOffset + cursor));
            if (!file.read(reinterpret_cast<char*>(buffers[nextBuffer].data()), count)) return false;
            XAUDIO2_BUFFER buffer{};
            buffer.AudioBytes = count;
            buffer.pAudioData = buffers[nextBuffer].data();
            if (!loop && cursor + count == info.dataBytes) buffer.Flags = XAUDIO2_END_OF_STREAM;
            if (FAILED(voice->SubmitSourceBuffer(&buffer))) return false;
            cursor += count;
            nextBuffer = (nextBuffer + 1u) % kBufferCount;
            if (!loop && cursor == info.dataBytes) feeding.store(false);
            return true;
        }
        void Feed() noexcept {
            try {
                while (!cancelled.load() && feeding.load() && !failed.load()) {
                    XAUDIO2_VOICE_STATE status{};
                    voice->GetState(&status, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                    if (status.BuffersQueued < kBufferCount) {
                        if (!SubmitNext()) { failed.store(true); break; }
                    } else {
                        WaitForSingleObject(changed, 100u);
                    }
                }
            } catch (...) { failed.store(true); }
            feeding.store(false);
        }
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnStreamEnd() override { SetEvent(changed); }
        void STDMETHODCALLTYPE OnBufferStart(void*) override {}
        void STDMETHODCALLTYPE OnBufferEnd(void*) override { SetEvent(changed); }
        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
        void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {
            failed.store(true); SetEvent(changed);
        }
    };

    AudioStreamVoice::AudioStreamVoice() = default;
    AudioStreamVoice::~AudioStreamVoice() = default;
    bool AudioStreamVoice::Open(IXAudio2& engine, const std::filesystem::path& path,
        const AudioWaveInfo& info, bool loop, float startSeconds) {
        state_.reset();
        auto candidate = std::make_unique<State>();
        candidate->file.open(path, std::ios::binary);
        candidate->info = info;
        candidate->loop = loop;
        if (!candidate->file || !candidate->changed || info.format.size() < sizeof(WAVEFORMATEX)) return false;
        const auto* format = reinterpret_cast<const WAVEFORMATEX*>(info.format.data());
        if (FAILED(engine.CreateSourceVoice(&candidate->voice, format, 0u,
                8.0f, candidate.get()))) return false;
        const double samplePosition = double(startSeconds) * format->nSamplesPerSec;
        if (samplePosition >= double(info.dataBytes / format->nBlockAlign)) return false;
        const uint64_t sample = static_cast<uint64_t>(samplePosition);
        if (sample >= info.dataBytes / format->nBlockAlign) return false;
        candidate->cursor = static_cast<uint32_t>(sample * format->nBlockAlign);
        candidate->feeding.store(true);
        for (size_t i = 0; i < State::kBufferCount && candidate->feeding.load(); ++i)
            if (!candidate->SubmitNext()) return false;
        auto* state = candidate.get();
        candidate->worker = std::thread([state] { state->Feed(); });
        state_ = std::move(candidate);
        return true;
    }
    IXAudio2SourceVoice* AudioStreamVoice::GetVoice() const noexcept { return state_ ? state_->voice : nullptr; }
    bool AudioStreamVoice::IsFeeding() const noexcept {
        return state_ && state_->feeding.load() && !state_->failed.load();
    }
    uint64_t AudioStreamVoice::BufferBytes() const noexcept {
        return state_ ? State::kBufferCount * State::kBufferBytes : 0u;
    }
}
