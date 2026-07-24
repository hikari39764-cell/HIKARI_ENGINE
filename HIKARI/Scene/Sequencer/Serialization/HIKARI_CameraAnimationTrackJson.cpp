#include "Scene/Sequencer/Serialization/HIKARI_CameraAnimationTrackJson.h"

#include <string>
#include <utility>

#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {
        using nlohmann::json;

        const char* ToString(
            SEQUENCER::SequenceInterpolationMode mode) noexcept {

            switch (mode) {
            case SEQUENCER::SequenceInterpolationMode::Hold:
                return "hold";
            case SEQUENCER::SequenceInterpolationMode::Linear:
                return "linear";
            default:
                return "smooth";
            }
        }

        SEQUENCER::SequenceInterpolationMode InterpolationFromString(
            const std::string& value) noexcept {

            if (value == "hold") {
                return SEQUENCER::SequenceInterpolationMode::Hold;
            }
            if (value == "linear") {
                return SEQUENCER::SequenceInterpolationMode::Linear;
            }
            return SEQUENCER::SequenceInterpolationMode::Smooth;
        }

        MATH::Vec3 Vec3Or(
            const json& value,
            const MATH::Vec3& fallback) {

            if (value.is_array() && value.size() >= 3 &&
                value[0].is_number() && value[1].is_number() &&
                value[2].is_number()) {
                return {
                    value[0].get<float>(),
                    value[1].get<float>(),
                    value[2].get<float>()
                };
            }
            if (value.is_object()) {
                return {
                    value.value("x", fallback.x),
                    value.value("y", fallback.y),
                    value.value("z", fallback.z)
                };
            }
            return fallback;
        }

        void DeserializeTransformTrack(
            const json& trackNode,
            CinematicSequence& sequence) {

            if (const auto idIt = trackNode.find("id");
                idIt != trackNode.end() && idIt->is_number_unsigned()) {
                sequence.cameraTransformTrack.id.value =
                    idIt->get<uint64_t>();
            }
            sequence.cameraTransformTrack.enabled =
                trackNode.value("enabled", true);
            const auto channelsIt = trackNode.find("channels");
            if (channelsIt == trackNode.end() || !channelsIt->is_array()) {
                return;
            }
            for (const json& channelNode : *channelsIt) {
                if (!channelNode.is_object()) {
                    continue;
                }
                SEQUENCER::CameraTransformChannel channel{};
                if (const auto bindingIt =
                        channelNode.find("cameraBindingId");
                    bindingIt != channelNode.end() &&
                    bindingIt->is_number_unsigned()) {
                    channel.cameraBindingId.value =
                        bindingIt->get<uint64_t>();
                }
                const auto keyframesIt = channelNode.find("keyframes");
                if (keyframesIt != channelNode.end() &&
                    keyframesIt->is_array()) {
                    for (const json& keyNode : *keyframesIt) {
                        if (!keyNode.is_object()) {
                            continue;
                        }
                        SEQUENCER::CameraTransformKeyframe keyframe{};
                        if (const auto idIt = keyNode.find("id");
                            idIt != keyNode.end() &&
                            idIt->is_number_unsigned()) {
                            keyframe.id = idIt->get<uint64_t>();
                        }
                        keyframe.timeSeconds =
                            keyNode.value("timeSeconds", 0.0f);
                        if (const auto positionIt = keyNode.find("position");
                            positionIt != keyNode.end()) {
                            keyframe.position = Vec3Or(
                                *positionIt,
                                keyframe.position);
                        }
                        if (const auto rotationIt =
                                keyNode.find("rotationEulerDeg");
                            rotationIt != keyNode.end()) {
                            keyframe.rotationEulerDeg = Vec3Or(
                                *rotationIt,
                                keyframe.rotationEulerDeg);
                        }
                        keyframe.interpolation = InterpolationFromString(
                            keyNode.value(
                                "interpolation",
                                std::string("smooth")));
                        channel.keyframes.push_back(keyframe);
                    }
                }
                sequence.cameraTransformTrack.channels.push_back(
                    std::move(channel));
            }
        }

        void DeserializeLensTrack(
            const json& trackNode,
            CinematicSequence& sequence) {

            if (const auto idIt = trackNode.find("id");
                idIt != trackNode.end() && idIt->is_number_unsigned()) {
                sequence.cameraLensTrack.id.value = idIt->get<uint64_t>();
            }
            sequence.cameraLensTrack.enabled =
                trackNode.value("enabled", true);
            const auto channelsIt = trackNode.find("channels");
            if (channelsIt == trackNode.end() || !channelsIt->is_array()) {
                return;
            }
            for (const json& channelNode : *channelsIt) {
                if (!channelNode.is_object()) {
                    continue;
                }
                SEQUENCER::CameraLensChannel channel{};
                if (const auto bindingIt =
                        channelNode.find("cameraBindingId");
                    bindingIt != channelNode.end() &&
                    bindingIt->is_number_unsigned()) {
                    channel.cameraBindingId.value =
                        bindingIt->get<uint64_t>();
                }
                const auto keyframesIt = channelNode.find("keyframes");
                if (keyframesIt != channelNode.end() &&
                    keyframesIt->is_array()) {
                    for (const json& keyNode : *keyframesIt) {
                        if (!keyNode.is_object()) {
                            continue;
                        }
                        SEQUENCER::CameraLensKeyframe keyframe{};
                        if (const auto idIt = keyNode.find("id");
                            idIt != keyNode.end() &&
                            idIt->is_number_unsigned()) {
                            keyframe.id = idIt->get<uint64_t>();
                        }
                        keyframe.timeSeconds =
                            keyNode.value("timeSeconds", 0.0f);
                        keyframe.verticalFovDegrees = keyNode.value(
                            "verticalFovDegrees",
                            60.0f);
                        keyframe.nearClip = keyNode.value(
                            "nearClip",
                            0.1f);
                        keyframe.farClip = keyNode.value(
                            "farClip",
                            100.0f);
                        keyframe.interpolation = InterpolationFromString(
                            keyNode.value(
                                "interpolation",
                                std::string("smooth")));
                        channel.keyframes.push_back(keyframe);
                    }
                }
                sequence.cameraLensTrack.channels.push_back(
                    std::move(channel));
            }
        }

        json SerializeTransformTrack(const CinematicSequence& sequence) {
            json trackNode = {
                { "id", sequence.cameraTransformTrack.id.value },
                { "type", "cameraTransform" },
                { "enabled", sequence.cameraTransformTrack.enabled },
                { "channels", json::array() }
            };
            for (const SEQUENCER::CameraTransformChannel& channel :
                    sequence.cameraTransformTrack.channels) {
                json channelNode = {
                    { "cameraBindingId", channel.cameraBindingId.value },
                    { "keyframes", json::array() }
                };
                for (const SEQUENCER::CameraTransformKeyframe& keyframe :
                        channel.keyframes) {
                    channelNode["keyframes"].push_back({
                        { "id", keyframe.id },
                        { "timeSeconds", keyframe.timeSeconds },
                        { "position",
                            JsonMath::ToJsonArray(keyframe.position) },
                        { "rotationEulerDeg",
                            JsonMath::ToJsonArray(
                                keyframe.rotationEulerDeg) },
                        { "interpolation", ToString(keyframe.interpolation) }
                    });
                }
                trackNode["channels"].push_back(std::move(channelNode));
            }
            return trackNode;
        }

        json SerializeLensTrack(const CinematicSequence& sequence) {
            json trackNode = {
                { "id", sequence.cameraLensTrack.id.value },
                { "type", "cameraLens" },
                { "enabled", sequence.cameraLensTrack.enabled },
                { "channels", json::array() }
            };
            for (const SEQUENCER::CameraLensChannel& channel :
                    sequence.cameraLensTrack.channels) {
                json channelNode = {
                    { "cameraBindingId", channel.cameraBindingId.value },
                    { "keyframes", json::array() }
                };
                for (const SEQUENCER::CameraLensKeyframe& keyframe :
                        channel.keyframes) {
                    channelNode["keyframes"].push_back({
                        { "id", keyframe.id },
                        { "timeSeconds", keyframe.timeSeconds },
                        { "verticalFovDegrees",
                            keyframe.verticalFovDegrees },
                        { "nearClip", keyframe.nearClip },
                        { "farClip", keyframe.farClip },
                        { "interpolation", ToString(keyframe.interpolation) }
                    });
                }
                trackNode["channels"].push_back(std::move(channelNode));
            }
            return trackNode;
        }
    }

    void DeserializeCameraAnimationTracksJson(
        const nlohmann::json& sequenceNode,
        CinematicSequence& sequence) {

        const auto tracksIt = sequenceNode.find("tracks");
        if (tracksIt == sequenceNode.end() || !tracksIt->is_array()) {
            return;
        }
        for (const nlohmann::json& trackNode : *tracksIt) {
            if (!trackNode.is_object()) {
                continue;
            }
            const std::string type = trackNode.value(
                "type",
                std::string{});
            if (type == "cameraTransform") {
                DeserializeTransformTrack(trackNode, sequence);
            } else if (type == "cameraLens") {
                DeserializeLensTrack(trackNode, sequence);
            }
        }
    }

    void SerializeCameraAnimationTracksJson(
        const CinematicSequence& sequence,
        nlohmann::json& tracksNode) {

        if (!tracksNode.is_array()) {
            tracksNode = nlohmann::json::array();
        }
        tracksNode.push_back(SerializeTransformTrack(sequence));
        tracksNode.push_back(SerializeLensTrack(sequence));
    }

} // namespace HIKARI
