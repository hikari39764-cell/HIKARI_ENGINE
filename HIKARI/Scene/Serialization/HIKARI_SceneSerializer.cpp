#include "Scene/Serialization/HIKARI_SceneSerializer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"
#include "Scene/Serialization/Environment/HIKARI_SceneEnvironmentJson.h"
#include "Scene/Serialization/Lighting/HIKARI_SceneLightingBakeJson.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI::SCENE::SERIALIZATION {

void SerializeSceneSystemsJson(const std::vector<SceneSystemData>& systems, nlohmann::json& out) {
    out = nlohmann::json::array();
    for (const SceneSystemData& system : systems) {
        out.push_back({{"systemId", system.systemId},
                       {"enabled", system.enabled},
                       {"executionOrder", system.executionOrder},
                       {"settings", system.settings.is_object() ? system.settings : nlohmann::json::object()}});
    }
}

void DeserializeSceneSystemsJson(const nlohmann::json& node, std::vector<SceneSystemData>& outSystems) {
    outSystems.clear();
    if (!node.is_array())
        return;
    for (const nlohmann::json& systemNode : node) {
        if (!systemNode.is_object())
            continue;
        SceneSystemData system{};
        system.systemId = systemNode.value("systemId", std::string{});
        if (system.systemId.empty())
            continue;
        system.enabled = systemNode.value("enabled", true);
        system.executionOrder = systemNode.value("executionOrder", 0);
        system.settings = systemNode.value("settings", nlohmann::json::object());
        if (!system.settings.is_object()) {
            system.settings = nlohmann::json::object();
        }
        outSystems.push_back(std::move(system));
    }
}

} // namespace HIKARI::SCENE::SERIALIZATION

namespace HIKARI {

namespace SceneJson = SCENE::SERIALIZATION;

namespace {

void DeserializeCamera(const nlohmann::json& node, SceneCameraSettings& outCamera) {
    if (!node.is_object() || !node.contains("defaultCameraObjectId") ||
        !node["defaultCameraObjectId"].is_number_unsigned()) {
        return;
    }

    const uint64_t objectId = node["defaultCameraObjectId"].get<uint64_t>();
    if (objectId != 0u) {
        outCamera.defaultCameraObjectId = SceneObjectId{objectId};
    }
}

void SerializeCamera(const SceneCameraSettings& camera, nlohmann::json& out) {
    out["defaultCameraObjectId"] = camera.defaultCameraObjectId.has_value()
                                       ? nlohmann::json(camera.defaultCameraObjectId->value)
                                       : nlohmann::json(nullptr);
}

} // namespace

bool SceneSerializer::LoadFromFile(const std::string& path, SceneDocument& outDocument) const {
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(std::filesystem::path(path), root) || !root.is_object()) {
        return false;
    }

    try {
        SceneDocument decoded{};
        // 現行版だけを読む。旧版の scene は旧版エンジンで開いて保存し直す。
        decoded.version = root.value("version", 0u);
        if (decoded.version != kCurrentSceneDocumentVersion) {
            HIKARI_LOG_ERROR("[SceneAsset] unsupported scene version: " + std::to_string(decoded.version) +
                             " (supported: " + std::to_string(kCurrentSceneDocumentVersion) + ")");
            return false;
        }
        decoded.sceneName = root.value("sceneName", std::string("Untitled"));
        if (const auto folders = root.find("editorFolders"); folders != root.end() && folders->is_array())
            for (const auto& folder : *folders)
                if (folder.is_string())
                    decoded.editorFolders.push_back(folder.get<std::string>());

        if (root.contains("environment") && root["environment"].is_object()) {
            SceneJson::DeserializeSceneEnvironmentJson(root["environment"], decoded.environment);
        }

        SceneJson::DeserializeSceneLightingBakeJson(root.value("lightingBake", nlohmann::json::object()),
                                                    decoded.lightingBake);
        DeserializeCamera(root.value("camera", nlohmann::json::object()), decoded.camera);
        DeserializeSceneCinematicsJson(root.value("cinematics", nlohmann::json::object()), decoded.cinematics);
        SceneJson::DeserializeSceneSystemsJson(root.value("systems", nlohmann::json{}), decoded.systems);

        if (root.contains("objects") && root["objects"].is_array()) {
            for (const nlohmann::json& node : root["objects"]) {
                SceneObjectData object{};
                if (SceneJson::DeserializeSceneObjectJson(node, object,
                                                          SceneJson::SceneObjectJsonScope::SceneDocument)) {
                    decoded.objects.push_back(std::move(object));
                }
            }
        }

        LIGHTING_AUTHORING::SynchronizeLegacyLightingViews(decoded);
        for (SceneObjectData& object : decoded.objects) {
            NormalizeSceneComponentIds(object);
        }
        outDocument = std::move(decoded);
        return true;
    } catch (const nlohmann::json::exception& exception) {
        HIKARI_LOG_ERROR(std::string("[SceneAsset] malformed scene: ") + exception.what());
        return false;
    }
}

bool SceneSerializer::SaveToFile(const std::string& path, const SceneDocument& document) const {
    SceneDocument persisted = document;
    persisted.version = kCurrentSceneDocumentVersion;
    for (SceneObjectData& object : persisted.objects) {
        NormalizeSceneComponentIds(object);
    }
    LIGHTING_AUTHORING::SynchronizeLegacyLightingViews(persisted);

    nlohmann::json root = nlohmann::json::object();
    root["version"] = persisted.version;
    root["sceneName"] = persisted.sceneName;
    if (!persisted.editorFolders.empty())
        root["editorFolders"] = persisted.editorFolders;

    SceneJson::SerializeSceneEnvironmentJson(persisted.environment, root["environment"]);
    // Version 6 以场景组件为唯一 Probe / Light Authoring 来源。
    root["environment"].erase("reflectionProbe");
    root["environment"].erase("directional");
    root["environment"].erase("pointLights");
    root["environment"].erase("spotLights");
    SerializeCamera(persisted.camera, root["camera"]);
    SerializeSceneCinematicsJson(persisted.cinematics, root["cinematics"]);
    SceneJson::SerializeSceneSystemsJson(persisted.systems, root["systems"]);

    root["objects"] = nlohmann::json::array();
    for (const SceneObjectData& object : persisted.objects) {
        nlohmann::json objectNode{};
        SceneJson::SerializeSceneObjectJson(object, objectNode, SceneJson::SceneObjectJsonScope::SceneDocument);
        root["objects"].push_back(std::move(objectNode));
    }

    return SERIALIZATION::JSON::WriteJsonFile(std::filesystem::path(path), root);
}

} // namespace HIKARI
