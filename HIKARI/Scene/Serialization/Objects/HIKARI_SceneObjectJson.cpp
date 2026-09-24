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

void NormalizeLegacySceneObjectReferences(SceneComponentData& component) {
    // Legacy migration names only known component fields. New project
    // components opt in through PropertyWriter::WriteSceneObjectId.
    const auto normalize = [](nlohmann::json& properties, const char* name) {
        if (!properties.is_object())
            return;
        const auto found = properties.find(name);
        uint64_t id = 0;
        if (found != properties.end() && ReadSceneObjectReference(*found, id)) {
            *found = WriteSceneObjectReference(id);
        }
    };
    auto& properties = component.properties;
    if (!properties.is_object())
        return;
    if (component.type == "CameraFollowComponent") {
        normalize(properties, "targetObjectId");
        normalize(properties, "cameraLookTarget");
    } else if (component.type == "CameraActivationVolumeComponent" || component.type == "CameraFlowComponent") {
        normalize(properties, "subjectObjectId");
        normalize(properties, "cameraObjectId");
    } else if (component.type == "SequencePlayerComponent") {
        if (auto bindings = properties.find("bindings"); bindings != properties.end() && bindings->is_array()) {
            for (auto& binding : *bindings)
                normalize(binding, "sceneObjectId");
        }
    } else if (component.type == "VfxPlayerComponent") {
        if (auto slots = properties.find("slots"); slots != properties.end() && slots->is_array()) {
            for (auto& slot : *slots)
                normalize(slot, "fixedObjectId");
        }
    }
}

uint32_t RemapSceneObjectReferences(nlohmann::json& properties, const std::unordered_map<uint64_t, uint64_t>& ids,
                                    bool clearExternalReferences) {
    // Iterative traversal also handles arrays without recursive stack growth.
    uint32_t externalCount = 0;
    std::vector<nlohmann::json*> pending{&properties};
    while (!pending.empty()) {
        auto* value = pending.back();
        pending.pop_back();
        if (value->is_object() && value->contains("$hikariRef")) {
            uint64_t id = 0;
            if (ReadSceneObjectReference(*value, id)) {
                if (const auto found = ids.find(id); found != ids.end()) {
                    (*value)["id"] = found->second;
                } else if (id != 0) {
                    ++externalCount;
                    if (clearExternalReferences)
                        (*value)["id"] = uint64_t{0};
                }
            }
            continue;
        }
        if (value->is_object() || value->is_array()) {
            for (auto& child : *value)
                pending.push_back(&child);
        }
    }
    return externalCount;
}

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
        auto persisted = component;
        NormalizeLegacySceneObjectReferences(persisted);
        out.push_back({{"id", component.id.value}, {"type", component.type}, {"properties", persisted.properties}});
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
        NormalizeLegacySceneObjectReferences(component);
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
        if (!object.editorFolder.empty())
            out["editorFolder"] = object.editorFolder;
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
    outObject.name = node.value("name", scope == SceneObjectJsonScope::SceneDocument ? std::string("GameObject")
                                                                                     : std::string("PrefabObject"));
    outObject.sourcePrefabId = node.value("sourcePrefabId", std::string{});
    outObject.enabled = node.value("enabled", true);
    outObject.editorVisible = node.value("editorVisible", true);

    if (scope == SceneObjectJsonScope::SceneDocument) {
        outObject.editorLocked = node.value("editorLocked", false);
        outObject.editorFolder = node.value("editorFolder", std::string{});
        if (node.contains("parent") && node["parent"].is_number_unsigned()) {
            outObject.parent = SceneObjectId{node["parent"].get<uint64_t>()};
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
            nlohmann::json merged = TERRAIN::BuildTerrainSurfaceFromLegacy(*sandJson, waterJson);
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
