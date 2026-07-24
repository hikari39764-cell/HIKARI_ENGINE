#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

#include <cstdint>
#include <string>
#include <utility>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::SCENE::SERIALIZATION {

    namespace JsonMath = ::HIKARI::SERIALIZATION::JSON::MATH;

    namespace {

        void SerializeTransform(const TransformData& transform, nlohmann::json& out) {

            out["position"] = JsonMath::ToJsonArray(transform.position);
            out["rotationEulerDeg"] = JsonMath::ToJsonArray(transform.rotationEulerDeg);
            out["scale"] = JsonMath::ToJsonArray(transform.scale);
        }

        void DeserializeTransform(const nlohmann::json& node, TransformData& outTransform) {

            if (!node.is_object()) {
                return;
            }
            outTransform.position = JSONREAD::Vec3Or(node.value("position", nlohmann::json::array()), outTransform.position);
            outTransform.rotationEulerDeg =
                JSONREAD::Vec3Or(node.value("rotationEulerDeg", nlohmann::json::array()), outTransform.rotationEulerDeg);
            outTransform.scale = JSONREAD::Vec3Or(node.value("scale", nlohmann::json::array()), outTransform.scale);
        }

        void SerializeComponents(const std::vector<SceneComponentData>& components, nlohmann::json& out) {

            out = nlohmann::json::array();
            for (const SceneComponentData& component : components) {
                out.push_back({ { "type", component.type }, { "properties", component.properties } });
            }
        }

        void DeserializeComponents(const nlohmann::json& node, std::vector<SceneComponentData>& outComponents) {

            outComponents.clear();
            if (!node.is_array()) {
                return;
            }
            for (const nlohmann::json& componentNode : node) {
                if (!componentNode.is_object()) {
                    continue;
                }
                SceneComponentData component{};
                component.type = componentNode.value("type", std::string{});
                component.properties = componentNode.value("properties", nlohmann::json::object());
                outComponents.push_back(std::move(component));
            }
        }

    } // namespace

    void SerializeSceneObjectJson(const SceneObjectData& object, nlohmann::json& out, SceneObjectJsonScope scope) {

        out = nlohmann::json::object();
        out["id"] = object.id.value;
        out["name"] = object.name;
        out["sourcePrefabId"] = object.sourcePrefabId;
        out["enabled"] = object.enabled;
        out["editorVisible"] = object.editorVisible;

        if (scope == SceneObjectJsonScope::SceneDocument) {
            out["editorLocked"] = object.editorLocked;
            out["parent"] = object.parent.has_value() ? nlohmann::json(object.parent->value) : nlohmann::json(nullptr);
        } else {
            out["parent"] = nullptr;
        }

        SerializeTransform(object.transform, out["transform"]);
        SerializeComponents(object.components, out["components"]);
    }

    bool DeserializeSceneObjectJson(const nlohmann::json& node, SceneObjectData& outObject, SceneObjectJsonScope scope) {

        if (!node.is_object()) {
            return false;
        }

        outObject = SceneObjectData{};
        outObject.id.value = node.value("id", 0ull);
        outObject.name =
            node.value("name", scope == SceneObjectJsonScope::SceneDocument ? std::string("GameObject") : std::string("PrefabObject"));
        outObject.sourcePrefabId = node.value("sourcePrefabId", std::string{});
        outObject.enabled = node.value("enabled", true);
        outObject.editorVisible = node.value("editorVisible", true);

        if (scope == SceneObjectJsonScope::SceneDocument) {
            outObject.editorLocked = node.value("editorLocked", false);
            if (node.contains("parent") && node["parent"].is_number_unsigned()) {
                outObject.parent = SceneObjectId{ node["parent"].get<uint64_t>() };
            }
        }

        DeserializeTransform(node.value("transform", nlohmann::json::object()), outObject.transform);
        DeserializeComponents(node.value("components", nlohmann::json::array()), outObject.components);
        return true;
    }

} // namespace HIKARI::SCENE::SERIALIZATION
