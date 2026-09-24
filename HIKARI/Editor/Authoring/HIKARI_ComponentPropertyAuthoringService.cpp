#include "Editor/Authoring/HIKARI_ComponentPropertyBuilder.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::EDITOR {

namespace {

const char* CommandParameterTypeName(AuthoringCommandParameterType type) noexcept {
    switch (type) {
    case AuthoringCommandParameterType::String: return "string";
    case AuthoringCommandParameterType::Float: return "number";
    case AuthoringCommandParameterType::Int: return "integer";
    case AuthoringCommandParameterType::Bool: return "boolean";
    case AuthoringCommandParameterType::Json: return "json";
    }
    return "unknown";
}

bool MatchesCommandParameterType(const nlohmann::json& value, AuthoringCommandParameterType type) noexcept {
    switch (type) {
    case AuthoringCommandParameterType::String: return value.is_string();
    case AuthoringCommandParameterType::Float: return value.is_number();
    case AuthoringCommandParameterType::Int: return value.is_number_integer() || value.is_number_unsigned();
    case AuthoringCommandParameterType::Bool: return value.is_boolean();
    case AuthoringCommandParameterType::Json: return true;
    }
    return false;
}

struct ResolvedAuthoringTarget {
    SceneObjectData* object = nullptr;
    SceneComponentData* component = nullptr;
    const ComponentTypeInfo* typeInfo = nullptr;
    std::unique_ptr<IComponent> instance{};
};

ComponentAuthoringServiceResult ResolveTarget(const ComponentRegistry& registry, SceneDocument& document,
                                              ComponentAuthoringTarget target, ResolvedAuthoringTarget& resolved) {
    resolved.object = FindSceneObjectData(document, target.objectId);
    if (resolved.object == nullptr) {
        return MakeFailure("OBJECT_NOT_FOUND",
                           "Scene object id was not found: " + std::to_string(target.objectId.value));
    }
    resolved.component = FindSceneComponent(*resolved.object, target.componentId);
    if (resolved.component == nullptr) {
        return MakeFailure("COMPONENT_NOT_FOUND", "Component id was not found on object " +
                                                      std::to_string(target.objectId.value) + ": " +
                                                      std::to_string(target.componentId.value));
    }
    resolved.typeInfo = registry.Find(resolved.component->type);
    if (resolved.typeInfo == nullptr || !resolved.typeInfo->factory) {
        return MakeFailure("COMPONENT_TYPE_UNAVAILABLE",
                           "Component type is not registered: " + resolved.component->type);
    }
    ComponentAuthoringServiceResult result{};
    result.ok = true;
    return result;
}

ComponentAuthoringServiceResult ValidateCommandArguments(const ComponentAuthoringCommandDefinition& command,
                                                         const nlohmann::json& arguments) {
    if (!arguments.is_object()) {
        return MakeFailure("COMMAND_ARGUMENTS_NOT_OBJECT", "Component command arguments must be a JSON object.");
    }
    for (const AuthoringCommandParameter& parameter : command.parameters) {
        const auto found = arguments.find(parameter.name);
        if (found == arguments.end()) {
            if (parameter.required) {
                return MakeFailure("MISSING_COMMAND_PARAMETER",
                                   "Missing component command parameter: " + parameter.name);
            }
            continue;
        }
        if (!MatchesCommandParameterType(*found, parameter.type)) {
            return MakeFailure("COMMAND_PARAMETER_TYPE_MISMATCH", "Component command parameter '" + parameter.name +
                                                                      "' must be " +
                                                                      CommandParameterTypeName(parameter.type) + ".");
        }
    }
    for (const auto& [name, value] : arguments.items()) {
        (void)value;
        const auto known =
            std::find_if(command.parameters.begin(), command.parameters.end(),
                         [&name](const AuthoringCommandParameter& parameter) { return parameter.name == name; });
        if (known == command.parameters.end()) {
            return MakeFailure("UNKNOWN_COMMAND_PARAMETER", "Unknown component command parameter: " + name);
        }
    }
    ComponentAuthoringServiceResult result{};
    result.ok = true;
    return result;
}

ComponentAuthoringServiceResult ValidateAuthoringState(const ComponentTypeInfo& typeInfo, const IComponent& component,
                                                       const InspectorContext& context) {
    ComponentAuthoringServiceResult result{};
    if (!typeInfo.validateAuthoring) {
        result.ok = true;
        return result;
    }
    try {
        const AuthoringCommandResult validation = typeInfo.validateAuthoring(component, context);
        if (!validation.ok) {
            return MakeFailure(validation.code.empty() ? "COMPONENT_AUTHORING_INVALID" : validation.code,
                               validation.message.empty() ? "Component authoring validation failed."
                                                          : validation.message);
        }
    } catch (const std::exception& error) {
        return MakeFailure("COMPONENT_AUTHORING_VALIDATION_EXCEPTION", error.what());
    } catch (...) {
        return MakeFailure("COMPONENT_AUTHORING_VALIDATION_EXCEPTION",
                           "Component authoring validator threw an unknown exception.");
    }
    result.ok = true;
    return result;
}

ComponentAuthoringServiceResult DeserializeForAuthoring(ResolvedAuthoringTarget& resolved) {
    const std::string& componentType = resolved.component->type;
    try {
        resolved.instance = resolved.typeInfo->factory();
        if (!resolved.instance) {
            return MakeFailure("COMPONENT_FACTORY_FAILED", "Component factory returned no instance: " + componentType);
        }
        std::string diagnostic{};
        if (!resolved.instance->TryDeserialize(resolved.component->properties, diagnostic)) {
            return MakeFailure("COMPONENT_DESERIALIZATION_FAILED", "Component authoring data is invalid for " +
                                                                       std::string(componentType) +
                                                                       (diagnostic.empty() ? "." : ": " + diagnostic));
        }
    } catch (const std::exception& error) {
        return MakeFailure("COMPONENT_DESERIALIZATION_EXCEPTION",
                           "Component deserialization threw for " + std::string(componentType) + ": " + error.what());
    } catch (...) {
        return MakeFailure("COMPONENT_DESERIALIZATION_EXCEPTION",
                           "Component deserialization threw an unknown exception for " + std::string(componentType) +
                               ".");
    }
    ComponentAuthoringServiceResult result{};
    result.ok = true;
    return result;
}

ComponentAuthoringServiceResult SerializeForAuthoring(const IComponent& component, nlohmann::json& outProperties,
                                                      std::string_view componentType) {
    try {
        component.Serialize(outProperties);
    } catch (const std::exception& error) {
        return MakeFailure("COMPONENT_SERIALIZATION_EXCEPTION",
                           "Component serialization threw for " + std::string(componentType) + ": " + error.what());
    } catch (...) {
        return MakeFailure("COMPONENT_SERIALIZATION_EXCEPTION",
                           "Component serialization threw an unknown exception for " + std::string(componentType) +
                               ".");
    }
    ComponentAuthoringServiceResult result{};
    result.ok = true;
    return result;
}

ComponentAuthoringServiceResult ValidateStoredAuthoringState(const ComponentRegistry& registry, SceneDocument& document,
                                                             ComponentAuthoringTarget target,
                                                             InspectorContext context) {
    ResolvedAuthoringTarget resolved;
    const auto resolution = ResolveTarget(registry, document, target, resolved);
    if (!resolution.ok)
        return resolution;
    ComponentAuthoringServiceResult deserialized = DeserializeForAuthoring(resolved);
    if (!deserialized.ok) {
        return deserialized;
    }
    context.sceneDocument = &document;
    return ValidateAuthoringState(*resolved.typeInfo, *resolved.instance, context);
}

nlohmann::json SerializeCommands(const ComponentTypeInfo& typeInfo) {
    nlohmann::json commands = nlohmann::json::array();
    for (const ComponentAuthoringCommandDefinition& command : typeInfo.authoringCommands) {
        nlohmann::json parameters = nlohmann::json::array();
        for (const AuthoringCommandParameter& parameter : command.parameters) {
            parameters.push_back({{"name", parameter.name},
                                  {"type", CommandParameterTypeName(parameter.type)},
                                  {"required", parameter.required},
                                  {"description", parameter.description}});
        }
        commands.push_back({{"id", command.id}, {"summary", command.summary}, {"parameters", std::move(parameters)}});
    }
    return commands;
}

} // namespace

ComponentAuthoringServiceResult ComponentPropertyAuthoringService::Describe(const ComponentRegistry& registry,
                                                                            const SceneDocument& document,
                                                                            ComponentAuthoringTarget target,
                                                                            InspectorContext context) const {
    SceneDocument working = document;
    ResolvedAuthoringTarget resolved;
    const auto resolution = ResolveTarget(registry, working, target, resolved);
    if (!resolution.ok)
        return resolution;

    context.sceneDocument = &working;
    const ComponentAuthoringServiceResult deserialized = DeserializeForAuthoring(resolved);
    if (!deserialized.ok) {
        return deserialized;
    }
    ComponentAuthoringServiceResult result = DescribeComponentProperties(*resolved.instance, context);
    if (!result.ok)
        return result;

    result.message = "Component authoring schema returned.";
    result.data.update({{"objectId", target.objectId.value},
                        {"objectName", resolved.object->name},
                        {"componentId", target.componentId.value},
                        {"componentType", resolved.component->type},
                        {"commands", SerializeCommands(*resolved.typeInfo)}});
    if (result.data["fields"].empty() && !result.data["unavailableFields"].empty() &&
        resolved.typeInfo->authoringCommands.empty()) {
        result.ok = false;
        result.code = "AUTHORING_SCHEMA_INCOMPLETE";
        result.message =
            "This component still uses UI-only fields. No writable authoring schema is registered; querying again will not expose them.";
    }
    return result;
}

ComponentAuthoringServiceResult ComponentPropertyAuthoringService::Apply(
    const ComponentRegistry& registry, SceneDocument& document, std::span<const ComponentPropertyMutation> mutations,
    InspectorContext context) const {
    if (mutations.empty()) {
        return MakeFailure("EMPTY_TRANSACTION", "At least one component property mutation is required.");
    }

    SceneDocument working = document;
    context.sceneDocument = &working;
    nlohmann::json changedFields = nlohmann::json::array();
    std::vector<ComponentAuthoringTarget> changedTargets{};
    for (const ComponentPropertyMutation& mutation : mutations) {
        if (mutation.propertyId.empty()) {
            return MakeFailure("EMPTY_PROPERTY_ID", "Component property id must not be empty.");
        }
        ResolvedAuthoringTarget resolved;
        const auto resolution = ResolveTarget(registry, working, mutation.target, resolved);
        if (!resolution.ok)
            return resolution;

        const ComponentAuthoringServiceResult deserialized = DeserializeForAuthoring(resolved);
        if (!deserialized.ok) {
            return deserialized;
        }
        const auto applied = ApplyComponentProperty(*resolved.instance, context, mutation);
        if (!applied.ok)
            return applied;
        if (!applied.documentChanged)
            continue;
        nlohmann::json serialized = nlohmann::json::object();
        const ComponentAuthoringServiceResult serializedResult =
            SerializeForAuthoring(*resolved.instance, serialized, resolved.component->type);
        if (!serializedResult.ok) {
            return serializedResult;
        }
        resolved.component->properties = std::move(serialized);
        const auto knownTarget = std::find_if(
            changedTargets.begin(), changedTargets.end(), [&mutation](const ComponentAuthoringTarget& target) {
                return target.objectId == mutation.target.objectId && target.componentId == mutation.target.componentId;
            });
        if (knownTarget == changedTargets.end()) {
            changedTargets.push_back(mutation.target);
        }
        changedFields.push_back({{"objectId", mutation.target.objectId.value},
                                 {"componentId", mutation.target.componentId.value},
                                 {"componentType", resolved.component->type},
                                 {"propertyId", mutation.propertyId}});
    }
    for (const ComponentAuthoringTarget& changedTarget : changedTargets) {
        const ComponentAuthoringServiceResult validation =
            ValidateStoredAuthoringState(registry, working, changedTarget, context);
        if (!validation.ok) {
            return validation;
        }
    }

    ComponentAuthoringServiceResult result{};
    result.ok = true;
    result.documentChanged = !changedFields.empty();
    result.code = result.documentChanged ? "OK" : "NO_CHANGE";
    result.message = result.documentChanged ? "Component property transaction applied."
                                            : "Component property transaction produced no change.";
    result.data = {{"changed", std::move(changedFields)}};
    if (result.documentChanged) {
        document.objects = std::move(working.objects);
    }
    return result;
}

ComponentAuthoringServiceResult ComponentPropertyAuthoringService::ExecuteCommand(
    const ComponentRegistry& registry, SceneDocument& document, ComponentAuthoringTarget target,
    std::string_view commandId, const nlohmann::json& arguments, InspectorContext context) const {
    SceneDocument working = document;
    context.sceneDocument = &working;
    ResolvedAuthoringTarget resolved;
    const auto resolution = ResolveTarget(registry, working, target, resolved);
    if (!resolution.ok)
        return resolution;
    const auto command = std::find_if(
        resolved.typeInfo->authoringCommands.begin(), resolved.typeInfo->authoringCommands.end(),
        [commandId](const ComponentAuthoringCommandDefinition& candidate) { return candidate.id == commandId; });
    if (command == resolved.typeInfo->authoringCommands.end() || !command->execute) {
        return MakeFailure("COMPONENT_COMMAND_NOT_FOUND",
                           "Component command is not registered: " + std::string(commandId));
    }
    ComponentAuthoringServiceResult validation = ValidateCommandArguments(*command, arguments);
    if (!validation.ok) {
        return validation;
    }

    const ComponentAuthoringServiceResult deserialized = DeserializeForAuthoring(resolved);
    if (!deserialized.ok) {
        return deserialized;
    }
    AuthoringCommandResult commandResult{};
    try {
        commandResult = command->execute(*resolved.instance, arguments);
    } catch (const std::exception& error) {
        return MakeFailure("COMPONENT_COMMAND_EXCEPTION", error.what());
    } catch (...) {
        return MakeFailure("COMPONENT_COMMAND_EXCEPTION", "Component command threw an unknown exception.");
    }
    if (!commandResult.ok) {
        return MakeFailure(commandResult.code.empty() ? "COMPONENT_COMMAND_FAILED" : commandResult.code,
                           commandResult.message);
    }
    const ComponentAuthoringServiceResult authoringValidation =
        ValidateAuthoringState(*resolved.typeInfo, *resolved.instance, context);
    if (!authoringValidation.ok) {
        return authoringValidation;
    }

    nlohmann::json serialized = nlohmann::json::object();
    const ComponentAuthoringServiceResult serializedResult =
        SerializeForAuthoring(*resolved.instance, serialized, resolved.component->type);
    if (!serializedResult.ok) {
        return serializedResult;
    }
    const bool changed = serialized != resolved.component->properties;
    const std::string componentType = resolved.component->type;
    if (changed) {
        resolved.component->properties = std::move(serialized);
        document.objects = std::move(working.objects);
    }

    ComponentAuthoringServiceResult result{};
    result.ok = true;
    result.documentChanged = changed;
    result.code = changed ? "OK" : "NO_CHANGE";
    result.message = commandResult.message.empty()
                         ? (changed ? "Component command applied." : "Component command produced no change.")
                         : commandResult.message;
    result.data = {{"objectId", target.objectId.value},
                   {"componentId", target.componentId.value},
                   {"componentType", componentType},
                   {"commandId", std::string(commandId)},
                   {"result", commandResult.data}};
    return result;
}

} // namespace HIKARI::EDITOR
