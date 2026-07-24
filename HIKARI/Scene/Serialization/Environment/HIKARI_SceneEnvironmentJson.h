#pragma once

#include <json.hpp>

namespace HIKARI {

    struct SceneEnvironment;

    namespace SCENE::SERIALIZATION {

        void SerializeSceneEnvironmentJson(const SceneEnvironment& environment, nlohmann::json& out);

        void DeserializeSceneEnvironmentJson(const nlohmann::json& node, SceneEnvironment& outEnvironment);

    } // namespace SCENE::SERIALIZATION

} // namespace HIKARI
