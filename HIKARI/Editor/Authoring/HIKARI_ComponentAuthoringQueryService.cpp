#include "Editor/Authoring/HIKARI_ComponentAuthoringQueryService.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Editor/Authoring/HIKARI_ComponentPropertyAuthoringService.h"

namespace HIKARI::EDITOR {

namespace {

EditorOperationResult Failure(std::string code, std::string message) {
    EditorOperationResult result{};
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

std::string BuildHierarchyPath(const SceneDocument& document, const SceneObjectData& object) {
    std::vector<const SceneObjectData*> hierarchy{};
    std::unordered_set<uint64_t> visited{};
    const SceneObjectData* current = &object;
    while (current != nullptr && visited.insert(current->id.value).second) {
        hierarchy.push_back(current);
        current = current->parent.has_value() ? FindSceneObjectData(document, *current->parent) : nullptr;
    }
    std::reverse(hierarchy.begin(), hierarchy.end());
    std::string path{};
    for (const SceneObjectData* entry : hierarchy) {
        path += "/";
        path += entry->name.empty() ? "GameObject" : entry->name;
        path += "[" + std::to_string(entry->id.value) + "]";
    }
    return path.empty() ? "/GameObject[" + std::to_string(object.id.value) + "]" : path;
}

} // namespace

EditorOperationResult ComponentAuthoringQueryService::QueryTypes(DocumentSceneBase& scene, std::string_view keyword,
                                                                 int offset, int limit) const {
    const auto folded = [](std::string text) {
        for (auto& c : text)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return text;
    };
    const auto filter = folded(std::string(keyword));
    auto types = scene.GetComponentRegistry().GetTypeInfos();
    std::sort(types.begin(), types.end(), [](const auto* a, const auto* b) { return a->typeName < b->typeName; });
    auto items = nlohmann::json::array();
    size_t matched = 0;
    offset = (std::max)(0, offset);
    limit = std::clamp(limit, 1, 64);
    for (const auto* type : types) {
        const auto& presentation = type->presentation;
        const auto text = folded(type->typeName + " " + presentation.displayName + " " + presentation.category + " " +
                                 presentation.description);
        if (!filter.empty() && text.find(filter) == std::string::npos)
            continue;
        if (matched++ < static_cast<size_t>(offset) || items.size() >= static_cast<size_t>(limit))
            continue;
        items.push_back({{"componentType", type->typeName},
                         {"name", presentation.displayName},
                         {"category", presentation.category},
                         {"description", presentation.description},
                         {"requiredComponents", type->requiredComponents},
                         {"incompatibleComponents", type->incompatibleComponents},
                         {"allowMultiple", type->allowMultiple}});
    }
    EditorOperationResult result{true, "OK", "Registered component types", {{"types", items}, {"total", matched}}};
    if (static_cast<size_t>(offset) + items.size() < matched)
        result.data["nextOffset"] = offset + items.size();
    return result;
}

EditorOperationResult ComponentAuthoringQueryService::DescribeType(DocumentSceneBase& scene,
                                                                   std::string_view componentType) const {
    const auto* type = scene.GetComponentRegistry().Find(componentType);
    if (!type)
        return Failure("UNKNOWN_COMPONENT_TYPE", "Search component.types.query for a registered component type.");
    auto instance = type->factory();
    if (!instance)
        return Failure("COMPONENT_FACTORY_FAILED", "The component factory returned no instance.");
    // 型情報の取得は一時文書を使い、ユーザーのシーンには追加しない。
    SceneDocument defaults;
    SceneObjectData object;
    object.id = SceneObjectId{1};
    object.name = type->presentation.displayName;
    SceneComponentData component;
    component.id = SceneComponentId{1};
    component.type = type->typeName;
    instance->Serialize(component.properties);
    if (type->initializeDefaults)
        type->initializeDefaults(object, component.properties);
    object.components.push_back(std::move(component));
    defaults.objects.push_back(std::move(object));
    InspectorContext context;
    context.assetDatabase = &scene.GetAssetDatabase();
    auto described = ComponentPropertyAuthoringService{}.Describe(scene.GetComponentRegistry(), defaults,
                                                                  {SceneObjectId{1}, SceneComponentId{1}}, context);
    described.data.erase("objectId");
    described.data.erase("componentId");
    described.data.erase("objectName");
    described.data["scope"] = "type_defaults";
    return {described.ok, described.code, described.message, std::move(described.data)};
}

EditorOperationResult ComponentAuthoringQueryService::QueryObjects(const DocumentSceneBase& scene,
                                                                   std::string_view exactName,
                                                                   std::string_view componentType) const {
    const SceneDocument& document = scene.GetSceneDocument();
    nlohmann::json objects = nlohmann::json::array();
    EditorOperationResult result{};
    for (const SceneObjectData& object : document.objects) {
        if (!exactName.empty() && object.name != exactName) {
            continue;
        }
        if (!componentType.empty() && std::none_of(object.components.begin(), object.components.end(),
                                                   [componentType](const SceneComponentData& component) {
                                                       return component.type == componentType;
                                                   })) {
            continue;
        }
        objects.push_back(SerializeObject(document, object));
        result.details.push_back(std::to_string(object.id.value) + ": " + BuildHierarchyPath(document, object));
    }
    result.ok = true;
    result.code = "OK";
    result.message = "Scene object identities returned.";
    result.data = {{"count", objects.size()}, {"objects", std::move(objects)}};
    return result;
}

EditorOperationResult ComponentAuthoringQueryService::QueryComponents(const DocumentSceneBase& scene,
                                                                      uint64_t objectId) const {
    const SceneDocument& document = scene.GetSceneDocument();
    const SceneObjectData* object = FindSceneObjectData(document, SceneObjectId{objectId});
    if (object == nullptr) {
        return Failure("OBJECT_NOT_FOUND", "Scene object id was not found: " + std::to_string(objectId));
    }
    EditorOperationResult result{};
    result.ok = true;
    result.code = "OK";
    result.message = "Component instances returned.";
    result.data = SerializeObject(document, *object);
    for (const SceneComponentData& component : object->components) {
        result.details.push_back(std::to_string(component.id.value) + ": " + component.type);
    }
    return result;
}

nlohmann::json ComponentAuthoringQueryService::SerializeComponent(const SceneComponentData& component) const {
    return {{"componentId", component.id.value}, {"type", component.type}};
}

nlohmann::json ComponentAuthoringQueryService::SerializeObject(const SceneDocument& document,
                                                               const SceneObjectData& object) const {
    nlohmann::json components = nlohmann::json::array();
    for (const SceneComponentData& component : object.components) {
        components.push_back(SerializeComponent(component));
    }
    return {
        {"objectId", object.id.value},
        {"name", object.name},
        {"hierarchyPath", BuildHierarchyPath(document, object)},
        {"parentObjectId", object.parent.has_value() ? nlohmann::json(object.parent->value) : nlohmann::json(nullptr)},
        {"enabled", object.enabled},
        {"components", std::move(components)}};
}

} // namespace HIKARI::EDITOR
