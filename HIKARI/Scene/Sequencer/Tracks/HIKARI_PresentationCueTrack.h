#pragma once

#include <string>
#include <vector>
#include "Assets/HIKARI_AssetGuid.h"
#include "HIKARI/Project/AudioTypes.h"
#include "Scene/Sequencer/HIKARI_SequenceBinding.h"

namespace HIKARI::SEQUENCER {
    enum class PresentationCueKind : uint8_t { Audio, Vfx };
    struct PresentationCueClip {
        uint64_t id = 1u;
        std::string name = "Cue";
        PresentationCueKind kind = PresentationCueKind::Audio;
        float startSeconds = 0.0f;
        float durationSeconds = 1.0f;
        SequenceBindingId targetBinding{};
        std::string soundPath;
        AssetGuid vfxAsset;
        GameProjectAudioPlayOptions audio{};
    };
    struct PresentationCueTrack {
        bool enabled = true;
        std::vector<PresentationCueClip> clips;
    };
    inline constexpr size_t kMaximumPresentationCues = 64u;
    bool ValidatePresentationCueTrack(const PresentationCueTrack& track, std::string& error);
    float GetPresentationCueContentEnd(const PresentationCueTrack& track) noexcept;
}
