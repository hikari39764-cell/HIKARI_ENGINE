#pragma once

#include <memory>
#include "HIKARI_AudioWaveFile.h"

struct IXAudio2;
struct IXAudio2SourceVoice;

namespace HIKARI::AUDIO {
    // Worker は disk read のみ。callback は signal のみでメモリ確保しない。
    class AudioStreamVoice {
    public:
        AudioStreamVoice();
        ~AudioStreamVoice();
        bool Open(IXAudio2& engine, const std::filesystem::path& path,
            const AudioWaveInfo& info, bool loop, float startSeconds);
        IXAudio2SourceVoice* GetVoice() const noexcept;
        bool IsFeeding() const noexcept;
        uint64_t BufferBytes() const noexcept;
    private:
        struct State;
        std::unique_ptr<State> state_;
    };
}
