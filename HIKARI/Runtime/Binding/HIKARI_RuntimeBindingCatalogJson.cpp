#include "Runtime/Binding/HIKARI_RuntimeBindingCatalogJson.h"

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI::BINDING {
namespace {

nlohmann::json WriteValue(const Value& value) {
    nlohmann::json node{{"type", ToString(value.type)}};
    switch (value.type) {
    case ValueType::Bool: node["value"] = value.boolean; break;
    case ValueType::Int: node["value"] = value.integer; break;
    case ValueType::Float: node["value"] = value.numeric.x; break;
    case ValueType::Float2: node["value"] = {value.numeric.x, value.numeric.y}; break;
    case ValueType::Float3: node["value"] = {value.numeric.x, value.numeric.y, value.numeric.z}; break;
    case ValueType::Float4:
    case ValueType::Color: node["value"] = {value.numeric.x, value.numeric.y, value.numeric.z, value.numeric.w}; break;
    }
    return node;
}

bool ReadValue(const nlohmann::json& node, Value& outValue) {
    if (!node.is_object()) {
        return false;
    }
    ValueType type{};
    if (!TryParseValueType(node.value("type", ""), type)) {
        return false;
    }
    const auto found = node.find("value");
    if (found == node.end()) {
        return false;
    }
    Value value = MakeDefaultValue(type);
    try {
        switch (type) {
        case ValueType::Bool: value.boolean = found->get<bool>(); break;
        case ValueType::Int: value.integer = found->get<int64_t>(); break;
        case ValueType::Float: value.numeric.x = found->get<float>(); break;
        case ValueType::Float2:
        case ValueType::Float3:
        case ValueType::Float4:
        case ValueType::Color: {
            if (!found->is_array() || found->size() != GetValueComponentCount(type)) {
                return false;
            }
            float* components[] = {&value.numeric.x, &value.numeric.y, &value.numeric.z, &value.numeric.w};
            for (size_t index = 0u; index < found->size(); ++index) {
                *components[index] = (*found)[index].get<float>();
            }
            break;
        }
        }
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    outValue = value;
    return true;
}

} // namespace

std::filesystem::path GetCatalogPath(const std::filesystem::path& projectRoot) {
    return (projectRoot / "ProjectSettings" / "runtime_bindings.json").lexically_normal();
}

bool LoadCatalog(const std::filesystem::path& path, Catalog& outCatalog, std::string& outError) {
    outError.clear();
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root) || !root.is_object()) {
        outError = "runtime binding catalog could not be read";
        return false;
    }

    Catalog catalog{};
    std::string decodeContext = "root";
    try {
        decodeContext = "root.version";
        catalog.version = root.value("version", 0u);

        const auto variables = root.find("variables");
        if (variables != root.end() && !variables->is_array()) {
            outError = "runtime binding catalog field is invalid at "
                       "root.variables: expected an array";
            return false;
        }
        if (variables != root.end()) {
            size_t variableIndex = 0u;
            for (const nlohmann::json& node : *variables) {
                const std::string variableContext = "root.variables[" + std::to_string(variableIndex++) + "]";
                if (!node.is_object()) {
                    outError =
                        "runtime binding catalog field is invalid at " + variableContext + ": expected an object";
                    return false;
                }
                VariableDefinition definition{};
                decodeContext = variableContext + ".id";
                definition.id = node.value("id", "");
                decodeContext = variableContext + ".name";
                definition.name = node.value("name", "");
                decodeContext = variableContext + ".description";
                definition.description = node.value("description", "");
                decodeContext = variableContext + ".hasRange";
                definition.hasRange = node.value("hasRange", false);
                decodeContext = variableContext + ".minimum";
                definition.minimum = node.value("minimum", 0.0f);
                decodeContext = variableContext + ".maximum";
                definition.maximum = node.value("maximum", 1.0f);
                decodeContext = variableContext + ".step";
                definition.step = node.value("step", 0.01f);
                decodeContext = variableContext + ".default";
                if (!ReadValue(node.value("default", nlohmann::json::object()), definition.defaultValue)) {
                    outError = "runtime binding variable has an invalid default: " + definition.id;
                    return false;
                }
                catalog.variables.push_back(std::move(definition));
            }
        }

        const auto events = root.find("events");
        if (events != root.end() && !events->is_array()) {
            outError = "runtime binding catalog field is invalid at "
                       "root.events: expected an array";
            return false;
        }
        if (events != root.end()) {
            size_t eventIndex = 0u;
            for (const nlohmann::json& node : *events) {
                const std::string eventContext = "root.events[" + std::to_string(eventIndex++) + "]";
                if (!node.is_object()) {
                    outError = "runtime binding catalog field is invalid at " + eventContext + ": expected an object";
                    return false;
                }
                EventDefinition definition{};
                decodeContext = eventContext + ".id";
                definition.id = node.value("id", "");
                decodeContext = eventContext + ".name";
                definition.name = node.value("name", "");
                decodeContext = eventContext + ".description";
                definition.description = node.value("description", "");
                decodeContext = eventContext + ".hasPayload";
                definition.hasPayload = node.value("hasPayload", false);
                decodeContext = eventContext + ".payloadType";
                if (definition.hasPayload &&
                    !TryParseValueType(node.value("payloadType", ""), definition.payloadType)) {
                    outError = "runtime binding event has an invalid payload type: " + definition.id;
                    return false;
                }
                catalog.events.push_back(std::move(definition));
            }
        }
    } catch (const nlohmann::json::exception& exception) {
        outError = "runtime binding catalog field is invalid at " + decodeContext + ": " + exception.what();
        return false;
    }

    const std::vector<CatalogDiagnostic> diagnostics = ValidateCatalog(catalog);
    if (HasCatalogErrors(diagnostics)) {
        outError = diagnostics.front().message;
        if (!diagnostics.front().subjectId.empty()) {
            outError += " [" + diagnostics.front().subjectId + "]";
        }
        return false;
    }
    outCatalog = std::move(catalog);
    return true;
}

bool SaveCatalog(const std::filesystem::path& path, const Catalog& catalog, std::string& outError) {
    outError.clear();
    const std::vector<CatalogDiagnostic> diagnostics = ValidateCatalog(catalog);
    if (HasCatalogErrors(diagnostics)) {
        outError = diagnostics.front().message;
        if (!diagnostics.front().subjectId.empty()) {
            outError += " [" + diagnostics.front().subjectId + "]";
        }
        return false;
    }

    nlohmann::json variables = nlohmann::json::array();
    for (const VariableDefinition& variable : catalog.variables) {
        variables.push_back({
            {"id", variable.id},
            {"name", variable.name},
            {"description", variable.description},
            {"default", WriteValue(variable.defaultValue)},
            {"hasRange", variable.hasRange},
            {"minimum", variable.minimum},
            {"maximum", variable.maximum},
            {"step", variable.step},
        });
    }
    nlohmann::json events = nlohmann::json::array();
    for (const EventDefinition& event : catalog.events) {
        events.push_back({
            {"id", event.id},
            {"name", event.name},
            {"description", event.description},
            {"hasPayload", event.hasPayload},
            {"payloadType", ToString(event.payloadType)},
        });
    }
    const nlohmann::json root{
        {"version", catalog.version},
        {"variables", std::move(variables)},
        {"events", std::move(events)},
    };
    return SERIALIZATION::JSON::WriteJsonFile(path, root, &outError);
}

} // namespace HIKARI::BINDING
