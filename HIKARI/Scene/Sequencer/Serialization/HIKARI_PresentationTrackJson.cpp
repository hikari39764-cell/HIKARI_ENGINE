#include "Scene/Sequencer/Serialization/HIKARI_PresentationTrackJson.h"

#include <string>
#include <utility>

namespace HIKARI::SEQUENCER {

    namespace {
        using nlohmann::json;

        MATH::Vec3 ReadVec3(
            const json& node,
            const char* key,
            const MATH::Vec3& fallback = {}) {

            const auto it = node.find(key);
            if (it == node.end() || !it->is_array() || it->size() != 3) {
                return fallback;
            }
            MATH::Vec3 value = fallback;
            if ((*it)[0].is_number()) value.x = (*it)[0].get<float>();
            if ((*it)[1].is_number()) value.y = (*it)[1].get<float>();
            if ((*it)[2].is_number()) value.z = (*it)[2].get<float>();
            return value;
        }

        json WriteVec3(const MATH::Vec3& value) {
            return json::array({ value.x, value.y, value.z });
        }

        SequenceBindingId ReadBindingId(
            const json& node,
            const char* key) noexcept {

            SequenceBindingId result{};
            const auto it = node.find(key);
            if (it != node.end() && it->is_number_unsigned()) {
                result.value = it->get<uint64_t>();
            }
            return result;
        }

        uint64_t ReadId(const json& node) noexcept {
            const auto it = node.find("id");
            return it != node.end() && it->is_number_unsigned()
                ? it->get<uint64_t>()
                : 0;
        }

        void ReadActorTrack(
            const json& trackNode,
            ActorAnimationTrack& track) {

            track.id.value = trackNode.value("id", track.id.value);
            track.enabled = trackNode.value("enabled", true);
            const auto clipsIt = trackNode.find("clips");
            if (clipsIt == trackNode.end() || !clipsIt->is_array()) {
                return;
            }
            for (const json& node : *clipsIt) {
                if (!node.is_object()) continue;
                ActorAnimationClip clip{};
                clip.id = ReadId(node);
                clip.actorBindingId = ReadBindingId(node, "actorBindingId");
                clip.clipName = node.value("clipName", std::string{});
                clip.startTimeSeconds = node.value(
                    "startTimeSeconds", clip.startTimeSeconds);
                clip.durationSeconds = node.value(
                    "durationSeconds", clip.durationSeconds);
                clip.sourceStartTimeSeconds = node.value(
                    "sourceStartTimeSeconds", clip.sourceStartTimeSeconds);
                clip.playbackRate = node.value(
                    "playbackRate", clip.playbackRate);
                clip.blendInSeconds = node.value(
                    "blendInSeconds", clip.blendInSeconds);
                clip.loop = node.value("loop", clip.loop);
                track.clips.push_back(std::move(clip));
            }
        }

        void ReadVfxTrack(
            const json& trackNode,
            SequenceVfxTrack& track) {

            track.id.value = trackNode.value("id", track.id.value);
            track.enabled = trackNode.value("enabled", true);
            const auto cuesIt = trackNode.find("cues");
            if (cuesIt == trackNode.end() || !cuesIt->is_array()) return;
            for (const json& node : *cuesIt) {
                if (!node.is_object()) continue;
                SequenceVfxCue cue{};
                cue.id = ReadId(node);
                cue.ownerBindingId = ReadBindingId(node, "ownerBindingId");
                cue.assetGuid.value = node.value(
                    "assetGuid", std::string{});
                cue.timeSeconds = node.value(
                    "timeSeconds", cue.timeSeconds);
                cue.positionOffset = ReadVec3(
                    node, "positionOffset", cue.positionOffset);
                cue.rotationEulerDeg = ReadVec3(
                    node, "rotationEulerDeg", cue.rotationEulerDeg);
                cue.scale = node.value("scale", cue.scale);
                cue.randomSeed = node.value("randomSeed", cue.randomSeed);
                cue.fireOnSeek = node.value("fireOnSeek", cue.fireOnSeek);
                track.cues.push_back(std::move(cue));
            }
        }

        void ReadTimeTrack(
            const json& trackNode,
            SequenceTimeDilationTrack& track) {

            track.id.value = trackNode.value("id", track.id.value);
            track.enabled = trackNode.value("enabled", true);
            const auto clipsIt = trackNode.find("clips");
            if (clipsIt == trackNode.end() || !clipsIt->is_array()) return;
            for (const json& node : *clipsIt) {
                if (!node.is_object()) continue;
                SequenceTimeDilationClip clip{};
                clip.id = ReadId(node);
                clip.startTimeSeconds = node.value(
                    "startTimeSeconds", clip.startTimeSeconds);
                clip.durationSeconds = node.value(
                    "durationSeconds", clip.durationSeconds);
                clip.scale = node.value("scale", clip.scale);
                clip.priority = node.value("priority", clip.priority);
                track.clips.push_back(clip);
            }
        }

        void ReadSignalTrack(
            const json& trackNode,
            SequenceSignalTrack& track) {

            track.id.value = trackNode.value("id", track.id.value);
            track.enabled = trackNode.value("enabled", true);
            const auto cuesIt = trackNode.find("cues");
            if (cuesIt == trackNode.end() || !cuesIt->is_array()) return;
            for (const json& node : *cuesIt) {
                if (!node.is_object()) continue;
                SequenceSignalCue cue{};
                cue.id = ReadId(node);
                cue.targetBindingId = ReadBindingId(
                    node, "targetBindingId");
                cue.name = node.value("name", std::string{});
                cue.payload = node.value("payload", std::string{});
                cue.timeSeconds = node.value(
                    "timeSeconds", cue.timeSeconds);
                cue.fireOnSeek = node.value("fireOnSeek", cue.fireOnSeek);
                track.cues.push_back(std::move(cue));
            }
        }

        json MakeTrackNode(uint64_t id, const char* type, bool enabled) {
            return {
                { "id", id },
                { "type", type },
                { "enabled", enabled }
            };
        }
    }

    void DeserializePresentationTracksJson(
        const nlohmann::json& sequenceNode,
        CinematicSequence& sequence) {

        const auto tracksIt = sequenceNode.find("tracks");
        if (tracksIt == sequenceNode.end() || !tracksIt->is_array()) {
            return;
        }
        for (const json& trackNode : *tracksIt) {
            if (!trackNode.is_object()) continue;
            const std::string type = trackNode.value(
                "type", std::string{});
            if (type == "actorAnimation") {
                ReadActorTrack(trackNode, sequence.actorAnimationTrack);
            } else if (type == "vfx") {
                ReadVfxTrack(trackNode, sequence.vfxTrack);
            } else if (type == "timeDilation") {
                ReadTimeTrack(trackNode, sequence.timeDilationTrack);
            } else if (type == "signal") {
                ReadSignalTrack(trackNode, sequence.signalTrack);
            }
        }
    }

    void SerializePresentationTracksJson(
        const CinematicSequence& sequence,
        nlohmann::json& tracksNode) {

        json actor = MakeTrackNode(
            sequence.actorAnimationTrack.id.value,
            "actorAnimation",
            sequence.actorAnimationTrack.enabled);
        actor["clips"] = json::array();
        for (const ActorAnimationClip& clip :
                sequence.actorAnimationTrack.clips) {
            actor["clips"].push_back({
                { "id", clip.id },
                { "actorBindingId", clip.actorBindingId.value },
                { "clipName", clip.clipName },
                { "startTimeSeconds", clip.startTimeSeconds },
                { "durationSeconds", clip.durationSeconds },
                { "sourceStartTimeSeconds", clip.sourceStartTimeSeconds },
                { "playbackRate", clip.playbackRate },
                { "blendInSeconds", clip.blendInSeconds },
                { "loop", clip.loop }
            });
        }
        tracksNode.push_back(std::move(actor));

        json vfx = MakeTrackNode(
            sequence.vfxTrack.id.value,
            "vfx",
            sequence.vfxTrack.enabled);
        vfx["cues"] = json::array();
        for (const SequenceVfxCue& cue : sequence.vfxTrack.cues) {
            vfx["cues"].push_back({
                { "id", cue.id },
                { "ownerBindingId", cue.ownerBindingId.value },
                { "assetGuid", cue.assetGuid.value },
                { "timeSeconds", cue.timeSeconds },
                { "positionOffset", WriteVec3(cue.positionOffset) },
                { "rotationEulerDeg", WriteVec3(cue.rotationEulerDeg) },
                { "scale", cue.scale },
                { "randomSeed", cue.randomSeed },
                { "fireOnSeek", cue.fireOnSeek }
            });
        }
        tracksNode.push_back(std::move(vfx));

        json time = MakeTrackNode(
            sequence.timeDilationTrack.id.value,
            "timeDilation",
            sequence.timeDilationTrack.enabled);
        time["clips"] = json::array();
        for (const SequenceTimeDilationClip& clip :
                sequence.timeDilationTrack.clips) {
            time["clips"].push_back({
                { "id", clip.id },
                { "startTimeSeconds", clip.startTimeSeconds },
                { "durationSeconds", clip.durationSeconds },
                { "scale", clip.scale },
                { "priority", clip.priority }
            });
        }
        tracksNode.push_back(std::move(time));

        json signals = MakeTrackNode(
            sequence.signalTrack.id.value,
            "signal",
            sequence.signalTrack.enabled);
        signals["cues"] = json::array();
        for (const SequenceSignalCue& cue : sequence.signalTrack.cues) {
            signals["cues"].push_back({
                { "id", cue.id },
                { "targetBindingId", cue.targetBindingId.value },
                { "name", cue.name },
                { "payload", cue.payload },
                { "timeSeconds", cue.timeSeconds },
                { "fireOnSeek", cue.fireOnSeek }
            });
        }
        tracksNode.push_back(std::move(signals));
    }

} // namespace HIKARI::SEQUENCER
