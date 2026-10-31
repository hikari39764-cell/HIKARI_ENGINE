#include "Editor/Authoring/HIKARI_ComponentAuthoringOperations.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Editor/Authoring/HIKARI_ComponentAuthoringQueryService.h"
#include "Editor/Authoring/HIKARI_ComponentPropertyAuthoringService.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::EDITOR {

namespace {

constexpr size_t kMaxTransactionMutations = 256u;

bool ReadRequiredId(const nlohmann::json& parameters, const char* name, uint64_t& outValue,
                    EditorOperationResult& outFailure) {
    const auto found = parameters.find(name);
    if (found == parameters.end() || (!found->is_number_unsigned() && !found->is_number_integer())) {
        outFailure = OperationFailure("INVALID_ID", std::string(name) + " must be a positive integer id.");
        return false;
    }
    if (!found->is_number_unsigned() && found->get<int64_t>() <= 0) {
        outFailure = OperationFailure("INVALID_ID", std::string(name) + " must be a positive integer id.");
        return false;
    }
    outValue = found->get<uint64_t>();
    if (outValue == 0u) {
        outFailure = OperationFailure("INVALID_ID", std::string(name) + " must be a positive integer id.");
        return false;
    }
    return true;
}

EditorOperationResult ToOperationResult(ComponentAuthoringServiceResult serviceResult) {
    EditorOperationResult result{};
    result.ok = serviceResult.ok;
    result.code = std::move(serviceResult.code);
    result.message = std::move(serviceResult.message);
    result.data = std::move(serviceResult.data);
    return result;
}

class ComponentAuthoringOperationHandler {
public:
    explicit ComponentAuthoringOperationHandler(ComponentAuthoringOperationContext context)
        : context_(std::move(context)) {}

    EditorOperationResult QueryTypes(const nlohmann::json& parameters) const {
        auto* scene = GetScene();
        return scene ? queryService_.QueryTypes(*scene, parameters.value("keyword", std::string{}),
                                                parameters.value("offset", 0), parameters.value("limit", 16))
                     : NoScene();
    }

    EditorOperationResult DescribeType(const nlohmann::json& parameters) const {
        auto* scene = GetScene();
        return scene ? queryService_.DescribeType(*scene, parameters.at("componentType").get<std::string>())
                     : NoScene();
    }

    EditorOperationResult QueryObjects(const nlohmann::json& parameters) const {
        DocumentSceneBase* scene = GetScene();
        if (scene == nullptr) {
            return NoScene();
        }
        return queryService_.QueryObjects(*scene, parameters);
    }

    EditorOperationResult QueryComponents(const nlohmann::json& parameters) const {
        DocumentSceneBase* scene = GetScene();
        if (scene == nullptr) {
            return NoScene();
        }
        EditorOperationResult failure{};
        uint64_t objectId = 0u;
        if (!ReadRequiredId(parameters, "objectId", objectId, failure)) {
            return failure;
        }
        return queryService_.QueryComponents(*scene, objectId);
    }

    EditorOperationResult DescribeComponent(const nlohmann::json& parameters) const {
        DocumentSceneBase* scene = GetScene();
        if (scene == nullptr) {
            return NoScene();
        }
        ComponentAuthoringTarget target{};
        EditorOperationResult failure{};
        if (!ReadTarget(parameters, target, failure)) {
            return failure;
        }
        EditorOperationResult result = ToOperationResult(propertyService_.Describe(
            scene->GetComponentRegistry(), scene->GetSceneDocument(), target, BuildInspectorContext(*scene)));
        if (result.ok)
            TrimDescribedFields(result.data, parameters);
        return result;
    }

    // 智能体の文脈を小さく保つ：propertyIds で字段を絞り、資産の一覧のような大きな選択肢は数だけにする
    // （material.list_assets などに詳しい一覧がある。includeChoices で全部）。
    static void TrimDescribedFields(nlohmann::json& data, const nlohmann::json& parameters) {
        constexpr size_t kMaximumInlineChoices = 16u;
        const nlohmann::json wanted = parameters.value("propertyIds", nlohmann::json::array());
        const bool includeChoices = parameters.value("includeChoices", false);
        if (!data.contains("fields") || !data["fields"].is_array())
            return;
        nlohmann::json kept = nlohmann::json::array();
        nlohmann::json unknown = wanted;
        for (nlohmann::json& field : data["fields"]) {
            const std::string id = field.value("id", std::string{});
            if (!wanted.empty() && std::find(wanted.begin(), wanted.end(), id) == wanted.end())
                continue;
            unknown.erase(std::remove(unknown.begin(), unknown.end(), id), unknown.end());
            const auto choices = field.find("choices");
            if (!includeChoices && choices != field.end() && choices->is_array() &&
                choices->size() > kMaximumInlineChoices) {
                field["choiceCount"] = choices->size();
                field.erase("choices");
            }
            kept.push_back(std::move(field));
        }
        data["fields"] = std::move(kept);
        // 今の状態に無い字段（球体の height など）か ID の間違いかを、智能体が見分けられるように返す。
        if (!unknown.empty())
            data["unknownPropertyIds"] = std::move(unknown);
    }

    EditorOperationResult SetProperty(const nlohmann::json& parameters) {
        ComponentAuthoringTarget target{};
        EditorOperationResult failure{};
        if (!ReadTarget(parameters, target, failure)) {
            return failure;
        }
        ComponentPropertyMutation mutation{target, parameters.value("propertyId", std::string{}),
                                           parameters.at("value")};
        return ApplyMutations(std::span<const ComponentPropertyMutation>(&mutation, 1u),
                              "Edit Component " + mutation.propertyId);
    }

    EditorOperationResult ApplyTransaction(const nlohmann::json& parameters) {
        const nlohmann::json& commands = parameters.at("commands");
        if (!commands.is_array() || commands.empty()) {
            return OperationFailure("EMPTY_TRANSACTION", "commands must be a non-empty array.");
        }
        if (commands.size() > kMaxTransactionMutations) {
            return OperationFailure("TRANSACTION_TOO_LARGE", "A component authoring transaction supports at most " +
                                                                 std::to_string(kMaxTransactionMutations) +
                                                                 " mutations.");
        }
        std::vector<ComponentPropertyMutation> mutations{};
        mutations.reserve(commands.size());
        for (const nlohmann::json& command : commands) {
            if (!command.is_object() || !command.contains("propertyId") || !command["propertyId"].is_string() ||
                !command.contains("value")) {
                return OperationFailure("INVALID_TRANSACTION_COMMAND", "Each transaction command requires objectId, "
                                                                       "componentId, propertyId, and value.");
            }
            ComponentAuthoringTarget target{};
            EditorOperationResult failure{};
            if (!ReadTarget(command, target, failure)) {
                return failure;
            }
            mutations.push_back({target, command["propertyId"].get<std::string>(), command["value"]});
        }
        return ApplyMutations(mutations, "Edit Component Properties");
    }

    EditorOperationResult ExecuteCommand(const nlohmann::json& parameters) {
        DocumentSceneBase* scene = GetScene();
        if (scene == nullptr) {
            return NoScene();
        }
        ComponentAuthoringTarget target{};
        EditorOperationResult failure{};
        if (!ReadTarget(parameters, target, failure)) {
            return failure;
        }
        const std::vector<SceneObjectData> beforeObjects = scene->GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera = scene->GetSceneDocument().camera;
        const bool dirtyBefore = IsDirty(*scene);
        ComponentAuthoringServiceResult serviceResult = propertyService_.ExecuteCommand(
            scene->GetComponentRegistry(), scene->GetSceneDocument(), target,
            parameters.value("commandId", std::string{}), parameters.at("arguments"), BuildInspectorContext(*scene));
        EditorOperationResult result = ToOperationResult(std::move(serviceResult));
        if (result.ok && result.code != "NO_CHANGE") {
            Commit(*scene, "Execute Component Command", beforeObjects, beforeCamera, dirtyBefore);
        }
        return result;
    }

    EditorOperationResult AddComponent(const nlohmann::json& parameters) {
        DocumentSceneBase* scene = GetScene();
        if (scene == nullptr) {
            return NoScene();
        }
        EditorOperationResult failure{};
        uint64_t objectId = 0u;
        if (!ReadRequiredId(parameters, "objectId", objectId, failure)) {
            return failure;
        }
        SceneObjectData* object = FindSceneObjectData(scene->GetSceneDocument(), SceneObjectId{objectId});
        if (object == nullptr) {
            return OperationFailure("OBJECT_NOT_FOUND", "Scene object id was not found: " + std::to_string(objectId));
        }
        const std::vector<SceneObjectData> beforeObjects = scene->GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera = scene->GetSceneDocument().camera;
        const bool dirtyBefore = IsDirty(*scene);
        std::unordered_set<uint64_t> existingIds{};
        for (const SceneComponentData& component : object->components) {
            existingIds.insert(component.id.value);
        }
        const ComponentAddResult addResult = structureService_.AddComponent(
            scene->GetComponentRegistry(), *object, parameters.value("componentType", std::string{}));
        if (!addResult.success) {
            return OperationFailure("COMPONENT_ADD_REJECTED", addResult.message);
        }
        nlohmann::json added = nlohmann::json::array();
        for (const SceneComponentData& component : object->components) {
            if (!existingIds.contains(component.id.value)) {
                added.push_back(queryService_.SerializeComponent(component));
            }
        }
        Commit(*scene, "Add Component", beforeObjects, beforeCamera, dirtyBefore);
        EditorOperationResult result{};
        result.ok = true;
        result.code = "OK";
        result.message = addResult.message;
        result.data = {{"objectId", objectId}, {"addedComponents", std::move(added)}};
        return result;
    }

    EditorOperationResult RemoveComponent(const nlohmann::json& parameters) {
        DocumentSceneBase* scene = GetScene();
        if (scene == nullptr) {
            return NoScene();
        }
        ComponentAuthoringTarget target{};
        EditorOperationResult failure{};
        if (!ReadTarget(parameters, target, failure)) {
            return failure;
        }
        SceneObjectData* object = FindSceneObjectData(scene->GetSceneDocument(), target.objectId);
        if (object == nullptr) {
            return OperationFailure("OBJECT_NOT_FOUND",
                                    "Scene object id was not found: " + std::to_string(target.objectId.value));
        }
        const std::optional<size_t> componentIndex = FindSceneComponentIndex(*object, target.componentId);
        if (!componentIndex.has_value()) {
            return OperationFailure("COMPONENT_NOT_FOUND",
                                    "Component id was not found: " + std::to_string(target.componentId.value));
        }
        const std::string componentType = object->components[*componentIndex].type;
        const std::vector<SceneObjectData> beforeObjects = scene->GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera = scene->GetSceneDocument().camera;
        const bool dirtyBefore = IsDirty(*scene);
        const ComponentEditResult editResult = structureService_.RemoveComponent(
            scene->GetComponentRegistry(), scene->GetSceneDocument(), *object, *componentIndex);
        if (!editResult.success) {
            return OperationFailure("COMPONENT_REMOVE_REJECTED", editResult.message);
        }
        Commit(*scene, "Remove " + componentType, beforeObjects, beforeCamera, dirtyBefore);
        EditorOperationResult result{};
        result.ok = true;
        result.code = "OK";
        result.message = editResult.message;
        result.data = {{"objectId", target.objectId.value},
                       {"componentId", target.componentId.value},
                       {"componentType", componentType}};
        return result;
    }

    EditorOperationResult SelectObject(const nlohmann::json& parameters) const {
        DocumentSceneBase* scene = GetScene();
        if (scene == nullptr) {
            return NoScene();
        }
        EditorOperationResult failure{};
        uint64_t objectId = 0u;
        if (!ReadRequiredId(parameters, "objectId", objectId, failure)) {
            return failure;
        }
        const SceneObjectData* object = FindSceneObjectData(scene->GetSceneDocument(), SceneObjectId{objectId});
        if (object == nullptr) {
            return OperationFailure("OBJECT_NOT_FOUND", "Scene object id was not found: " + std::to_string(objectId));
        }
        if (context_.selectObject) {
            context_.selectObject(*scene, SceneObjectId{objectId});
        }
        EditorOperationResult result{};
        result.ok = true;
        result.code = "OK";
        result.message = "Scene object selected.";
        result.data = queryService_.SerializeObject(scene->GetSceneDocument(), *object);
        return result;
    }

private:
    DocumentSceneBase* GetScene() const { return context_.sceneProvider ? context_.sceneProvider() : nullptr; }

    EditorOperationResult NoScene() const {
        return OperationFailure("SCENE_UNAVAILABLE", "No active Scene document is available.");
    }

    bool IsDirty(const DocumentSceneBase& scene) const {
        return context_.dirtyProvider ? context_.dirtyProvider(scene) : scene.HasUnsavedSceneChanges();
    }

    InspectorContext BuildInspectorContext(DocumentSceneBase& scene) const {
        return InspectorContext{&scene.GetAssetRegistry(),
                                &scene.GetAssetDatabase(),
                                &scene.GetSceneDocument(),
                                &scene.GetWorld().Services(),
                                nullptr,
                                context_.modelMaterialSlotProvider};
    }

    bool ReadTarget(const nlohmann::json& parameters, ComponentAuthoringTarget& outTarget,
                    EditorOperationResult& outFailure) const {
        return ReadRequiredId(parameters, "objectId", outTarget.objectId.value, outFailure) &&
               ReadRequiredId(parameters, "componentId", outTarget.componentId.value, outFailure);
    }

    EditorOperationResult ApplyMutations(std::span<const ComponentPropertyMutation> mutations, std::string label) {
        DocumentSceneBase* scene = GetScene();
        if (scene == nullptr) {
            return NoScene();
        }
        const std::vector<SceneObjectData> beforeObjects = scene->GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera = scene->GetSceneDocument().camera;
        const bool dirtyBefore = IsDirty(*scene);
        ComponentAuthoringServiceResult serviceResult = propertyService_.Apply(
            scene->GetComponentRegistry(), scene->GetSceneDocument(), mutations, BuildInspectorContext(*scene));
        EditorOperationResult result = ToOperationResult(std::move(serviceResult));
        if (result.ok && result.code != "NO_CHANGE") {
            Commit(*scene, std::move(label), beforeObjects, beforeCamera, dirtyBefore);
        }
        return result;
    }

    void Commit(DocumentSceneBase& scene, std::string label, const std::vector<SceneObjectData>& beforeObjects,
                const SceneCameraSettings& beforeCamera, bool dirtyBefore) {
        if (!context_.commitHistory) {
            return;
        }
        SceneObjectAuthoringHistoryRequest history{};
        history.label = std::move(label);
        history.beforeObjects = beforeObjects;
        history.afterObjects = scene.GetSceneDocument().objects;
        history.beforeCamera = beforeCamera;
        history.afterCamera = scene.GetSceneDocument().camera;
        history.dirtyBefore = dirtyBefore;
        history.runtimeWorldAffected = true;
        history.runtimeWorldRefreshRequired = true;
        context_.commitHistory(scene, std::move(history));
    }

    ComponentAuthoringOperationContext context_{};
    ComponentAuthoringQueryService queryService_{};
    ComponentPropertyAuthoringService propertyService_{};
    DocumentComponentAuthoringService structureService_{};
};

} // namespace

void RegisterComponentAuthoringOperations(EditorOperationRegistry& registry,
                                          ComponentAuthoringOperationContext context) {
    auto handler = std::make_shared<ComponentAuthoringOperationHandler>(std::move(context));
    using ParameterType = EditorOperationParameterType;

    registry.Register(
        {"component.types.query",
         "Search registered component types by name, category or purpose, including dependencies. 组件目录 / コンポーネント一覧.",
         "scene",
         {{"keyword", ParameterType::String, false, "Component name, category or purpose"},
          {"offset", ParameterType::Int, false, "Next page offset"},
          {"limit", ParameterType::Int, false, "Page size 1 to 64, default 16"}},
         [handler](const nlohmann::json& parameters) { return handler->QueryTypes(parameters); }},
        EditorOperationKind::Query, false);
    registry.Register(
        {"component.type.describe",
         "Describe default editable fields and commands before adding a component. Instance fields may depend on its current settings. "
         "Components with a field table also return schema: every declared field with unit, range, group, common/advanced flags, choices and conditional visibility, including list elements.",
         "scene",
         {{"componentType", ParameterType::String, true, "Type from component.types.query"}},
         [handler](const nlohmann::json& parameters) { return handler->DescribeType(parameters); }},
        EditorOperationKind::Query, false);
    registry.Register(
        {"scene.objects.query",
         "Page through scene identities, transforms, world bounds, model sources, material slots and current-camera screen anchors. No occlusion inference.",
         "scene",
         {{"name", ParameterType::String, false,
           "Optional exact object name filter; duplicate matches are all returned."},
          {"componentType", ParameterType::String, false, "Optional registered component type filter."},
          {"objectIds",
           ParameterType::Json,
           false,
           "Optional stable object ids, e.g. [12, 14].",
           {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 1}}}}},
          {"keyword", ParameterType::String, false, "Optional name substring."},
          {"offset", ParameterType::Int, false, "Page offset, default 0."},
          {"limit", ParameterType::Int, false, "Page size 1..64, default 16."},
          {"detail", ParameterType::Bool, false,
           "Return full observations (local transform, model, material slots, screen anchor); default is a compact "
           "summary with ids, components, world position and bounds."}},
         [handler](const nlohmann::json& parameters) { return handler->QueryObjects(parameters); }},
        HIKARI::EDITOR::EditorOperationKind::Query);
    registry.Register(
        {"scene.object.select",
         "Select one Scene object by stable objectId for UI reveal. Authoring operations do not require selection.",
         "scene",
         {{"objectId", ParameterType::Int, true, "Stable SceneObjectId returned by scene.objects.query."}},
         [handler](const nlohmann::json& parameters) { return handler->SelectObject(parameters); }},
        HIKARI::EDITOR::EditorOperationKind::Session, false);
    registry.Register({"component.instances.query",
                       "List stable component instance ids on one Scene object.",
                       "scene",
                       {{"objectId", ParameterType::Int, true, "Stable SceneObjectId."}},
                       [handler](const nlohmann::json& parameters) { return handler->QueryComponents(parameters); }},
                      HIKARI::EDITOR::EditorOperationKind::Query);
    registry.Register(
        {"component.describe",
         "Return typed writable fields and semantic commands declared by one component instance.",
         "scene",
         {{"objectId", ParameterType::Int, true, "Stable SceneObjectId."},
          {"componentId", ParameterType::Int, true, "Stable SceneComponentId within the object."},
          {"propertyIds",
           ParameterType::Json,
           false,
           "Only these field ids",
           {{"type", "array"}, {"items", {{"type", "string"}}}}},
          {"includeChoices", ParameterType::Bool, false,
           "List every choice of long choice fields (asset lists); by default they show choiceCount only"}},
         [handler](const nlohmann::json& parameters) { return handler->DescribeComponent(parameters); }},
        HIKARI::EDITOR::EditorOperationKind::Query);
    registry.Register(
        {"component.property.set",
         "Set one typed component field through its shared Inspector/Authoring declaration and record Document History.",
         "scene",
         {{"objectId", ParameterType::Int, true, "Stable SceneObjectId."},
          {"componentId", ParameterType::Int, true, "Stable SceneComponentId."},
          {"propertyId", ParameterType::String, true, "Stable property id returned by component.describe."},
          {"value", ParameterType::Json, true, "Typed property value matching component.describe."}},
         [handler](const nlohmann::json& parameters) { return handler->SetProperty(parameters); }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register(
        {"authoring.transaction",
         "Atomically apply up to 256 component property mutations as one Undo step; any failure rolls back the whole transaction.",
         "scene",
         {{"commands", ParameterType::Json, true, "Array of {objectId, componentId, propertyId, value} mutations."}},
         [handler](const nlohmann::json& parameters) { return handler->ApplyTransaction(parameters); }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register(
        {"component.command.execute",
         "Execute a semantic command declared by the target component; query component.describe for command ids and parameters.",
         "scene",
         {{"objectId", ParameterType::Int, true, "Stable SceneObjectId."},
          {"componentId", ParameterType::Int, true, "Stable SceneComponentId."},
          {"commandId", ParameterType::String, true, "Command id returned by component.describe."},
          {"arguments", ParameterType::Json, true, "Command argument object matching component.describe."}},
         [handler](const nlohmann::json& parameters) { return handler->ExecuteCommand(parameters); }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"component.add",
                       "Add a registered component type and required dependencies to an exact Scene object.",
                       "scene",
                       {{"objectId", ParameterType::Int, true, "Stable SceneObjectId."},
                        {"componentType", ParameterType::String, true, "Registered component type name."}},
                       [handler](const nlohmann::json& parameters) { return handler->AddComponent(parameters); }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"component.remove",
                       "Remove one exact component instance while enforcing component dependency rules.",
                       "scene",
                       {{"objectId", ParameterType::Int, true, "Stable SceneObjectId."},
                        {"componentId", ParameterType::Int, true, "Stable SceneComponentId."}},
                       [handler](const nlohmann::json& parameters) { return handler->RemoveComponent(parameters); }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
}

} // namespace HIKARI::EDITOR
