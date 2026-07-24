#pragma once

#include <json.hpp>

#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI {

    void DeserializeCinematicSequenceJson(
        const nlohmann::json& input,
        CinematicSequence& sequence);
    void SerializeCinematicSequenceJson(
        const CinematicSequence& sequence,
        nlohmann::json& output);

    void DeserializeSceneCinematicsJson(
        const nlohmann::json& input,
        SceneCinematicsSettings& settings);
    void SerializeSceneCinematicsJson(
        const SceneCinematicsSettings& settings,
        nlohmann::json& output);

} // namespace HIKARI
