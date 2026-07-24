#pragma once

#include <json.hpp>

namespace HIKARI {

    struct SceneLightingBakeSettings;

    namespace SCENE::SERIALIZATION {

        void SerializeSceneLightingBakeJson(const SceneLightingBakeSettings& settings, nlohmann::json& out);

        void DeserializeSceneLightingBakeJson(const nlohmann::json& node, SceneLightingBakeSettings& outSettings);

    } // namespace SCENE::SERIALIZATION

} // namespace HIKARI
