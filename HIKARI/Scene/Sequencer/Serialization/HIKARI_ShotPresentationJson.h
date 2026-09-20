#pragma once
#include <json.hpp>
#include "Scene/Sequencer/Tracks/HIKARI_ShotPresentation.h"

namespace HIKARI::SEQUENCER {
    nlohmann::json WriteShotCamera(const ShotCamera& camera);
    ShotCamera ReadShotCamera(const nlohmann::json& node);
    nlohmann::json WriteScreenEffects(const std::vector<SequenceScreenEffect>& effects);
    std::vector<SequenceScreenEffect> ReadScreenEffects(const nlohmann::json& node);
}
