#include "Scene/Prefab/HIKARI_PrefabSerializer.h"

#include <filesystem>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

    namespace SceneJson = SCENE::SERIALIZATION;

    bool PrefabSerializer::LoadFromFile(const std::string& path, PrefabDocument& outDocument) const {

        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(std::filesystem::path(path), root) || !root.is_object()) {
            return false;
        }

        PrefabDocument document{};
        document.version = root.value("version", 1u);
        document.prefabName = root.value("prefabName", std::string("Prefab"));
        (void)SceneJson::DeserializeSceneObjectJson(root.value("rootObject", nlohmann::json::object()), document.rootObject,
                                                    SceneJson::SceneObjectJsonScope::PrefabRoot);

        outDocument = std::move(document);
        return true;
    }

    bool PrefabSerializer::SaveToFile(const std::string& path, const PrefabDocument& document) const {

        nlohmann::json root = { { "version", document.version }, { "prefabName", document.prefabName } };
        SceneJson::SerializeSceneObjectJson(document.rootObject, root["rootObject"], SceneJson::SceneObjectJsonScope::PrefabRoot);

        return SERIALIZATION::JSON::WriteJsonFile(std::filesystem::path(path), root);
    }

} // namespace HIKARI
