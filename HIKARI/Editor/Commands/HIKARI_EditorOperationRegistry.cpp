#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

#include <algorithm>
#include <exception>

namespace HIKARI::EDITOR {

namespace {

const char* ParameterTypeName(
    EditorOperationParameterType type) noexcept {
    switch (type) {
    case EditorOperationParameterType::String:
        return "string";
    case EditorOperationParameterType::Float:
        return "number";
    case EditorOperationParameterType::Int:
        return "integer";
    case EditorOperationParameterType::Bool:
        return "boolean";
    case EditorOperationParameterType::Json:
        return "json";
    }
    return "unknown";
}

bool MatchesParameterType(
    const nlohmann::json& value,
    EditorOperationParameterType type) noexcept {
    switch (type) {
    case EditorOperationParameterType::String:
        return value.is_string();
    case EditorOperationParameterType::Float:
        return value.is_number();
    case EditorOperationParameterType::Int:
        return value.is_number_integer() ||
            value.is_number_unsigned();
    case EditorOperationParameterType::Bool:
        return value.is_boolean();
    case EditorOperationParameterType::Json:
        return true;
    }
    return false;
}

} // namespace

void EditorOperationRegistry::Register(
    EditorOperationDefinition definition) {
    if (definition.id.empty() || !definition.execute) {
        return;
    }
    operations_[definition.id] = std::move(definition);
}

bool EditorOperationRegistry::IsRegistered(
    const std::string& id) const noexcept {
    return operations_.find(id) != operations_.end();
}

EditorOperationResult EditorOperationRegistry::Validate(
    const EditorOperationDefinition& definition,
    const nlohmann::json& parameters) const {
    EditorOperationResult result{};
    if (!parameters.is_object()) {
        result.code = "PARAMETERS_NOT_OBJECT";
        result.message =
            "Operation parameters must be a JSON object: " +
            definition.id;
        return result;
    }

    for (const EditorOperationParameter& parameter :
        definition.parameters) {
        const auto found = parameters.find(parameter.name);
        if (found == parameters.end()) {
            if (!parameter.required) {
                continue;
            }
            result.code = "MISSING_PARAMETER";
            result.message =
                "Missing required parameter: " + parameter.name;
            result.details.push_back(
                "operation: " + definition.id);
            return result;
        }
        if (!MatchesParameterType(*found, parameter.type)) {
            result.code = "PARAMETER_TYPE_MISMATCH";
            result.message =
                "Parameter '" + parameter.name + "' must be " +
                ParameterTypeName(parameter.type) + ", received " +
                found->type_name();
            result.details.push_back(
                "operation: " + definition.id);
            return result;
        }
    }

    for (const auto& [name, value] : parameters.items()) {
        (void)value;
        if (name == "action") {
            continue;
        }
        const auto known = std::find_if(
            definition.parameters.begin(),
            definition.parameters.end(),
            [&name](const EditorOperationParameter& parameter) {
                return parameter.name == name;
            });
        if (known == definition.parameters.end()) {
            result.code = "UNKNOWN_PARAMETER";
            result.message = "Unknown parameter: " + name;
            result.details.push_back(
                "operation: " + definition.id);
            return result;
        }
    }

    result.ok = true;
    return result;
}

EditorOperationResult EditorOperationRegistry::Execute(
    const std::string& id,
    const nlohmann::json& parameters) const {
    const auto found = operations_.find(id);
    if (found == operations_.end()) {
        EditorOperationResult result{};
        result.code = "OPERATION_NOT_FOUND";
        result.message = "Unregistered editor operation: " + id;
        return result;
    }
    EditorOperationResult validation =
        Validate(found->second, parameters);
    if (!validation.ok) {
        return validation;
    }
    try {
        return found->second.execute(parameters);
    } catch (const std::exception& error) {
        EditorOperationResult result{};
        result.code = "OPERATION_EXCEPTION";
        result.message =
            "Editor operation failed with an exception: " + id;
        result.details.push_back(error.what());
        return result;
    } catch (...) {
        EditorOperationResult result{};
        result.code = "OPERATION_EXCEPTION";
        result.message =
            "Editor operation failed with an unknown exception: " + id;
        return result;
    }
}

EditorOperationResult EditorOperationRegistry::Execute(
    const std::string& id) const {
    return Execute(id, nlohmann::json::object());
}

std::vector<std::string> EditorOperationRegistry::RegisteredIds() const {
    std::vector<std::string> ids{};
    ids.reserve(operations_.size());
    for (const auto& [id, definition] : operations_) {
        (void)definition;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

const EditorOperationDefinition* EditorOperationRegistry::Find(
    const std::string& id) const noexcept {
    const auto found = operations_.find(id);
    return found != operations_.end()
        ? &found->second
        : nullptr;
}

std::vector<const EditorOperationDefinition*>
EditorOperationRegistry::QueryWorkspace(
    const std::string& workspace) const {
    std::vector<const EditorOperationDefinition*> definitions{};
    for (const auto& [id, definition] : operations_) {
        (void)id;
        if (definition.workspace == workspace) {
            definitions.push_back(&definition);
        }
    }
    std::sort(
        definitions.begin(),
        definitions.end(),
        [](const EditorOperationDefinition* lhs,
           const EditorOperationDefinition* rhs) {
            return lhs->id < rhs->id;
        });
    return definitions;
}

} // namespace HIKARI::EDITOR
