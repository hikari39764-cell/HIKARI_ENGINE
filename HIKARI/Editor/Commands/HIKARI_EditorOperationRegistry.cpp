#include "Editor/Commands/HIKARI_EditorOperationContext.h"
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
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::EDITOR {

bool EquivalentOperationValue(const nlohmann::json& requested, const nlohmann::json& applied) {
    if (requested.is_number() && applied.is_number()) {
        if (!requested.is_number_float() || !applied.is_number_float())
            return requested == applied;
        const float left = static_cast<float>(requested.get<double>());
        const float right = static_cast<float>(applied.get<double>());
        return MATH::IsFinite(left) && MATH::IsFinite(right) ? left == right : requested == applied;
    }
    if (requested.is_array() && applied.is_array()) {
        if (requested.size() != applied.size())
            return false;
        for (size_t index = 0; index < requested.size(); ++index)
            if (!EquivalentOperationValue(requested[index], applied[index]))
                return false;
        return true;
    }
    if (requested.is_object() && applied.is_object()) {
        if (requested.size() != applied.size())
            return false;
        for (const auto& [key, value] : requested.items()) {
            const auto found = applied.find(key);
            if (found == applied.end() || !EquivalentOperationValue(value, *found))
                return false;
        }
        return true;
    }
    return requested == applied;
}

namespace {
std::string JoinPatchPath(const std::string& path, const std::string& key) {
    return path.empty() ? key : path + "." + key;
}

bool FindUnknownPatchKey(const nlohmann::json& patch, const nlohmann::json& current, const std::string& path,
                         std::string& outPath) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const auto found = current.find(it.key());
        if (found == current.end()) {
            outPath = JoinPatchPath(path, it.key());
            return true;
        }
        if (it->is_object() && found->is_object() &&
            FindUnknownPatchKey(*it, *found, JoinPatchPath(path, it.key()), outPath)) {
            return true;
        }
    }
    return false;
}

void CollectAdjustedPatchValues(const nlohmann::json& requested, const nlohmann::json& stored, const std::string& path,
                                nlohmann::json& outAdjusted) {
    for (auto it = requested.begin(); it != requested.end(); ++it) {
        const auto found = stored.find(it.key());
        if (found == stored.end()) {
            continue;
        }
        const std::string key = JoinPatchPath(path, it.key());
        if (it->is_object() && found->is_object()) {
            CollectAdjustedPatchValues(*it, *found, key, outAdjusted);
        } else if (!EquivalentOperationValue(*it, *found)) {
            outAdjusted[key] = *found;
        }
    }
}
} // namespace

std::string FindUnknownPatchKey(const nlohmann::json& patch, const nlohmann::json& current) {
    std::string path;
    if (patch.is_object() && current.is_object()) {
        (void)FindUnknownPatchKey(patch, current, {}, path);
    }
    return path;
}

nlohmann::json CollectAdjustedPatchValues(const nlohmann::json& requested, const nlohmann::json& stored) {
    nlohmann::json adjusted = nlohmann::json::object();
    if (requested.is_object() && stored.is_object()) {
        CollectAdjustedPatchValues(requested, stored, {}, adjusted);
    }
    return adjusted;
}

nlohmann::json ProjectAppliedPatch(const nlohmann::json& patch, const nlohmann::json& stored) {
    nlohmann::json applied = nlohmann::json::object();
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const auto found = stored.find(it.key());
        if (found != stored.end()) {
            applied[it.key()] = it->is_object() && found->is_object() ? ProjectAppliedPatch(*it, *found) : *found;
        }
    }
    return applied;
}

namespace {

std::atomic<uint64_t> gOperationSequence{0u};

void AppendOperationIdForLog(std::ostringstream& stream, std::string_view id) {
    constexpr size_t kMaximumLoggedIdLength = 128u;
    const size_t count = (std::min)(id.size(), kMaximumLoggedIdLength);
    for (size_t index = 0u; index < count; ++index) {
        const char character = id[index];
        const bool accepted = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9') || character == '.' || character == '_' ||
                              character == '-';
        stream << (accepted ? character : '?');
    }
    if (id.size() > count) {
        stream << "...";
    }
}

void LogOperationBegin(uint64_t sequence, const std::string& id, const EditorOperationDefinition* definition) noexcept {
    try {
        std::ostringstream oss;
        oss << "[EditorOperation][" << sequence << "] begin id=";
        AppendOperationIdForLog(oss, id);
        oss << " policy=";
        if (definition == nullptr) {
            oss << "unknown";
        } else if (definition->executionPolicy == EditorOperationExecutionPolicy::WorkerThread) {
            oss << "worker";
        } else {
            oss << "main";
        }
        oss << " thread=" << std::this_thread::get_id();
        HIKARI_LOG_INFO(oss.str());
    } catch (...) {
    }
}

void LogOperationEnd(uint64_t sequence, const std::string& id, const EditorOperationResult& result,
                     std::chrono::steady_clock::time_point startedAt) noexcept {
    try {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt);
        std::ostringstream oss;
        oss << "[EditorOperation][" << sequence << "] end id=";
        AppendOperationIdForLog(oss, id);
        oss << " ok=" << (result.ok ? "true" : "false") << " code=" << (result.code.empty() ? "NONE" : result.code)
            << " durationMs=" << elapsed.count();
        if (result.ok) {
            HIKARI_LOG_INFO(oss.str());
        } else {
            HIKARI_LOG_WARN(oss.str());
        }
    } catch (...) {
    }
}

bool MatchesParameterType(const nlohmann::json& value, EditorOperationParameterType type) noexcept {
    switch (type) {
    case EditorOperationParameterType::String: return value.is_string();
    case EditorOperationParameterType::Float: return value.is_number();
    case EditorOperationParameterType::Int: return value.is_number_integer() || value.is_number_unsigned();
    case EditorOperationParameterType::Bool: return value.is_boolean();
    case EditorOperationParameterType::Json: return true;
    }
    return false;
}

EditorOperationRegistrationResult RejectRegistration(EditorOperationRegistrationCode code, std::string_view id,
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

bool ValidateShape(const nlohmann::json& value, const nlohmann::json& schema, const std::string& path,
                   std::string& error, int depth = 0) {
    if (depth > 16 || !schema.is_object()) {
        error = path + " has an invalid or excessively nested parameter schema.";
        return false;
    }
    if (schema.contains("oneOf")) {
        size_t matches = 0;
        for (const auto& candidate : schema["oneOf"]) {
            std::string ignored;
            if (ValidateShape(value, candidate, path, ignored, depth + 1))
                ++matches;
        }
        if (matches != 1) {
            error = path + " must match exactly one accepted command shape. See operations.describe.";
            return false;
        }
    }
    const auto type = schema.value("type", std::string{});
    if ((!type.empty() && type == "object" && !value.is_object()) || (type == "array" && !value.is_array()) ||
        (type == "string" && !value.is_string()) || (type == "boolean" && !value.is_boolean()) ||
        (type == "number" && !value.is_number()) || (type == "integer" && !value.is_number_integer())) {
        error = path + " must be " + type;
        return false;
    }
    if (schema.contains("enum") &&
        std::find(schema["enum"].begin(), schema["enum"].end(), value) == schema["enum"].end()) {
        // 長い一覧（コンソール変数の名前など）は数だけ言う。全部は操作の説明の schema にある。
        constexpr size_t kListedChoices = 12;
        const nlohmann::json& choices = schema["enum"];
        error = choices.size() <= kListedChoices ? path + " must be one of " + choices.dump()
                                                 : path + " is not one of its " + std::to_string(choices.size()) +
                                                       " allowed values (listed in the operation's parameter schema)";
        return false;
    }
    if (value.is_number()) {
        for (const auto* key : {"minimum", "maximum"}) {
            if (schema.contains(key) &&
                (std::string_view(key) == "minimum" ? value.get<double>() < schema[key].get<double>()
                                                    : value.get<double>() > schema[key].get<double>())) {
                error = path + " violates " + key + "=" + schema[key].dump();
                return false;
            }
        }
    }
    if (value.is_object()) {
        for (const auto& required : schema.value("required", nlohmann::json::array())) {
            if (!value.contains(required.get<std::string>())) {
                error = "Missing required parameter: " + path + "." + required.get<std::string>();
                return false;
            }
        }
        const auto properties = schema.value("properties", nlohmann::json::object());
        for (const auto& [key, child] : value.items()) {
            if (properties.contains(key) && !ValidateShape(child, properties[key], path + "." + key, error, depth + 1))
                return false;
            if (!properties.contains(key) && schema.value("additionalProperties", true) == false) {
                error = "Unknown parameter: " + path + "." + key;
                return false;
            }
        }
    }
    if (value.is_array()) {
        for (const auto* key : {"minItems", "maxItems"}) {
            if (schema.contains(key) &&
                (std::string_view(key) == "minItems" ? value.size() < schema[key].get<size_t>()
                                                     : value.size() > schema[key].get<size_t>())) {
                error = path + " violates " + key + "=" + schema[key].dump();
                return false;
            }
        }
    }
    if (value.is_array() && schema.contains("items")) {
        for (size_t i = 0; i < value.size(); ++i)
            if (!ValidateShape(value[i], schema["items"], path + "[" + std::to_string(i) + "]", error, depth + 1))
                return false;
    }
    return true;
}

} // namespace

nlohmann::json EditorOperationVec3Schema() {
    return {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}};
}

nlohmann::json DescribeEditorOperation(const EditorOperationDefinition& definition) {
    nlohmann::json result{{"id", definition.id},
                          {"summary", definition.summary},
                          {"workspace", definition.workspace},
                          {"kind", TEXT::EnumToName(kEditorOperationKindNames, definition.kind, "unspecified")},
                          {"permission", TEXT::EnumToName(kEditorOperationPermissionNames, definition.permission)},
                          {"requiresDocumentSession", definition.requiresDocumentSession},
                          {"requiresCleanDocument", definition.requiresCleanDocument},
                          {"entryPoint", definition.entryPoint},
                          {"approveEachTime", definition.approveEachTime},
                          {"searchTerms", definition.searchTerms},
                          {"parameters", nlohmann::json::array()}};
    for (const auto& parameter : definition.parameters) {
        result["parameters"].push_back({{"name", parameter.name},
                                        {"type", TEXT::EnumToName(kEditorOperationParameterTypeNames, parameter.type)},
                                        {"required", parameter.required},
                                        {"description", parameter.description},
                                        {"schema", parameter.schema}});
    }
    return result;
}

EditorOperationRegistrationResult EditorOperationRegistry::Register(EditorOperationDefinition definition) {
    definition.ownerToken = 0u;
    return RegisterInternal(std::move(definition));
}

EditorOperationRegistrationResult EditorOperationRegistry::RegisterInternal(EditorOperationDefinition definition) {
    if (definition.id.empty()) {
        return RejectRegistration(EditorOperationRegistrationCode::MissingId, definition.id,
                                  "Editor operation ID is required.");
    }
    if (definition.summary.empty()) {
        return RejectRegistration(EditorOperationRegistrationCode::MissingSummary, definition.id,
                                  "Editor operation summary is required.");
    }
    if (definition.workspace.empty()) {
        return RejectRegistration(EditorOperationRegistrationCode::MissingWorkspace, definition.id,
                                  "Editor operation workspace is required.");
    }
    if (!definition.execute) {
        return RejectRegistration(EditorOperationRegistrationCode::MissingExecute, definition.id,
                                  "Editor operation execute callback is required.");
    }
    if (definition.kind == EditorOperationKind::Unspecified) {
        return RejectRegistration(EditorOperationRegistrationCode::MissingKind, definition.id,
                                  "Editor operation kind (query/mutation/session) is required.");
    }
    if (operations_.find(definition.id) != operations_.end()) {
        return RejectRegistration(EditorOperationRegistrationCode::DuplicateId, definition.id,
                                  "Editor operation ID is already registered.");
    }

    const std::string id = definition.id;
    operations_.emplace(id, std::move(definition));
    ++revision_;

    EditorOperationRegistrationResult result{};
    result.ok = true;
    result.code = EditorOperationRegistrationCode::Registered;
    result.message = "Registered editor operation: " + id;
    return result;
}

EditorOperationOwnerToken EditorOperationRegistry::CreateOwnerToken() noexcept {
    ++nextOwnerToken_;
    if (nextOwnerToken_ == 0u) {
        ++nextOwnerToken_;
    }
    return nextOwnerToken_;
}

EditorOperationRegistrationResult EditorOperationRegistry::RegisterOwned(EditorOperationDefinition definition,
                                                                         EditorOperationOwnerToken ownerToken) {
    if (ownerToken == 0u) {
        return RejectRegistration(EditorOperationRegistrationCode::InvalidOwner, definition.id,
                                  "Editor operation owner token must be nonzero.");
    }
    definition.ownerToken = ownerToken;
    return RegisterInternal(std::move(definition));
}

size_t EditorOperationRegistry::UnregisterOwner(EditorOperationOwnerToken ownerToken) noexcept {
    if (ownerToken == 0u) {
        return 0u;
    }
    size_t removed = 0u;
    for (auto operation = operations_.begin(); operation != operations_.end();) {
        if (operation->second.ownerToken == ownerToken) {
            operation = operations_.erase(operation);
            ++removed;
        } else {
            ++operation;
        }
    }
    if (removed != 0u) {
        ++revision_;
    }
    return removed;
}

bool EditorOperationRegistry::IsRegistered(const std::string& id) const noexcept {
    return operations_.find(id) != operations_.end();
}

EditorOperationResult EditorOperationRegistry::Validate(const EditorOperationDefinition& definition,
                                                        const nlohmann::json& parameters) const {
    // 間違えた引数の説明だけを返す（操作全体の説明は operations.describe）。智能体の文脈を小さく保つ。
    const auto failure = [&definition](std::string_view code, std::string message, nlohmann::json data) {
        EditorOperationResult result = OperationFailure(code, std::move(message));
        if (data.is_object()) {
            result.data = std::move(data);
        }
        result.details.push_back("operation: " + definition.id);
        result.hints.push_back("operations.describe " + definition.id + " lists every parameter.");
        return result;
    };
    const auto describe = [](const EditorOperationParameter& parameter) {
        nlohmann::json described{{"name", parameter.name},
                                 {"type", TEXT::EnumToName(kEditorOperationParameterTypeNames, parameter.type)},
                                 {"required", parameter.required},
                                 {"description", parameter.description}};
        if (!parameter.schema.empty()) {
            described["schema"] = parameter.schema;
        }
        return nlohmann::json{{"parameter", std::move(described)}};
    };
    if (!parameters.is_object()) {
        return failure("PARAMETERS_NOT_OBJECT", "Operation parameters must be a JSON object: " + definition.id, {});
    }

    for (const EditorOperationParameter& parameter : definition.parameters) {
        const auto found = parameters.find(parameter.name);
        if (found == parameters.end()) {
            if (!parameter.required) {
                continue;
            }
            return failure("MISSING_PARAMETER", "Missing required parameter: " + parameter.name, describe(parameter));
        }
        if (!MatchesParameterType(*found, parameter.type)) {
            return failure("PARAMETER_TYPE_MISMATCH",
                           "Parameter '" + parameter.name + "' must be " +
                               TEXT::EnumToName(kEditorOperationParameterTypeNames, parameter.type) + ", received " +
                               found->type_name(),
                           describe(parameter));
        }
        std::string message;
        if (!ValidateShape(*found, parameter.schema, parameter.name, message)) {
            return failure("PARAMETER_CONSTRAINT", std::move(message), describe(parameter));
        }
    }

    for (const auto& [name, value] : parameters.items()) {
        (void)value;
        if (name == "action") {
            continue;
        }
        const auto known =
            std::find_if(definition.parameters.begin(), definition.parameters.end(),
                         [&name](const EditorOperationParameter& parameter) { return parameter.name == name; });
        if (known == definition.parameters.end()) {
            nlohmann::json names = nlohmann::json::array();
            for (const EditorOperationParameter& parameter : definition.parameters) {
                names.push_back(parameter.name);
            }
            return failure("UNKNOWN_PARAMETER", "Unknown parameter: " + name, {{"parameters", std::move(names)}});
        }
    }

    EditorOperationResult result{};
    result.ok = true;
    return result;
}

EditorOperationResult EditorOperationRegistry::Execute(const std::string& id, const nlohmann::json& parameters) const {
    const uint64_t sequence = gOperationSequence.fetch_add(1u, std::memory_order_relaxed) + 1u;
    const auto startedAt = std::chrono::steady_clock::now();
    const auto found = operations_.find(id);
    LogOperationBegin(sequence, id, found != operations_.end() ? &found->second : nullptr);
    const auto finish = [&](EditorOperationResult result) {
        if (found != operations_.end()) {
            if (!result.data.is_object())
                result.data = nlohmann::json::object();
            result.data["operationKind"] =
                TEXT::EnumToName(kEditorOperationKindNames, found->second.kind, "unspecified");
            // 成功の code が空の操作があると、呼び出し側が失敗と見分けにくい。
            if (result.ok && result.code.empty())
                result.code = "OK";
        }
        LogOperationEnd(sequence, id, result, startedAt);
        return result;
    };
    if (found == operations_.end()) {
        EditorOperationResult result{};
        result.code = "OPERATION_NOT_FOUND";
        result.message = "Unregistered editor operation: " + id;
        return finish(std::move(result));
    }
    auto arguments = parameters;
    const auto& definition = found->second;
    const bool declaredSession = std::any_of(definition.parameters.begin(), definition.parameters.end(),
                                             [](const auto& p) { return p.name == "sessionId"; });
    std::string session;
    if (targetExecutor_ && arguments.is_object() && !declaredSession) {
        if (arguments.contains("sessionId")) {
            if (!arguments["sessionId"].is_string())
                return finish({false, "INVALID_PARAMETER", "sessionId must be a string."});
            if (definition.requiresDocumentSession) {
                session = arguments["sessionId"];
                if (session.empty())
                    return finish({false, "INVALID_PARAMETER", "sessionId must not be empty."});
            }
            arguments.erase("sessionId");
        } else if (definition.requiresDocumentSession && targetSelector_) {
            session = targetSelector_(id);
        }
    }
    const auto validation = Validate(definition, arguments);
    if (!validation.ok)
        return finish(validation);
    try {
        if (!session.empty()) {
            arguments["sessionId"] = session;
            return finish(targetExecutor_(
                definition.sessionDispatch == EditorOperationSessionDispatch::Preview ? "authoring.preview" : id,
                arguments));
        }
        return finish(definition.execute(arguments));
    } catch (const std::exception& error) {
        EditorOperationResult result{};
        result.code = "OPERATION_EXCEPTION";
        result.message = "Editor operation failed with an exception: " + id;
        result.details.push_back(error.what());
        return finish(std::move(result));
    } catch (...) {
        EditorOperationResult result{};
        result.code = "OPERATION_EXCEPTION";
        result.message = "Editor operation failed with an unknown exception: " + id;
        return finish(std::move(result));
    }
}

EditorOperationResult EditorOperationRegistry::Execute(const std::string& id) const {
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

const EditorOperationDefinition* EditorOperationRegistry::Find(const std::string& id) const noexcept {
    const auto found = operations_.find(id);
    return found != operations_.end() ? &found->second : nullptr;
}

} // namespace HIKARI::EDITOR
