#include "HIKARI_PrefabRegistry.h"

#include <filesystem>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "HIKARI_PrefabDocument.h"
#include "HIKARI_PrefabSerializer.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

    namespace SceneJson = SCENE::SERIALIZATION;

    PrefabRegistry::PrefabRegistry(std::string rootPath)
        : rootPath_(std::move(rootPath)) {
    }

    bool PrefabRegistry::Save(const std::string& prefabId, const PrefabDocument& document, const PrefabSerializer& serializer) const {
        if (prefabId.empty()) {
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(rootPath_, ec);
        if (ec) {
            return false;
        }

        return serializer.SaveToFile(BuildPath(prefabId), document);
    }

    bool PrefabRegistry::Load(const std::string& prefabId, PrefabDocument& outDocument, const PrefabSerializer& serializer) const {
        if (prefabId.empty()) {
            return false;
        }
        return serializer.LoadFromFile(BuildPath(prefabId), outDocument);
    }

    std::vector<std::string> PrefabRegistry::ListPrefabIds() const {
        std::vector<std::string> ids{};

        std::error_code ec{};
        if (!std::filesystem::exists(rootPath_, ec) || ec) {
            return ids;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(rootPath_, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }

            const std::filesystem::path path = entry.path();
            if (path.extension() != ".json") {
                continue;
            }
            ids.push_back(path.stem().string());
        }

        return ids;
    }

    std::string PrefabRegistry::BuildPath(const std::string& prefabId) const {
        return rootPath_ + "/" + prefabId + ".json";
    }

    bool PrefabSerializer::LoadFromFile(
        const std::string& path,
        PrefabDocument& outDocument) const {
        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                std::filesystem::path(path), root) ||
            !root.is_object()) {
            return false;
        }
        PrefabDocument document{};
        document.version = root.value("version", 1u);
        document.prefabName = root.value(
            "prefabName", std::string("Prefab"));
        (void)SceneJson::DeserializeSceneObjectJson(
            root.value("rootObject", nlohmann::json::object()),
            document.rootObject,
            SceneJson::SceneObjectJsonScope::PrefabRoot);
        outDocument = std::move(document);
        return true;
    }

    bool PrefabSerializer::SaveToFile(
        const std::string& path,
        const PrefabDocument& document) const {
        nlohmann::json root{
            { "version", document.version },
            { "prefabName", document.prefabName }
        };
        SceneObjectData persistedRoot = document.rootObject;
        NormalizeSceneComponentIds(persistedRoot);
        SceneJson::SerializeSceneObjectJson(
            persistedRoot,
            root["rootObject"],
            SceneJson::SceneObjectJsonScope::PrefabRoot);
        return SERIALIZATION::JSON::WriteJsonFile(
            std::filesystem::path(path), root);
    }

} // namespace HIKARI
