#include "Editor/Authoring/HIKARI_ComponentAuthoringQueryService.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"

#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "Core/HIKARI_TimeService.h"
#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Editor/Authoring/HIKARI_ComponentPropertyAuthoringService.h"

namespace HIKARI::EDITOR {

namespace {

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
        return OperationFailure("UNKNOWN_COMPONENT_TYPE",
                                "Search component.types.query for a registered component type.");
    auto instance = type->factory();
    if (!instance)
        return OperationFailure("COMPONENT_FACTORY_FAILED", "The component factory returned no instance.");
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
                                                                   const nlohmann::json& parameters) const {
    const auto name = parameters.value("name", std::string{});
    const auto keyword = parameters.value("keyword", std::string{});
    const auto componentType = parameters.value("componentType", std::string{});
    const auto objectId = parameters.value("objectId", uint64_t{});
    if (parameters.contains("objectId") && objectId == 0)
        return OperationFailure("INVALID_OBJECT_ID", "objectId must be nonzero.");
    const int offset = (std::max)(0, parameters.value("offset", 0));
    const int limit = std::clamp(parameters.value("limit", 16), 1, 64);
    const bool detail = parameters.value("detail", false);
    const SceneDocument& document = scene.GetSceneDocument();
    auto objects = nlohmann::json::array();
    size_t matched = 0;
    for (const auto& object : document.objects) {
        if ((!name.empty() && object.name != name) || (objectId && object.id.value != objectId) ||
            (!keyword.empty() && object.name.find(keyword) == std::string::npos))
            continue;
        if (!componentType.empty() &&
            std::none_of(object.components.begin(), object.components.end(),
                         [&componentType](const auto& component) { return component.type == componentType; }))
            continue;
        if (matched++ < static_cast<size_t>(offset) || objects.size() >= static_cast<size_t>(limit))
            continue;
        if (!detail) {
            objects.push_back(SummarizeObject(scene, object));
            continue;
        }
        auto item = SerializeObject(document, object);
        item.update(ObserveSceneObject(scene, object));
        objects.push_back(std::move(item));
    }
    const auto count = objects.size();
    if (!detail) {
        auto summary = OperationOk("Scene object summaries returned; pass detail for models, material slots and "
                                   "screen anchors.",
                                   {{"count", count},
                                    {"total", matched},
                                    {"objects", std::move(objects)},
                                    {"sceneRevision", scene.GetSceneDocumentRevision()}});
        if (static_cast<size_t>(offset) + count < matched)
            summary.data["nextOffset"] = offset + count;
        return summary;
    }
    auto result = OperationOk(
        "Scene object observations returned.",
        {{"count", count},
         {"total", matched},
         {"objects", std::move(objects)},
         {"sceneRevision", scene.GetSceneDocumentRevision()},
         {"frameIndex", TIME::GetFrameContext().frameIndex},
         {"screenSpace",
          "Current scene camera, normalized top-left UV; projected anchor, no occlusion test. Bounds are conservative, not pixel silhouettes or an earlier screenshot snapshot."}});
    result.data["cameraObjectId"] = scene.GetResolvedCameraFrame().sourceCameraObjectId;
    result.data["cameraRevision"] = scene.GetResolvedCameraFrame().revision;
    if (static_cast<size_t>(offset) + count < matched)
        result.data["nextOffset"] = offset + count;
    return result;
}

EditorOperationResult ComponentAuthoringQueryService::QueryComponents(const DocumentSceneBase& scene,
                                                                      uint64_t objectId) const {
    const SceneDocument& document = scene.GetSceneDocument();
    const SceneObjectData* object = FindSceneObjectData(document, SceneObjectId{objectId});
    if (object == nullptr) {
        return OperationFailure("OBJECT_NOT_FOUND", "Scene object id was not found: " + std::to_string(objectId));
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

nlohmann::json ComponentAuthoringQueryService::SummarizeObject(const DocumentSceneBase& scene,
                                                               const SceneObjectData& object) const {
    nlohmann::json components = nlohmann::json::array();
    for (const SceneComponentData& component : object.components)
        components.push_back(SerializeComponent(component));
    nlohmann::json result{{"objectId", object.id.value}, {"name", object.name}, {"components", std::move(components)}};
    if (object.parent.has_value())
        result["parentObjectId"] = object.parent->value;
    if (!object.enabled)
        result["enabled"] = false;
    if (const GameObject* runtime = scene.GetWorld().FindObject(object.id)) {
        const MATH::Mat4 world = runtime->GetTransform().GetWorldMatrix();
        result["worldPosition"] = {world.m[3][0], world.m[3][1], world.m[3][2]};
    }
    if (const Bounds bounds = ObserveSceneObjectBounds(scene, object.id); BOUNDS::IsUsable(bounds))
        result["worldBounds"] = {{"min", {bounds.min.x, bounds.min.y, bounds.min.z}},
                                 {"max", {bounds.max.x, bounds.max.y, bounds.max.z}}};
    return result;
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
