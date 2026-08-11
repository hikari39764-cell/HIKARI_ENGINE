#include "Editor/Authoring/HIKARI_ComponentAuthoringQueryService.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::EDITOR {

    namespace {

        EditorOperationResult Failure(
            std::string code,
            std::string message) {

            EditorOperationResult result{};
            result.code = std::move(code);
            result.message = std::move(message);
            return result;
        }

        std::string BuildHierarchyPath(
            const SceneDocument& document,
            const SceneObjectData& object) {

            std::vector<const SceneObjectData*> hierarchy{};
            std::unordered_set<uint64_t> visited{};
            const SceneObjectData* current = &object;
            while (current != nullptr &&
                visited.insert(current->id.value).second) {
                hierarchy.push_back(current);
                current = current->parent.has_value()
                    ? FindSceneObjectData(document, *current->parent)
                    : nullptr;
            }
            std::reverse(hierarchy.begin(), hierarchy.end());
            std::string path{};
            for (const SceneObjectData* entry : hierarchy) {
                path += "/";
                path += entry->name.empty()
                    ? "GameObject"
                    : entry->name;
                path += "[" + std::to_string(entry->id.value) + "]";
            }
            return path.empty()
                ? "/GameObject[" +
                    std::to_string(object.id.value) + "]"
                : path;
        }

    } // namespace

    EditorOperationResult ComponentAuthoringQueryService::QueryObjects(
        const DocumentSceneBase& scene,
        std::string_view exactName,
        std::string_view componentType) const {

        const SceneDocument& document = scene.GetSceneDocument();
        nlohmann::json objects = nlohmann::json::array();
        EditorOperationResult result{};
        for (const SceneObjectData& object : document.objects) {
            if (!exactName.empty() && object.name != exactName) {
                continue;
            }
            if (!componentType.empty() &&
                std::none_of(
                    object.components.begin(),
                    object.components.end(),
                    [componentType](
                        const SceneComponentData& component) {
                        return component.type == componentType;
                    })) {
                continue;
            }
            objects.push_back(SerializeObject(document, object));
            result.details.push_back(
                std::to_string(object.id.value) + ": " +
                BuildHierarchyPath(document, object));
        }
        result.ok = true;
        result.code = "OK";
        result.message = "Scene object identities returned.";
        result.data = {
            { "count", objects.size() },
            { "objects", std::move(objects) }
        };
        return result;
    }

    EditorOperationResult ComponentAuthoringQueryService::QueryComponents(
        const DocumentSceneBase& scene,
        uint64_t objectId) const {

        const SceneDocument& document = scene.GetSceneDocument();
        const SceneObjectData* object = FindSceneObjectData(
            document,
            SceneObjectId{ objectId });
        if (object == nullptr) {
            return Failure(
                "OBJECT_NOT_FOUND",
                "Scene object id was not found: " +
                    std::to_string(objectId));
        }
        EditorOperationResult result{};
        result.ok = true;
        result.code = "OK";
        result.message = "Component instances returned.";
        result.data = SerializeObject(document, *object);
        for (const SceneComponentData& component :
            object->components) {
            result.details.push_back(
                std::to_string(component.id.value) + ": " +
                component.type);
        }
        return result;
    }

    nlohmann::json ComponentAuthoringQueryService::SerializeComponent(
        const SceneComponentData& component) const {

        return {
            { "componentId", component.id.value },
            { "type", component.type }
        };
    }

    nlohmann::json ComponentAuthoringQueryService::SerializeObject(
        const SceneDocument& document,
        const SceneObjectData& object) const {

        nlohmann::json components = nlohmann::json::array();
        for (const SceneComponentData& component :
            object.components) {
            components.push_back(SerializeComponent(component));
        }
        return {
            { "objectId", object.id.value },
            { "name", object.name },
            { "hierarchyPath", BuildHierarchyPath(document, object) },
            {
                "parentObjectId",
                object.parent.has_value()
                    ? nlohmann::json(object.parent->value)
                    : nlohmann::json(nullptr)
            },
            { "enabled", object.enabled },
            { "components", std::move(components) }
        };
    }

} // namespace HIKARI::EDITOR
