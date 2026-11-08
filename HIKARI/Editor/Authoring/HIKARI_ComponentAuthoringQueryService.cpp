#include "Editor/Authoring/HIKARI_ComponentAuthoringQueryService.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"

#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "Core/HIKARI_TimeService.h"
#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"
#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
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

// 程序化の方块に開けた门・窗・洞の数（procedural.opening の index は 0 から）。摘要と detail の両方に付ける。
void AddOpeningCount(const SceneObjectData& object, nlohmann::json& out) {
    for (const SceneComponentData& component : object.components) {
        const auto cuts = component.properties.find("cuts");
        if (component.type == "ProceduralMeshComponent" && cuts != component.properties.end() && cuts->is_array() &&
            !cuts->empty())
            out["openings"] = cuts->size();
    }
}

// 項目表の種類を、検査器の field.type と同じ語にする。
const char* FieldKindName(const JSONFIELDS::FieldInfo& info) noexcept {
    using Kind = JSONFIELDS::FieldKind;
    switch (info.kind) {
    case Kind::Bool: return "bool";
    case Kind::Int: return "int";
    case Kind::Float: return "float";
    case Kind::Vec2: return "vec2";
    case Kind::Vec3: return info.color ? "color3" : "vec3";
    case Kind::String: return "string";
    case Kind::Enum: return "choice";
    case Kind::AssetRef: return "assetId";
    case Kind::ObjectRef: return "sceneObjectId";
    case Kind::EventRef: return "runtimeEventId";
    case Kind::VariableRef: return "runtimeVariableId";
    case Kind::Elements: return "list";
    case Kind::Nested: return "struct";
    }
    return "unknown";
}

// 項目表の説明（単位・範囲・節・常用/高度・列挙の保存名・子項目）。条件付きの項目は conditional で示す。
nlohmann::json DescribeFieldInfos(const std::vector<JSONFIELDS::FieldInfo>& infos) {
    nlohmann::json list = nlohmann::json::array();
    for (const JSONFIELDS::FieldInfo& info : infos) {
        nlohmann::json entry{{"id", info.id}, {"label", info.label}, {"type", FieldKindName(info)}};
        const auto set = [&entry](const char* key, const std::string& text) {
            if (!text.empty())
                entry[key] = text;
        };
        set("help", info.help);
        set("unit", info.unit);
        set("group", info.group);
        if (info.hasRange) {
            entry["minimum"] = info.minimum;
            entry["maximum"] = info.maximum;
        }
        const std::pair<const char*, bool> flags[] = {{"slider", info.slider},     {"common", info.common},
                                                      {"advanced", info.advanced}, {"readOnly", info.readOnly},
                                                      {"hidden", info.hidden},     {"conditional", info.hasShow}};
        for (const auto& [key, enabled] : flags) {
            if (enabled)
                entry[key] = true;
        }
        if (info.kind == JSONFIELDS::FieldKind::AssetRef)
            entry["assetType"] = std::string(ASSETS::SEMANTICS::ToString(static_cast<AssetType>(info.assetType)));
        if (info.kind == JSONFIELDS::FieldKind::Enum)
            entry["choices"] = info.enumNames;
        if (!info.children.empty())
            entry["fields"] = DescribeFieldInfos(info.children);
        list.push_back(std::move(entry));
    }
    return list;
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
    nlohmann::json inactive = nlohmann::json::array();
    for (const RuntimeFeatureInfo& info : scene.GetRuntimeFeatureCatalog().GetFeatureInfos()) {
        std::vector<std::string> components;
        for (const std::string& component : info.componentTypeNames) {
            if (!info.active && (filter.empty() || folded(component).find(filter) != std::string::npos))
                components.push_back(component);
        }
        if (!components.empty())
            inactive.push_back({{"feature", info.featureId}, {"components", std::move(components)}});
    }
    if (!inactive.empty()) {
        result.data["inactiveFeatures"] = std::move(inactive);
        result.hints.push_back("Components of inactive features need the feature enabled first: "
                               "project.features.edit {\"enable\": [feature]}.");
    }
    return result;
}

EditorOperationResult ComponentAuthoringQueryService::DescribeType(DocumentSceneBase& scene,
                                                                   std::string_view componentType) const {
    const auto* type = scene.GetComponentRegistry().Find(componentType);
    if (!type) {
        if (const std::string feature = scene.GetRuntimeFeatureCatalog().FindOwningFeatureForComponent(componentType);
            !feature.empty())
            return OperationFailure("FEATURE_DISABLED", std::string(componentType) +
                                                            " belongs to the runtime feature " + feature +
                                                            ", which this project has not enabled. Use "
                                                            "project.features.edit {\"enable\": [\"" +
                                                            feature + "\"]}.");
        return OperationFailure("UNKNOWN_COMPONENT_TYPE",
                                "Search component.types.query for a registered component type.");
    }
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
    if (described.ok && !type->fields.empty())
        described.data["schema"] = DescribeFieldInfos(type->fields);
    return {described.ok, described.code, described.message, std::move(described.data)};
}

EditorOperationResult ComponentAuthoringQueryService::QueryObjects(const DocumentSceneBase& scene,
                                                                   const nlohmann::json& parameters) const {
    const auto name = parameters.value("name", std::string{});
    const auto keyword = parameters.value("keyword", std::string{});
    const auto componentType = parameters.value("componentType", std::string{});
    const std::vector<uint64_t> objectIds = parameters.value("objectIds", std::vector<uint64_t>{});
    const int offset = (std::max)(0, parameters.value("offset", 0));
    const int limit = std::clamp(parameters.value("limit", 16), 1, 64);
    const bool detail = parameters.value("detail", false);
    const SceneDocument& document = scene.GetSceneDocument();
    auto objects = nlohmann::json::array();
    size_t matched = 0;
    for (const auto& object : document.objects) {
        if ((!name.empty() && object.name != name) ||
            (!objectIds.empty() && std::find(objectIds.begin(), objectIds.end(), object.id.value) == objectIds.end()) ||
            (!keyword.empty() && object.name.find(keyword) == std::string::npos))
            continue;
        if (!componentType.empty() && object.FindComponent(componentType) == nullptr)
            continue;
        if (matched++ < static_cast<size_t>(offset) || objects.size() >= static_cast<size_t>(limit))
            continue;
        if (!detail) {
            objects.push_back(SummarizeObject(scene, object));
            continue;
        }
        auto item = SerializeObject(document, object);
        item.update(ObserveSceneObject(scene, object));
        AddOpeningCount(object, item);
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
    AddOpeningCount(object, result);
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
    nlohmann::json described = {
        {"objectId", object.id.value},
        {"name", object.name},
        {"hierarchyPath", BuildHierarchyPath(document, object)},
        {"parentObjectId", object.parent.has_value() ? nlohmann::json(object.parent->value) : nlohmann::json(nullptr)},
        {"enabled", object.enabled},
        {"components", std::move(components)}};
    if (document.partition.enabled) {
        // 分区場景では保存先（主ファイルか格子）を示す。子は根に従う。
        const SceneObjectData* root = &object;
        for (size_t step = 0u; step < document.objects.size() && root->parent.has_value(); ++step) {
            const SceneObjectData* parent = FindSceneObjectData(document, *root->parent);
            if (parent == nullptr) {
                break;
            }
            root = parent;
        }
        described["alwaysLoaded"] = object.alwaysLoaded;
        described["storedIn"] =
            root->alwaysLoaded ? std::string("main")
                               : WorldCellFileStem(WorldCellAt(root->transform.position.x, root->transform.position.z,
                                                               document.partition.cellSizeMeters));
    }
    return described;
}

} // namespace HIKARI::EDITOR
