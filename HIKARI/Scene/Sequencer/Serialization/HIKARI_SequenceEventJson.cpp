#include "HIKARI_SequenceEventJson.h"
#include <stdexcept>

namespace HIKARI::SEQUENCER {
void ReadSequenceEventJson(const nlohmann::json& sequence, SequenceEventTrack& out) {
    SequenceEventTrack candidate;
    bool found = false;
    if (const auto tracks = sequence.find("tracks"); tracks != sequence.end() && tracks->is_array()) {
        for (const auto& track : *tracks) {
            if (!track.is_object() || track.value("type", std::string{}) != "event")
                continue;
            if (found)
                throw std::runtime_error("Multiple event tracks are unsupported");
            found = true;
            const auto& clips = track.at("clips");
            if (!clips.is_array() || clips.size() > kMaximumSequenceEvents)
                throw std::runtime_error("Invalid event clip list");
            for (const auto& node : clips) {
                SequenceEventClip clip;
                clip.id = node.at("id").get<uint64_t>();
                clip.name = node.at("name").get<std::string>();
                clip.startSeconds = node.at("startSeconds").get<float>();
                clip.durationSeconds = node.at("durationSeconds").get<float>();
                candidate.clips.push_back(std::move(clip));
            }
        }
    }
    std::string error;
    if (!ValidateSequenceEventTrack(candidate, error))
        throw std::runtime_error(error);
    out = std::move(candidate);
}

void WriteSequenceEventJson(const SequenceEventTrack& track, nlohmann::json& tracks) {
    if (track.clips.empty())
        return;
    nlohmann::json result{{"type", "event"}, {"clips", nlohmann::json::array()}};
    for (const auto& clip : track.clips)
        result["clips"].push_back({{"id", clip.id},
                                   {"name", clip.name},
                                   {"startSeconds", clip.startSeconds},
                                   {"durationSeconds", clip.durationSeconds}});
    tracks.push_back(std::move(result));
}
} // namespace HIKARI::SEQUENCER
