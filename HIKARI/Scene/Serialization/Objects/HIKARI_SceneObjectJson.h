#pragma once

#include <json.hpp>

namespace HIKARI {

    struct SceneObjectData;

    namespace SCENE::SERIALIZATION {

        enum class SceneObjectJsonScope { SceneDocument, PrefabRoot };

        void SerializeSceneObjectJson(const SceneObjectData& object, nlohmann::json& out, SceneObjectJsonScope scope);

        bool DeserializeSceneObjectJson(const nlohmann::json& node, SceneObjectData& outObject, SceneObjectJsonScope scope);

    } // namespace SCENE::SERIALIZATION

} // namespace HIKARI
