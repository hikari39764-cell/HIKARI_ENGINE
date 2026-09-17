#include "HIKARI_PrefabRegistry.h"

#include <filesystem>
#include <utility>
#include <algorithm>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "HIKARI_PrefabDocument.h"
#include "HIKARI_PrefabSerializer.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

    namespace SceneJson = SCENE::SERIALIZATION;

    namespace {
        bool IsValidPrefabId(std::string_view id) {
            return !id.empty() && id.size() <= 128 && id != "." && id != ".." &&
                std::none_of(id.begin(), id.end(), [](unsigned char c) {
                    return c < 32 || c == '/' || c == '\\' || c == ':';
                });
        }
    }

    PrefabRegistry::PrefabRegistry(std::string rootPath)
        : rootPath_(std::move(rootPath)) {
    }

    bool PrefabRegistry::Save(const std::string& prefabId, const PrefabDocument& document, const PrefabSerializer& serializer) const {
        if (!IsValidPrefabId(prefabId)) {
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
        if (!IsValidPrefabId(prefabId)) {
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
        try {
            const uint32_t version = root.value("version", 1u);
            if (version != 1u && version != 2u) return false;
            PrefabDocument document{};
            document.prefabName = root.value("prefabName", std::string("Prefab"));
            if (!root.contains("rootObject") || !SceneJson::DeserializeSceneObjectJson(
                    root.at("rootObject"), document.rootObject,
                    version == 1u ? SceneJson::SceneObjectJsonScope::PrefabRoot :
                        SceneJson::SceneObjectJsonScope::SceneDocument)) return false;
            if (version == 1u && document.rootObject.id.value == 0u) document.rootObject.id.value = 1u;
            if (version == 2u && root.contains("descendants")) {
                const auto& descendants = root.at("descendants");
                if (!descendants.is_array()) return false;
                for (const auto& node : descendants) {
                    SceneObjectData object;
                    if (!SceneJson::DeserializeSceneObjectJson(node, object,
                            SceneJson::SceneObjectJsonScope::SceneDocument)) return false;
                    document.descendants.push_back(std::move(object));
                }
            }
            std::string error;
            if (!ValidatePrefabDocument(document, error)) return false;
            outDocument = std::move(document);
            return true;
        } catch (const nlohmann::json::exception&) {
            return false;
        }
    }

    bool PrefabSerializer::SaveToFile(
        const std::string& path,
        const PrefabDocument& document) const {
        std::string error;
        if (!ValidatePrefabDocument(document, error)) return false;
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
        root["descendants"] = nlohmann::json::array();
        for (auto child : document.descendants) {
            NormalizeSceneComponentIds(child);
            nlohmann::json node;
            SceneJson::SerializeSceneObjectJson(child, node,
                SceneJson::SceneObjectJsonScope::SceneDocument);
            root["descendants"].push_back(std::move(node));
        }
        return SERIALIZATION::JSON::WriteJsonFile(
            std::filesystem::path(path), root);
    }

} // namespace HIKARI
