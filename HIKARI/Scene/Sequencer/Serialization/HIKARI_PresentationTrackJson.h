#pragma once

#include <json.hpp>

#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI::SEQUENCER {

    void DeserializePresentationTracksJson(
        const nlohmann::json& sequenceNode,
        CinematicSequence& sequence);
    void SerializePresentationTracksJson(
        const CinematicSequence& sequence,
        nlohmann::json& tracksNode);

} // namespace HIKARI::SEQUENCER
