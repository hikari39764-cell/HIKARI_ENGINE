#pragma once

#include <json.hpp>

#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI {

    void DeserializeCameraAnimationTracksJson(
        const nlohmann::json& sequenceNode,
        CinematicSequence& sequence);
    void SerializeCameraAnimationTracksJson(
        const CinematicSequence& sequence,
        nlohmann::json& tracksNode);

} // namespace HIKARI
