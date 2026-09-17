#include "HIKARI_PresentationCueJson.h"
#include <json.hpp>
#include <stdexcept>

namespace HIKARI::SEQUENCER {
    void ReadPresentationCueJson(const nlohmann::json& sequence, PresentationCueTrack& out) {
        PresentationCueTrack candidate;
        bool found = false;
        if (const auto tracks = sequence.find("tracks"); tracks != sequence.end() && tracks->is_array()) {
            for (const auto& track : *tracks) {
                if (!track.is_object() || track.value("type", std::string{}) != "presentationCue") continue;
                if (found) throw std::runtime_error("Multiple presentation cue tracks are unsupported");
                found = true;
                candidate.enabled = track.value("enabled", true);
                const auto& clips = track.at("clips");
                if (!clips.is_array() || clips.size() > kMaximumPresentationCues)
                    throw std::runtime_error("Invalid presentation cue list");
                for (const auto& node : clips) {
                    PresentationCueClip clip;
                    clip.id = node.at("id").get<uint64_t>();
                    clip.name = node.value("name", "Cue");
                    const auto kind = node.at("kind").get<std::string>();
                    if (kind != "audio" && kind != "vfx") throw std::runtime_error("Unknown presentation cue kind");
                    clip.kind = kind == "audio" ? PresentationCueKind::Audio : PresentationCueKind::Vfx;
                    clip.startSeconds = node.at("startSeconds").get<float>();
                    clip.durationSeconds = node.at("durationSeconds").get<float>();
                    clip.targetBinding.value = node.value("targetBinding", uint64_t{});
                    clip.soundPath = node.value("soundPath", std::string{});
                    clip.vfxAsset.value = node.value("vfxAsset", std::string{});
                    clip.audio.volume = node.value("volume", 1.0f);
                    const auto bus = node.value("bus", std::string("Se"));
                    if (bus != "Se" && bus != "Bgm" && bus != "Voice") throw std::runtime_error("Unknown audio bus");
                    clip.audio.bus = bus == "Bgm" ? GameProjectAudioBus::Bgm :
                        bus == "Voice" ? GameProjectAudioBus::Voice : GameProjectAudioBus::Se;
                    clip.audio.spatial = node.value("spatial", false) ? 1u : 0u;
                    clip.audio.maximumInstances = node.value("maximumInstances", 8u);
                    clip.audio.priority = node.value("priority", 0);
                    clip.audio.source.minimumDistance = node.value("minimumDistance", 1.0f);
                    clip.audio.source.maximumDistance = node.value("maximumDistance", 30.0f);
                    candidate.clips.push_back(std::move(clip));
                }
            }
        }
        std::string error;
        if (!ValidatePresentationCueTrack(candidate, error)) throw std::runtime_error(error);
        out = std::move(candidate);
    }
    void WritePresentationCueJson(const PresentationCueTrack& track, nlohmann::json& tracks) {
        if (track.clips.empty()) return;
        nlohmann::json result{{"type", "presentationCue"}, {"enabled", track.enabled}, {"clips", nlohmann::json::array()}};
        for (const auto& clip : track.clips) {
            result["clips"].push_back({{"id", clip.id}, {"name", clip.name},
                {"kind", clip.kind == PresentationCueKind::Audio ? "audio" : "vfx"},
                {"startSeconds", clip.startSeconds}, {"durationSeconds", clip.durationSeconds},
                {"targetBinding", clip.targetBinding.value}, {"soundPath", clip.soundPath},
                {"vfxAsset", clip.vfxAsset.value}, {"volume", clip.audio.volume},
                {"bus", clip.audio.bus == GameProjectAudioBus::Bgm ? "Bgm" : clip.audio.bus == GameProjectAudioBus::Voice ? "Voice" : "Se"},
                {"spatial", clip.audio.spatial != 0u}, {"maximumInstances", clip.audio.maximumInstances},
                {"priority", clip.audio.priority}, {"minimumDistance", clip.audio.source.minimumDistance},
                {"maximumDistance", clip.audio.source.maximumDistance}});
        }
        tracks.push_back(std::move(result));
    }
}
