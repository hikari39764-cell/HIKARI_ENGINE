#include "HIKARI_SequenceTextJson.h"
#include <stdexcept>

namespace HIKARI::SEQUENCER {
namespace {
template <class E, class Parse>
E ReadName(const nlohmann::json& node, const char* key, E fallback, Parse parse) {
    const auto found = node.find(key);
    if (found == node.end())
        return fallback;
    E value = fallback;
    if (!parse(found->get<std::string>(), value))
        throw std::runtime_error(std::string("Unknown text clip ") + key);
    return value;
}
} // namespace

void ReadSequenceTextJson(const nlohmann::json& sequence, SequenceTextTrack& out) {
    SequenceTextTrack candidate;
    bool found = false;
    if (const auto tracks = sequence.find("tracks"); tracks != sequence.end() && tracks->is_array()) {
        for (const auto& track : *tracks) {
            if (!track.is_object() || track.value("type", std::string{}) != "text")
                continue;
            if (found)
                throw std::runtime_error("Multiple text tracks are unsupported");
            found = true;
            const auto& clips = track.at("clips");
            if (!clips.is_array() || clips.size() > kMaximumSequenceTexts)
                throw std::runtime_error("Invalid text clip list");
            for (const auto& node : clips) {
                SequenceTextClip clip;
                clip.id = node.at("id").get<uint64_t>();
                clip.text = node.at("text").get<std::string>();
                clip.tag = node.value("tag", std::string{});
                clip.startSeconds = node.at("startSeconds").get<float>();
                clip.durationSeconds = node.at("durationSeconds").get<float>();
                clip.style.value = node.value("style", std::string{});
                clip.layout = ReadName(node, "layout", clip.layout, ParseSequenceTextLayout);
                clip.alignX = ReadName(node, "alignX", clip.alignX, ParseSequenceTextAlign);
                clip.alignY = ReadName(node, "alignY", clip.alignY, ParseSequenceTextAlign);
                clip.x = node.value("x", clip.x);
                clip.y = node.value("y", clip.y);
                clip.width = node.value("width", clip.width);
                clip.columnSpacing = node.value("columnSpacing", clip.columnSpacing);
                clip.scale = node.value("scale", clip.scale);
                if (const auto tint = node.find("tint"); tint != node.end()) {
                    if (!tint->is_array() || tint->size() != 4u)
                        throw std::runtime_error("Text clip tint must be [r, g, b, a]");
                    clip.tint = {(*tint)[0].get<float>(), (*tint)[1].get<float>(), (*tint)[2].get<float>(),
                                 (*tint)[3].get<float>()};
                }
                clip.reveal = ReadName(node, "reveal", clip.reveal, ParseSequenceTextReveal);
                clip.revealSeconds = node.value("revealSeconds", clip.revealSeconds);
                clip.fadeInSeconds = node.value("fadeInSeconds", clip.fadeInSeconds);
                clip.fadeOutSeconds = node.value("fadeOutSeconds", clip.fadeOutSeconds);
                clip.order = node.value("order", clip.order);
                candidate.clips.push_back(std::move(clip));
            }
        }
    }
    std::string error;
    if (!ValidateSequenceTextTrack(candidate, error))
        throw std::runtime_error(error);
    out = std::move(candidate);
}

void WriteSequenceTextJson(const SequenceTextTrack& track, nlohmann::json& tracks) {
    if (track.clips.empty())
        return;
    nlohmann::json result{{"type", "text"}, {"clips", nlohmann::json::array()}};
    for (const auto& clip : track.clips)
        result["clips"].push_back({{"id", clip.id},
                                   {"text", clip.text},
                                   {"tag", clip.tag},
                                   {"startSeconds", clip.startSeconds},
                                   {"durationSeconds", clip.durationSeconds},
                                   {"style", clip.style.value},
                                   {"layout", ToString(clip.layout)},
                                   {"alignX", ToString(clip.alignX)},
                                   {"alignY", ToString(clip.alignY)},
                                   {"x", clip.x},
                                   {"y", clip.y},
                                   {"width", clip.width},
                                   {"columnSpacing", clip.columnSpacing},
                                   {"scale", clip.scale},
                                   {"tint", {clip.tint.r, clip.tint.g, clip.tint.b, clip.tint.a}},
                                   {"reveal", ToString(clip.reveal)},
                                   {"revealSeconds", clip.revealSeconds},
                                   {"fadeInSeconds", clip.fadeInSeconds},
                                   {"fadeOutSeconds", clip.fadeOutSeconds},
                                   {"order", clip.order}});
    tracks.push_back(std::move(result));
}
} // namespace HIKARI::SEQUENCER
