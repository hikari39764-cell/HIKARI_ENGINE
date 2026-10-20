#include "HIKARI_SequenceEventTrack.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Scene/Sequencer/HIKARI_SequenceTrack.h"

namespace HIKARI::SEQUENCER {
bool ValidateSequenceEventTrack(const SequenceEventTrack& track, std::string& error) {
    error.clear();
    if (track.clips.size() > kMaximumSequenceEvents) {
        error = "Event track exceeds 128 clips";
        return false;
    }
    std::unordered_set<uint64_t> ids;
    for (const auto& clip : track.clips) {
        const auto invalid = [&](const char* message) {
            error = "Event " + std::to_string(clip.id) + ": " + message;
            return false;
        };
        if (clip.id == 0u || !ids.insert(clip.id).second)
            return invalid("duplicate or empty id");
        if (clip.name.empty() || clip.name.size() > kMaximumSequenceEventNameBytes ||
            clip.name.find('\0') != std::string::npos)
            return invalid("name must be 1..63 bytes");
        if (!std::isfinite(clip.startSeconds) || !std::isfinite(clip.durationSeconds) || clip.startSeconds < 0.0f ||
            clip.durationSeconds <= 0.0f || clip.startSeconds + clip.durationSeconds > kMaxSequenceDurationSeconds)
            return invalid("invalid interval");
    }
    return true;
}

float GetSequenceEventContentEnd(const SequenceEventTrack& track) noexcept {
    float end = 0.0f;
    for (const auto& clip : track.clips)
        if (std::isfinite(clip.startSeconds) && std::isfinite(clip.durationSeconds))
            end = (std::max)(end, clip.startSeconds + clip.durationSeconds);
    return end;
}
} // namespace HIKARI::SEQUENCER
