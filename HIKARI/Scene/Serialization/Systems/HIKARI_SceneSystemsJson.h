#pragma once

#include <vector>

#include <json.hpp>

namespace HIKARI {

    struct SceneSystemData;

    namespace SCENE::SERIALIZATION {

        void SerializeSceneSystemsJson(const std::vector<SceneSystemData>& systems, nlohmann::json& out);

        void DeserializeSceneSystemsJson(const nlohmann::json& node, std::vector<SceneSystemData>& outSystems);

    } // namespace SCENE::SERIALIZATION

} // namespace HIKARI
