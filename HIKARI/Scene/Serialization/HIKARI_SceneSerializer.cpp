#include "Scene/Serialization/HIKARI_SceneSerializer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"
#include "Scene/Serialization/Environment/HIKARI_SceneEnvironmentJson.h"
#include "Scene/Serialization/Lighting/HIKARI_SceneLightingBakeJson.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include "Scene/Serialization/Systems/HIKARI_SceneSystemsJson.h"

namespace HIKARI {

    namespace SceneJson = SCENE::SERIALIZATION;

    namespace {

        void MigrateLegacyProceduralModels(SceneDocument& document) {

            if (document.version >= 3) {
                return;
            }
            for (SceneObjectData& object : document.objects) {
                SceneComponentData* model = nullptr;
                bool hasProceduralMesh = false;
                for (SceneComponentData& component : object.components) {
                    if (component.type == "ModelComponent") {
                        model = &component;
                    } else if (component.type == "ProceduralMeshComponent") {
                        hasProceduralMesh = true;
                    }
                }
                if (model == nullptr || !model->properties.is_object()) {
                    continue;
                }

                const nlohmann::json sourceKind = model->properties.value("sourceKind", nlohmann::json{});
                const bool wasProcedural = (sourceKind.is_string() && sourceKind.get<std::string>() == "Procedural") ||
                                           (sourceKind.is_number_integer() && sourceKind.get<int>() == 1);
                if (wasProcedural && !hasProceduralMesh) {
                    nlohmann::json properties = model->properties.value("procedural", nlohmann::json::object());
                    if (!properties.is_object()) {
                        properties = nlohmann::json::object();
                    }
                    const std::string kind = properties.value("kind", std::string("GridPlane"));
                    if ((kind == "Plane" || kind == "GridPlane") && properties.contains("height")) {
                        // Legacy planes used height as their Z dimension.
                        properties["depth"] = properties["height"];
                    }
                    object.components.push_back(SceneComponentData{ "ProceduralMeshComponent", std::move(properties) });
                }
                model->properties.erase("sourceKind");
                model->properties.erase("procedural");
            }
            document.version = (std::max)(document.version, 3u);
        }

        void MigrateEmbeddedModelMaterialFx(
            SceneDocument& document) {

            if (document.version >= 4u) {
                return;
            }

            for (SceneObjectData& object : document.objects) {
                const bool hasMaterialFx =
                    std::any_of(
                        object.components.begin(),
                        object.components.end(),
                        [](const SceneComponentData& component) {
                            return component.type ==
                                "MaterialFxComponent";
                        });
                bool shouldAddMaterialFx = false;
                nlohmann::json materialFxProperties{};

                for (SceneComponentData& component :
                    object.components) {

                    if (component.type != "ModelComponent" ||
                        !component.properties.is_object()) {

                        continue;
                    }

                    nlohmann::json& properties =
                        component.properties;
                    const std::string profileId =
                        properties.value(
                            "materialFxProfileId",
                            std::string{});
                    const bool valuesInitialized =
                        properties.value(
                            "materialFxValuesInitialized",
                            false);
                    const bool hasParamValues =
                        properties.contains(
                            "materialFxParamValues") &&
                        properties["materialFxParamValues"]
                            .is_array() &&
                        !properties["materialFxParamValues"]
                            .empty();

                    if (!hasMaterialFx &&
                        !shouldAddMaterialFx &&
                        (!profileId.empty() ||
                            valuesInitialized ||
                            hasParamValues)) {

                        materialFxProperties = {
                            { "profileId", profileId },
                            {
                                "valuesInitialized",
                                valuesInitialized ||
                                    hasParamValues
                            }
                        };
                        if (hasParamValues) {
                            materialFxProperties["paramValues"] =
                                properties[
                                    "materialFxParamValues"];
                        }
                        shouldAddMaterialFx = true;
                    }

                    properties.erase("materialFxProfileId");
                    properties.erase(
                        "materialFxValuesInitialized");
                    properties.erase(
                        "materialFxParamValues");
                }

                if (shouldAddMaterialFx) {
                    object.components.push_back(
                        SceneComponentData{
                            "MaterialFxComponent",
                            std::move(materialFxProperties)
                        });
                }
            }
            document.version =
                (std::max)(document.version, 4u);
        }

        void DeserializeCamera(const nlohmann::json& node, SceneCameraSettings& outCamera) {

            if (!node.is_object() || !node.contains("defaultCameraObjectId") || !node["defaultCameraObjectId"].is_number_unsigned()) {
                return;
            }

            const uint64_t objectId = node["defaultCameraObjectId"].get<uint64_t>();
            if (objectId != 0u) {
                outCamera.defaultCameraObjectId = SceneObjectId{ objectId };
            }
        }

        void SerializeCamera(const SceneCameraSettings& camera, nlohmann::json& out) {

            out["defaultCameraObjectId"] =
                camera.defaultCameraObjectId.has_value() ? nlohmann::json(camera.defaultCameraObjectId->value) : nlohmann::json(nullptr);
        }

    } // namespace

    bool SceneSerializer::LoadFromFile(const std::string& path, SceneDocument& outDocument) const {

        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(std::filesystem::path(path), root) || !root.is_object()) {
            return false;
        }

        outDocument = SceneDocument{};
        outDocument.version = root.value("version", 1u);
        outDocument.sceneName = root.value("sceneName", std::string("Untitled"));

        if (root.contains("environment") && root["environment"].is_object()) {
            SceneJson::DeserializeSceneEnvironmentJson(root["environment"], outDocument.environment);
        }

        SceneJson::DeserializeSceneLightingBakeJson(root.value("lightingBake", nlohmann::json::object()), outDocument.lightingBake);
        DeserializeCamera(root.value("camera", nlohmann::json::object()), outDocument.camera);
        DeserializeSceneCinematicsJson(root.value("cinematics", nlohmann::json::object()), outDocument.cinematics);
        SceneJson::DeserializeSceneSystemsJson(root.value("systems", nlohmann::json{}), outDocument.systems);

        if (root.contains("objects") && root["objects"].is_array()) {
            for (const nlohmann::json& node : root["objects"]) {
                SceneObjectData object{};
                if (SceneJson::DeserializeSceneObjectJson(node, object, SceneJson::SceneObjectJsonScope::SceneDocument)) {
                    outDocument.objects.push_back(std::move(object));
                }
            }
        }

        MigrateLegacyProceduralModels(outDocument);
        MigrateEmbeddedModelMaterialFx(outDocument);
        outDocument.version = (std::max)(outDocument.version, kCurrentSceneDocumentVersion);
        return true;
    }

    bool SceneSerializer::SaveToFile(const std::string& path, const SceneDocument& document) const {

        nlohmann::json root = nlohmann::json::object();
        root["version"] = document.version;
        root["sceneName"] = document.sceneName;

        SceneJson::SerializeSceneEnvironmentJson(document.environment, root["environment"]);
        SceneJson::SerializeSceneLightingBakeJson(document.lightingBake, root["lightingBake"]);
        SerializeCamera(document.camera, root["camera"]);
        SerializeSceneCinematicsJson(document.cinematics, root["cinematics"]);
        SceneJson::SerializeSceneSystemsJson(document.systems, root["systems"]);

        root["objects"] = nlohmann::json::array();
        for (const SceneObjectData& object : document.objects) {
            nlohmann::json objectNode{};
            SceneJson::SerializeSceneObjectJson(object, objectNode, SceneJson::SceneObjectJsonScope::SceneDocument);
            root["objects"].push_back(std::move(objectNode));
        }

        return SERIALIZATION::JSON::WriteJsonFile(std::filesystem::path(path), root);
    }

} // namespace HIKARI
