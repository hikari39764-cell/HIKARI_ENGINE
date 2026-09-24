#include "HIKARI_PresentationCueTrack.h"
#include <algorithm>
#include "Scene/Sequencer/HIKARI_SequenceTrack.h"
#include <cmath>
#include <unordered_set>

namespace HIKARI::SEQUENCER {
bool ValidatePresentationCueTrack(const PresentationCueTrack& track, std::string& error) {
    error.clear();
    if (track.clips.size() > kMaximumPresentationCues) {
        error = "Presentation track exceeds 64 cues";
        return false;
    }
    std::unordered_set<uint64_t> ids;
    for (const auto& clip : track.clips) {
        const auto invalid = [&](const char* message) {
            error = "Cue " + std::to_string(clip.id) + ": " + message;
            return false;
        };
        if (clip.id == 0u || !ids.insert(clip.id).second)
            return invalid("duplicate or empty id");
        if (!std::isfinite(clip.startSeconds) || !std::isfinite(clip.durationSeconds) || clip.startSeconds < 0.0f ||
            clip.durationSeconds <= 0.0f || clip.startSeconds + clip.durationSeconds > kMaxSequenceDurationSeconds)
            return invalid("invalid interval");
        if (clip.kind == PresentationCueKind::Vfx) {
            if (!clip.vfxAsset.IsValid())
                return invalid("missing VFX asset");
        } else if (clip.kind == PresentationCueKind::Audio) {
            // Audio payloads have an explicit project-owned export root.
            const auto& path = clip.soundPath;
            if (!path.starts_with("Assets/Audio/") || path.size() > 1024u || path.find("..") != std::string::npos ||
                path.find(':') != std::string::npos || path.find('\\') != std::string::npos ||
                path.find('\0') != std::string::npos)
                return invalid("sound must be a relative Assets/Audio path");
            const auto& a = clip.audio;
            if (a.bus > GameProjectAudioBus::Voice || !std::isfinite(a.volume) || a.volume < 0.0f || a.volume > 1.0f ||
                a.maximumInstances == 0u || a.maximumInstances > 128u || a.spatial > 1u ||
                !std::isfinite(a.source.minimumDistance) || !std::isfinite(a.source.maximumDistance) ||
                a.source.minimumDistance <= 0.0f || a.source.maximumDistance <= a.source.minimumDistance ||
                (a.spatial && !clip.targetBinding.IsValid()))
                return invalid("invalid audio settings or missing spatial binding");
        } else
            return invalid("unknown cue kind");
    }
    return true;
}
float GetPresentationCueContentEnd(const PresentationCueTrack& track) noexcept {
    float end = 0.0f;
    for (const auto& clip : track.clips)
        if (std::isfinite(clip.startSeconds) && std::isfinite(clip.durationSeconds))
            end = (std::max)(end, clip.startSeconds + clip.durationSeconds);
    return end;
}
} // namespace HIKARI::SEQUENCER
