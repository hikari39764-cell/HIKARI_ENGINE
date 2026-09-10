#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

#include <cstdint>
#include <string>
#include <utility>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceMigration.h"
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
                out.push_back({
                    { "id", component.id.value },
                    { "type", component.type },
                    { "properties", component.properties }
                });
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
                component.id.value = componentNode.value("id", 0ull);
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
            if (!object.editorFolder.empty()) out["editorFolder"] = object.editorFolder;
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
            outObject.editorFolder = node.value("editorFolder", std::string{});
            if (node.contains("parent") && node["parent"].is_number_unsigned()) {
                outObject.parent = SceneObjectId{ node["parent"].get<uint64_t>() };
            }
        }

        DeserializeTransform(node.value("transform", nlohmann::json::object()), outObject.transform);
        DeserializeComponents(node.value("components", nlohmann::json::array()), outObject.components);
        {
            const nlohmann::json* sandJson = nullptr;
            const nlohmann::json* waterJson = nullptr;
            SceneComponentId terrainId{};
            for (const SceneComponentData& component : outObject.components) {
                if (component.type == "SandSurfaceComponent") {
                    sandJson = &component.properties;
                    terrainId = component.id;
                } else if (component.type == "WaterSurfaceComponent") {
                    waterJson = &component.properties;
                }
            }
            if (sandJson != nullptr) {
                nlohmann::json merged =
                    TERRAIN::BuildTerrainSurfaceFromLegacy(
                        *sandJson,
                        waterJson);
                std::vector<SceneComponentData> next{};
                next.reserve(outObject.components.size());
                for (SceneComponentData& component : outObject.components) {
                    // 砂地は統合地形へ移行し、水は独立コンポーネントとして温存する。
                    if (component.type == "SandSurfaceComponent") {
                        continue;
                    }
                    next.push_back(std::move(component));
                }
                SceneComponentData terrain{};
                terrain.type = "TerrainSurfaceComponent";
                terrain.id = terrainId;
                terrain.properties = std::move(merged);
                next.push_back(std::move(terrain));
                outObject.components = std::move(next);
            }
        }
        NormalizeSceneComponentIds(outObject);
        return true;
    }

} // namespace HIKARI::SCENE::SERIALIZATION
