#include "Editor/Project/HIKARI_GameProjectEditorOperationRegistration.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceRegistry.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <json.hpp>

namespace HIKARI::EDITOR {
namespace {

constexpr size_t kMaximumOperationParameterCount = 64u;
constexpr size_t kMaximumOperationSearchTermCount = 64u;
constexpr size_t kMaximumOperationStringLength = 4096u;
constexpr size_t kMaximumOperationSchemaLength = 1024u * 1024u;

static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationParameterType::String) ==
              static_cast<uint32_t>(EditorOperationParameterType::String));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationParameterType::Number) ==
              static_cast<uint32_t>(EditorOperationParameterType::Float));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationParameterType::Integer) ==
              static_cast<uint32_t>(EditorOperationParameterType::Int));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationParameterType::Boolean) ==
              static_cast<uint32_t>(EditorOperationParameterType::Bool));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationParameterType::Json) ==
              static_cast<uint32_t>(EditorOperationParameterType::Json));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationKind::Query) ==
              static_cast<uint32_t>(EditorOperationKind::Query));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationKind::Mutation) ==
              static_cast<uint32_t>(EditorOperationKind::Mutation));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationKind::Session) ==
              static_cast<uint32_t>(EditorOperationKind::Session));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationPermission::ReadEditorState) ==
              static_cast<uint32_t>(EditorOperationPermission::ReadEditorState));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationPermission::ModifyEditorState) ==
              static_cast<uint32_t>(EditorOperationPermission::ModifyEditorState));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationPermission::ModifyProjectFiles) ==
              static_cast<uint32_t>(EditorOperationPermission::ModifyProjectFiles));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationPermission::ExecuteExternalProcess) ==
              static_cast<uint32_t>(EditorOperationPermission::ExecuteExternalProcess));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationExposureMask::Human) ==
              static_cast<uint32_t>(EditorOperationExposureMask::Human));
static_assert(static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationExposureMask::Agent) ==
              static_cast<uint32_t>(EditorOperationExposureMask::Agent));

std::string_view BoundedString(const char* value, size_t maximumLength) noexcept {
    if (value == nullptr) {
        return {};
    }
    size_t length = 0u;
    while (length < maximumLength && value[length] != '\0') {
        ++length;
    }
    return length < maximumLength ? std::string_view(value, length) : std::string_view{};
}

class OperationResultSink final : public EDITOR_PROJECT::IEditorOperationResultSink {
public:
    void Receive(bool resultOk, std::string_view resultCode, std::string_view resultMessage,
                 std::string_view dataJson) override {
        if (received) {
            result = {false, "MULTIPLE_RESULTS", "An Editor operation handler returned more than one result."};
            return;
        }
        received = true;
        result.ok = resultOk;
        result.code.assign(resultCode);
        result.message.assign(resultMessage);
        if (dataJson.empty()) {
            result.data = nlohmann::json::object();
            return;
        }
        result.data = nlohmann::json::parse(dataJson, nullptr, false);
        if (!result.data.is_object()) {
            result.ok = false;
            result.code = "INVALID_RESULT_DATA";
            result.message = "Editor operation result data must be a JSON object.";
            result.data = nlohmann::json::object();
        }
    }

    bool received = false;
    EditorOperationResult result{};
};

} // namespace

GameProjectEditorOperationRegistration::GameProjectEditorOperationRegistration(
    EditorOperationRegistry& registry, EditorOperationOwnerToken ownerToken,
    EDITOR_PROJECT::IEditorDocumentService* documents)
    : registry_(registry), ownerToken_(ownerToken), documents_(documents) {}

EDITOR_PROJECT::EditorOperationRegistrationResult GameProjectEditorOperationRegistration::RegisterOperation(
    const EDITOR_PROJECT::EditorOperationDescriptor& descriptor) {
    using PublicCode = EDITOR_PROJECT::EditorOperationRegistrationCode;
    if (failed_) {
        return {false, PublicCode::InvalidDescriptor};
    }
    if (descriptor.structSize < sizeof(EDITOR_PROJECT::EditorOperationDescriptor) ||
        descriptor.apiVersion != EDITOR_PROJECT::kEditorOperationsApiVersion || descriptor.reserved != 0u ||
        descriptor.handler == nullptr || (descriptor.parameters == nullptr && descriptor.parameterCount != 0u) ||
        (descriptor.searchTerms == nullptr && descriptor.searchTermCount != 0u) ||
        descriptor.parameterCount > kMaximumOperationParameterCount ||
        descriptor.searchTermCount > kMaximumOperationSearchTermCount) {
        return Fail(PublicCode::InvalidDescriptor, "Project Editor operation descriptor is invalid.");
    }

    const std::string_view id = BoundedString(descriptor.id, 128u);
    const std::string_view summary = BoundedString(descriptor.summary, kMaximumOperationStringLength + 1u);
    const std::string_view workspace = BoundedString(descriptor.workspace, 128u);
    if (!IsGameProjectEditorIdentifier(id) || summary.empty() || !IsGameProjectEditorIdentifier(workspace)) {
        return Fail(PublicCode::InvalidDescriptor,
                    "Project Editor operation requires a bounded identifier, summary, and workspace.");
    }
    const uint32_t publicKind = static_cast<uint32_t>(descriptor.kind);
    if (publicKind > (std::numeric_limits<uint8_t>::max)()) {
        return Fail(PublicCode::InvalidKind, "Project Editor operation kind must be query, mutation, or session.");
    }
    const auto kind = static_cast<EditorOperationKind>(publicKind);
    if (kind == EditorOperationKind::Unspecified || !TEXT::IsNamedEnum(kEditorOperationKindNames, kind)) {
        return Fail(PublicCode::InvalidKind, "Project Editor operation kind must be query, mutation, or session.");
    }
    const uint32_t publicPermission = static_cast<uint32_t>(descriptor.permission);
    if (publicPermission > (std::numeric_limits<uint8_t>::max)()) {
        return Fail(PublicCode::InvalidPermission, "Project Editor operation must declare an explicit permission.");
    }
    const auto permission = static_cast<EditorOperationPermission>(publicPermission);
    if (permission == EditorOperationPermission::Unspecified ||
        !TEXT::IsNamedEnum(kEditorOperationPermissionNames, permission)) {
        return Fail(PublicCode::InvalidPermission, "Project Editor operation must declare an explicit permission.");
    }

    constexpr uint32_t kKnownExposureMask = static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationExposureMask::Human) |
                                            static_cast<uint32_t>(EDITOR_PROJECT::EditorOperationExposureMask::Agent);
    const uint32_t exposureBits = static_cast<uint32_t>(descriptor.exposureMask);
    if (exposureBits == 0u || (exposureBits & ~kKnownExposureMask) != 0u) {
        return Fail(PublicCode::InvalidExposure, "Project Editor operation exposure mask is invalid or empty.");
    }

    EditorOperationDefinition operation{};
    operation.id.assign(id);
    operation.summary.assign(summary);
    operation.workspace.assign(workspace);
    operation.kind = kind;
    operation.permission = permission;
    operation.exposureMask = static_cast<EditorOperationExposureMask>(static_cast<uint8_t>(exposureBits));
    operation.executionPolicy = EditorOperationExecutionPolicy::MainThread;
    operation.requiresDocumentSession = false;

    std::unordered_set<std::string> parameterNames{};
    operation.parameters.reserve(descriptor.parameterCount);
    for (size_t index = 0u; index < descriptor.parameterCount; ++index) {
        const auto& source = descriptor.parameters[index];
        if (source.structSize < sizeof(EDITOR_PROJECT::EditorOperationParameterDescriptor) ||
            source.apiVersion != EDITOR_PROJECT::kEditorOperationsApiVersion) {
            return Fail(PublicCode::InvalidParameter,
                        "Project Editor operation parameter descriptor version is incompatible.");
        }
        const uint32_t publicParameterType = static_cast<uint32_t>(source.type);
        if (source.type == EDITOR_PROJECT::EditorOperationParameterType::Unspecified ||
            publicParameterType > (std::numeric_limits<uint8_t>::max)()) {
            return Fail(PublicCode::InvalidParameter, "Project Editor operation parameter type is invalid.");
        }
        const auto parameterType = static_cast<EditorOperationParameterType>(publicParameterType);
        if (!TEXT::IsNamedEnum(kEditorOperationParameterTypeNames, parameterType)) {
            return Fail(PublicCode::InvalidParameter, "Project Editor operation parameter type is invalid.");
        }

        const std::string_view name = BoundedString(source.name, 128u);
        const std::string_view description =
            source.description == nullptr ? std::string_view{}
                                          : BoundedString(source.description, kMaximumOperationStringLength + 1u);
        if (!IsGameProjectEditorIdentifier(name) || source.required > 1u || !parameterNames.emplace(name).second ||
            (source.description != nullptr && description.empty() && source.description[0] != '\0')) {
            return Fail(PublicCode::InvalidParameter, "Project Editor operation parameter descriptor is invalid.");
        }

        EditorOperationParameter parameter{};
        parameter.name.assign(name);
        parameter.type = parameterType;
        parameter.required = source.required != 0u;
        parameter.description.assign(description);
        if (source.schemaJson != nullptr && source.schemaJson[0] != '\0') {
            const std::string_view schemaText = BoundedString(source.schemaJson, kMaximumOperationSchemaLength + 1u);
            if (schemaText.empty()) {
                return Fail(PublicCode::InvalidSchema, "Project Editor operation parameter schema is too large.");
            }
            parameter.schema = nlohmann::json::parse(schemaText, nullptr, false);
            if (!parameter.schema.is_object()) {
                return Fail(PublicCode::InvalidSchema,
                            "Project Editor operation parameter schema must be a JSON object.");
            }
        }
        operation.parameters.push_back(std::move(parameter));
    }

    operation.searchTerms.reserve(descriptor.searchTermCount);
    for (size_t index = 0u; index < descriptor.searchTermCount; ++index) {
        const std::string_view term = BoundedString(descriptor.searchTerms[index], kMaximumOperationStringLength + 1u);
        if (term.empty()) {
            return Fail(PublicCode::InvalidDescriptor, "Project Editor operation search term is empty or too large.");
        }
        operation.searchTerms.emplace_back(term);
    }

    EDITOR_PROJECT::IEditorOperationHandler* handler = descriptor.handler;
    operation.execute = [handler](const nlohmann::json& parameters) {
        OperationResultSink sink{};
        const std::string parametersJson = parameters.dump();
        handler->Execute(parametersJson, sink);
        if (!sink.received) {
            return EditorOperationResult{false, "MISSING_RESULT", "Editor operation handler did not return a result."};
        }
        return std::move(sink.result);
    };

    const auto registration = registry_.RegisterOwned(std::move(operation), ownerToken_);
    if (!registration.ok) {
        return Fail(registration.code == EditorOperationRegistrationCode::DuplicateId ? PublicCode::DuplicateId
                                                                                      : PublicCode::InvalidDescriptor,
                    registration.message);
    }
    return {true, PublicCode::Registered};
}

bool GameProjectEditorOperationRegistration::Failed() const noexcept { return failed_; }

EDITOR_PROJECT::IEditorDocumentService* GameProjectEditorOperationRegistration::Documents() noexcept {
    return documents_;
}

const std::string& GameProjectEditorOperationRegistration::FailureMessage() const noexcept { return failureMessage_; }

EDITOR_PROJECT::EditorOperationRegistrationResult GameProjectEditorOperationRegistration::Fail(
    EDITOR_PROJECT::EditorOperationRegistrationCode code, std::string message) {
    failed_ = true;
    if (failureMessage_.empty()) {
        failureMessage_ = std::move(message);
    }
    return {false, code};
}

} // namespace HIKARI::EDITOR
