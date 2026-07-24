#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"

#include <string>
#include <utility>

#include "Scene/Sequencer/Serialization/HIKARI_CameraAnimationTrackJson.h"

namespace HIKARI {

    namespace {
        using nlohmann::json;

        const char* ToString(
            SEQUENCER::CameraCutTransitionMode mode) noexcept {

            return mode == SEQUENCER::CameraCutTransitionMode::EaseInOut
                ? "easeInOut"
                : "cut";
        }

        SEQUENCER::CameraCutTransitionMode CameraTransitionModeFromString(
            const std::string& value) noexcept {

            return value == "easeInOut" || value == "blend"
                ? SEQUENCER::CameraCutTransitionMode::EaseInOut
                : SEQUENCER::CameraCutTransitionMode::Cut;
        }

        void DeserializeBindings(
            const json& sequenceNode,
            SEQUENCER::SequenceBindingTable& bindings) {

            const auto bindingsIt = sequenceNode.find("bindings");
            if (bindingsIt == sequenceNode.end() ||
                !bindingsIt->is_array()) {
                return;
            }

            for (const json& bindingNode : *bindingsIt) {
                if (!bindingNode.is_object()) {
                    continue;
                }
                SEQUENCER::SequenceBinding binding{};
                if (const auto idIt = bindingNode.find("id");
                    idIt != bindingNode.end() &&
                    idIt->is_number_unsigned()) {
                    binding.id.value = idIt->get<uint64_t>();
                }
                if (const auto nameIt = bindingNode.find("name");
                    nameIt != bindingNode.end() && nameIt->is_string()) {
                    binding.name = nameIt->get<std::string>();
                }
                const std::string targetKind = bindingNode.value(
                    "targetKind",
                    std::string{ "sceneObject" });
                if (targetKind == "slot") {
                    binding.targetKind =
                        SEQUENCER::SequenceBindingTargetKind::Slot;
                    binding.slotName = bindingNode.value(
                        "slotName",
                        binding.name);
                }
                if (const auto objectIt = bindingNode.find("sceneObjectId");
                    objectIt != bindingNode.end() &&
                    objectIt->is_number_unsigned()) {
                    binding.sceneObjectId.value =
                        objectIt->get<uint64_t>();
                }
                binding.required = bindingNode.value("required", true);
                bindings.push_back(std::move(binding));
            }
            SEQUENCER::NormalizeSequenceBindings(bindings);
        }

        SEQUENCER::CameraCutClip DeserializeCameraCutClip(
            const json& clipNode,
            CinematicSequence& sequence,
            bool legacyShot) {

            SEQUENCER::CameraCutClip clip{};
            if (const auto idIt = clipNode.find("id");
                idIt != clipNode.end() && idIt->is_number_unsigned()) {
                clip.id = idIt->get<uint64_t>();
            }
            if (const auto bindingIt = clipNode.find("cameraBindingId");
                bindingIt != clipNode.end() &&
                bindingIt->is_number_unsigned()) {
                clip.cameraBindingId.value = bindingIt->get<uint64_t>();
            }
            if (legacyShot || !clip.cameraBindingId.IsValid()) {
                if (const auto cameraIt = clipNode.find("cameraObjectId");
                    cameraIt != clipNode.end() &&
                    cameraIt->is_number_unsigned()) {
                    const SceneObjectId cameraObjectId{
                        cameraIt->get<uint64_t>()
                    };
                    clip.cameraBindingId =
                        SEQUENCER::FindOrCreateSceneObjectBinding(
                            sequence.bindings,
                            cameraObjectId);
                }
            }
            if (const auto startIt = clipNode.find("startTimeSeconds");
                startIt != clipNode.end() && startIt->is_number()) {
                clip.startTimeSeconds = startIt->get<float>();
            }
            if (const auto durationIt = clipNode.find("durationSeconds");
                durationIt != clipNode.end() && durationIt->is_number()) {
                clip.durationSeconds = durationIt->get<float>();
            }
            if (const auto transitionIt = clipNode.find("transition");
                transitionIt != clipNode.end() &&
                transitionIt->is_object()) {
                if (const auto modeIt = transitionIt->find("mode");
                    modeIt != transitionIt->end() && modeIt->is_string()) {
                    clip.transition.mode = CameraTransitionModeFromString(
                        modeIt->get<std::string>());
                }
                if (const auto durationIt =
                        transitionIt->find("durationSeconds");
                    durationIt != transitionIt->end() &&
                    durationIt->is_number()) {
                    clip.transition.durationSeconds =
                        durationIt->get<float>();
                }
            }
            return clip;
        }

        bool DeserializeCameraCutTrack(
            const json& sequenceNode,
            CinematicSequence& sequence) {

            const auto tracksIt = sequenceNode.find("tracks");
            if (tracksIt == sequenceNode.end() || !tracksIt->is_array()) {
                return false;
            }
            for (const json& trackNode : *tracksIt) {
                if (!trackNode.is_object() ||
                    trackNode.value("type", std::string{}) != "cameraCut") {
                    continue;
                }
                if (const auto idIt = trackNode.find("id");
                    idIt != trackNode.end() &&
                    idIt->is_number_unsigned()) {
                    sequence.cameraCutTrack.id.value = idIt->get<uint64_t>();
                }
                sequence.cameraCutTrack.enabled =
                    trackNode.value("enabled", true);
                const auto clipsIt = trackNode.find("clips");
                if (clipsIt != trackNode.end() && clipsIt->is_array()) {
                    for (const json& clipNode : *clipsIt) {
                        if (clipNode.is_object()) {
                            sequence.cameraCutTrack.clips.push_back(
                                DeserializeCameraCutClip(
                                    clipNode,
                                    sequence,
                                    false));
                        }
                    }
                }
                return true;
            }
            return false;
        }

        void DeserializeLegacyShots(
            const json& sequenceNode,
            CinematicSequence& sequence) {

            const auto shotsIt = sequenceNode.find("shots");
            if (shotsIt == sequenceNode.end() || !shotsIt->is_array()) {
                return;
            }
            for (const json& shotNode : *shotsIt) {
                if (shotNode.is_object()) {
                    sequence.cameraCutTrack.clips.push_back(
                        DeserializeCameraCutClip(
                            shotNode,
                            sequence,
                            true));
                }
            }
        }

        CinematicSequence DeserializeSequence(
            const json& sequenceNode,
            CinematicSequenceId fallbackId,
            std::string fallbackName) {

            CinematicSequence sequence{};
            sequence.id = fallbackId;
            sequence.name = std::move(fallbackName);
            if (const auto idIt = sequenceNode.find("id");
                idIt != sequenceNode.end() && idIt->is_number_unsigned()) {
                sequence.id.value = idIt->get<uint64_t>();
            }
            if (const auto nameIt = sequenceNode.find("name");
                nameIt != sequenceNode.end() && nameIt->is_string()) {
                sequence.name = nameIt->get<std::string>();
            }
            if (const auto durationIt = sequenceNode.find("durationSeconds");
                durationIt != sequenceNode.end() && durationIt->is_number()) {
                sequence.durationSeconds = durationIt->get<float>();
            }

            DeserializeBindings(sequenceNode, sequence.bindings);
            if (!DeserializeCameraCutTrack(sequenceNode, sequence)) {
                DeserializeLegacyShots(sequenceNode, sequence);
            }
            DeserializeCameraAnimationTracksJson(sequenceNode, sequence);
            NormalizeCinematicSequence(sequence);
            return sequence;
        }

        json SerializeBinding(
            const SEQUENCER::SequenceBinding& binding) {

            json output = {
                { "id", binding.id.value },
                { "name", binding.name },
                { "required", binding.required },
                { "targetKind", binding.targetKind ==
                        SEQUENCER::SequenceBindingTargetKind::Slot
                    ? "slot"
                    : "sceneObject" }
            };
            if (binding.targetKind ==
                    SEQUENCER::SequenceBindingTargetKind::Slot) {
                output["slotName"] = binding.slotName;
            } else {
                output["sceneObjectId"] = binding.sceneObjectId.value;
            }
            return output;
        }

        json SerializeCameraCutClip(
            const SEQUENCER::CameraCutClip& clip) {

            return {
                { "id", clip.id },
                { "cameraBindingId", clip.cameraBindingId.value },
                { "startTimeSeconds", clip.startTimeSeconds },
                { "durationSeconds", clip.durationSeconds },
                { "transition", {
                    { "mode", ToString(clip.transition.mode) },
                    { "durationSeconds", clip.transition.durationSeconds }
                } }
            };
        }
    }

    void DeserializeSceneCinematicsJson(
        const nlohmann::json& input,
        SceneCinematicsSettings& settings) {

        if (!input.is_object()) {
            NormalizeSceneCinematicsSettings(settings);
            return;
        }

        settings.sequences.clear();
        if (const auto defaultIt = input.find("defaultSequenceId");
            defaultIt != input.end() && defaultIt->is_number_unsigned()) {
            settings.defaultSequenceId.value = defaultIt->get<uint64_t>();
        }

        auto sequencesIt = input.find("sequences");
        if (sequencesIt == input.end() || !sequencesIt->is_array()) {
            sequencesIt = input.find("cameraSequences");
        }
        if (sequencesIt != input.end() && sequencesIt->is_array()) {
            for (const nlohmann::json& sequenceNode : *sequencesIt) {
                if (!sequenceNode.is_object()) {
                    continue;
                }
                const CinematicSequenceId fallbackId{
                    static_cast<uint64_t>(settings.sequences.size() + 1)
                };
                settings.sequences.push_back(DeserializeSequence(
                    sequenceNode,
                    fallbackId,
                    "Sequence " + std::to_string(fallbackId.value)));
            }
        } else {
            const auto legacyIt = input.find("cameraSequence");
            if (legacyIt != input.end() && legacyIt->is_object()) {
                settings.sequences.push_back(DeserializeSequence(
                    *legacyIt,
                    CinematicSequenceId{ 1 },
                    "Main Sequence"));
                settings.defaultSequenceId = { 1 };
            }
        }
        NormalizeSceneCinematicsSettings(settings);
    }

    void SerializeSceneCinematicsJson(
        const SceneCinematicsSettings& settings,
        nlohmann::json& output) {

        output = nlohmann::json::object();
        output["defaultSequenceId"] = settings.defaultSequenceId.value;
        output["sequences"] = nlohmann::json::array();
        for (const CinematicSequence& sequence : settings.sequences) {
            nlohmann::json sequenceNode = {
                { "id", sequence.id.value },
                { "name", sequence.name },
                { "durationSeconds", sequence.durationSeconds },
                { "bindings", nlohmann::json::array() },
                { "tracks", nlohmann::json::array() }
            };
            for (const SEQUENCER::SequenceBinding& binding :
                    sequence.bindings) {
                sequenceNode["bindings"].push_back(
                    SerializeBinding(binding));
            }

            nlohmann::json cameraTrackNode = {
                { "id", sequence.cameraCutTrack.id.value },
                { "type", "cameraCut" },
                { "enabled", sequence.cameraCutTrack.enabled },
                { "clips", nlohmann::json::array() }
            };
            for (const SEQUENCER::CameraCutClip& clip :
                    sequence.cameraCutTrack.clips) {
                cameraTrackNode["clips"].push_back(
                    SerializeCameraCutClip(clip));
            }
            sequenceNode["tracks"].push_back(std::move(cameraTrackNode));
            SerializeCameraAnimationTracksJson(
                sequence,
                sequenceNode["tracks"]);
            output["sequences"].push_back(std::move(sequenceNode));
        }
    }

    void DeserializeCinematicSequenceJson(
        const nlohmann::json& input,
        CinematicSequence& sequence) {

        sequence = DeserializeSequence(
            input,
            CinematicSequenceId{ 1 },
            "Sequence");
    }

    void SerializeCinematicSequenceJson(
        const CinematicSequence& sequence,
        nlohmann::json& output) {

        SceneCinematicsSettings settings{};
        settings.sequences = { sequence };
        settings.defaultSequenceId = sequence.id.IsValid()
            ? sequence.id
            : CinematicSequenceId{ 1 };
        nlohmann::json wrapper{};
        SerializeSceneCinematicsJson(settings, wrapper);
        if (wrapper.contains("sequences") &&
            wrapper["sequences"].is_array() &&
            !wrapper["sequences"].empty()) {
            output = std::move(wrapper["sequences"][0]);
        } else {
            output = nlohmann::json::object();
        }
    }

} // namespace HIKARI
