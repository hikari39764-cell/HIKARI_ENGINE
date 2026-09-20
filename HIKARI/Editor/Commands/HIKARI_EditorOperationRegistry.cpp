#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

#include "Core/HIKARI_Logger.h"

namespace HIKARI::EDITOR {

namespace {

std::atomic<uint64_t> gOperationSequence{ 0u };

void AppendOperationIdForLog(
    std::ostringstream& stream,
    std::string_view id) {

    constexpr size_t kMaximumLoggedIdLength = 128u;
    const size_t count = (std::min)(id.size(), kMaximumLoggedIdLength);
    for (size_t index = 0u; index < count; ++index) {
        const char character = id[index];
        const bool accepted =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-';
        stream << (accepted ? character : '?');
    }
    if (id.size() > count) {
        stream << "...";
    }
}

void LogOperationBegin(
    uint64_t sequence,
    const std::string& id,
    const EditorOperationDefinition* definition) noexcept {

    try {
        std::ostringstream oss;
        oss << "[EditorOperation][" << sequence << "] begin id=";
        AppendOperationIdForLog(oss, id);
        oss << " policy=";
        if (definition == nullptr) {
            oss << "unknown";
        } else if (definition->executionPolicy ==
            EditorOperationExecutionPolicy::WorkerThread) {
            oss << "worker";
        } else {
            oss << "main";
        }
        oss << " thread=" << std::this_thread::get_id();
        HIKARI_LOG_INFO(oss.str());
    } catch (...) {
    }
}

void LogOperationEnd(
    uint64_t sequence,
    const std::string& id,
    const EditorOperationResult& result,
    std::chrono::steady_clock::time_point startedAt) noexcept {

    try {
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startedAt);
        std::ostringstream oss;
        oss << "[EditorOperation][" << sequence << "] end id=";
        AppendOperationIdForLog(oss, id);
        oss << " ok=" << (result.ok ? "true" : "false")
            << " code=" << (result.code.empty() ? "NONE" : result.code)
            << " durationMs=" << elapsed.count();
        if (result.ok) {
            HIKARI_LOG_INFO(oss.str());
        } else {
            HIKARI_LOG_WARN(oss.str());
        }
    } catch (...) {
    }
}

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

EditorOperationRegistrationResult RejectRegistration(
    EditorOperationRegistrationCode code,
    std::string_view id,
    std::string message) {

    try {
        std::ostringstream oss;
        oss << "[EditorOperation] registration rejected id=";
        AppendOperationIdForLog(oss, id);
        oss << " reason=" << message;
        HIKARI_LOG_ERROR(oss.str());
    } catch (...) {
    }

    EditorOperationRegistrationResult result{};
    result.code = code;
    result.message = std::move(message);
    return result;
}

} // namespace

EditorOperationRegistrationResult EditorOperationRegistry::Register(
    EditorOperationDefinition definition) {

    if (definition.id.empty()) {
        return RejectRegistration(
            EditorOperationRegistrationCode::MissingId,
            definition.id,
            "Editor operation ID is required.");
    }
    if (definition.summary.empty()) {
        return RejectRegistration(
            EditorOperationRegistrationCode::MissingSummary,
            definition.id,
            "Editor operation summary is required.");
    }
    if (definition.workspace.empty()) {
        return RejectRegistration(
            EditorOperationRegistrationCode::MissingWorkspace,
            definition.id,
            "Editor operation workspace is required.");
    }
    if (!definition.execute) {
        return RejectRegistration(
            EditorOperationRegistrationCode::MissingExecute,
            definition.id,
            "Editor operation execute callback is required.");
    }
    if (operations_.find(definition.id) != operations_.end()) {
        return RejectRegistration(
            EditorOperationRegistrationCode::DuplicateId,
            definition.id,
            "Editor operation ID is already registered.");
    }

    const std::string id = definition.id;
    operations_.emplace(id, std::move(definition));

    EditorOperationRegistrationResult result{};
    result.ok = true;
    result.code = EditorOperationRegistrationCode::Registered;
    result.message = "Registered editor operation: " + id;
    return result;
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

    if (targetExecutor_ && targetSelector_ && parameters.is_object() &&
        !parameters.contains("sessionId") && !id.starts_with("authoring.")) {
        const auto selected = targetSelector_(id);
        if (!selected.empty()) {
            auto targeted = parameters;
            targeted["sessionId"] = selected;
            return Execute(id, targeted);
        }
    }
    if (targetExecutor_ && parameters.is_object() && parameters.contains("sessionId") &&
        id.rfind("authoring.", 0) != 0) {
        if (!parameters["sessionId"].is_string())
            return {false, "INVALID_PARAMETER", "sessionId must be a string."};
        try { return targetExecutor_(id, parameters); }
        catch (const std::exception& error) { return {false, "SESSION_OPERATION_FAILED", error.what()}; }
    }

    const uint64_t sequence =
        gOperationSequence.fetch_add(1u, std::memory_order_relaxed) + 1u;
    const auto startedAt = std::chrono::steady_clock::now();
    const auto found = operations_.find(id);
    LogOperationBegin(
        sequence,
        id,
        found != operations_.end() ? &found->second : nullptr);
    const auto finish = [&](EditorOperationResult result) {
        LogOperationEnd(sequence, id, result, startedAt);
        return result;
    };
    if (found == operations_.end()) {
        EditorOperationResult result{};
        result.code = "OPERATION_NOT_FOUND";
        result.message = "Unregistered editor operation: " + id;
        return finish(std::move(result));
    }
    EditorOperationResult validation =
        Validate(found->second, parameters);
    if (!validation.ok) {
        return finish(std::move(validation));
    }
    try {
        return finish(found->second.execute(parameters));
    } catch (const std::exception& error) {
        EditorOperationResult result{};
        result.code = "OPERATION_EXCEPTION";
        result.message =
            "Editor operation failed with an exception: " + id;
        result.details.push_back(error.what());
        return finish(std::move(result));
    } catch (...) {
        EditorOperationResult result{};
        result.code = "OPERATION_EXCEPTION";
        result.message =
            "Editor operation failed with an unknown exception: " + id;
        return finish(std::move(result));
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
