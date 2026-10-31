#pragma once

#include <string>
#include <vector>
#include "Core/HIKARI_AssetGuid.h"
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
    // 音声だけ：片段の頭と終わりの音量の立ち上がり・消え際（秒）、素材のどこから鳴らすか（秒）、
    // 音の高さと速さ（1 がそのまま。演出の再生速度に掛ける）。
    float fadeInSeconds = 0.0f;
    float fadeOutSeconds = 0.0f;
    float sourceOffsetSeconds = 0.0f;
    float pitch = 1.0f;
};
struct PresentationCueTrack {
    bool enabled = true;
    std::vector<PresentationCueClip> clips;
};
inline constexpr size_t kMaximumPresentationCues = 64u;
bool ValidatePresentationCueTrack(const PresentationCueTrack& track, std::string& error);
float GetPresentationCueContentEnd(const PresentationCueTrack& track) noexcept;
// 片段の中の時刻での音量の掛け率（0〜1、淡入・淡出）。
float GetPresentationCueFadeGain(const PresentationCueClip& clip, float localTimeSeconds) noexcept;
} // namespace HIKARI::SEQUENCER
