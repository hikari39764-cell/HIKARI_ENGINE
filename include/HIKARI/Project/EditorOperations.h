#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "HIKARI/Project/EditorWorkspace.h"

namespace HIKARI::EDITOR_PROJECT {

class IEditorDocumentService;

inline constexpr uint32_t kEditorOperationsApiVersion = 1u;

enum class EditorOperationParameterType : uint32_t {
    String = 0u,
    Number = 1u,
    Integer = 2u,
    Boolean = 3u,
    Json = 4u,
    Unspecified = 0xffffffffu,
};

enum class EditorOperationKind : uint32_t {
    Unspecified = 0u,
    Query,
    Mutation,
    Session,
};

enum class EditorOperationPermission : uint32_t {
    Unspecified = 0u,
    ReadEditorState,
    ModifyEditorState,
    ModifyProjectFiles,
    ExecuteExternalProcess,
};

enum class EditorOperationExposureMask : uint32_t {
    None = 0u,
    Human = 1u << 0u,
    BuiltInAi = 1u << 1u,
    LocalBridge = 1u << 2u,
};

constexpr EditorOperationExposureMask operator|(EditorOperationExposureMask lhs,
                                                EditorOperationExposureMask rhs) noexcept {
    return static_cast<EditorOperationExposureMask>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

struct EditorOperationParameterDescriptor {
    uint32_t structSize = sizeof(EditorOperationParameterDescriptor);
    uint32_t apiVersion = kEditorOperationsApiVersion;
    const char* name = nullptr;
    EditorOperationParameterType type = EditorOperationParameterType::Unspecified;
    uint32_t required = 0u;
    const char* description = nullptr;
    // Optional JSON object used by the Engine's shared parameter validator.
    const char* schemaJson = nullptr;
};

class IEditorOperationHandler {
public:
    virtual ~IEditorOperationHandler() = default;

    // Called synchronously on the Editor owner thread. The parameters view is
    // valid only during this call. The handler may run while its workspace is
    // inactive, so it must not depend on Draw() or frame-scoped UI pointers.
    virtual void Execute(std::string_view parametersJson, IEditorOperationResultSink& result) = 0;
};

struct EditorOperationDescriptor {
    uint32_t structSize = sizeof(EditorOperationDescriptor);
    uint32_t apiVersion = kEditorOperationsApiVersion;
    const char* id = nullptr;
    const char* summary = nullptr;
    const char* workspace = nullptr;
    EditorOperationKind kind = EditorOperationKind::Unspecified;
    EditorOperationPermission permission = EditorOperationPermission::Unspecified;
    // Project operations are visible to workspace controls by default. AI
    // discovery and the local bridge require explicit exposure bits.
    EditorOperationExposureMask exposureMask = EditorOperationExposureMask::Human;
    uint32_t reserved = 0u;
    const EditorOperationParameterDescriptor* parameters = nullptr;
    size_t parameterCount = 0u;
    const char* const* searchTerms = nullptr;
    size_t searchTermCount = 0u;
    // The module owns this handler. The host keeps its pointer until all
    // operations owned by that module are unregistered before DLL unload.
    IEditorOperationHandler* handler = nullptr;
};

enum class EditorOperationRegistrationCode : uint32_t {
    Registered = 0u,
    InvalidDescriptor,
    DuplicateId,
    InvalidPermission,
    InvalidKind,
    InvalidExposure,
    InvalidParameter,
    InvalidSchema,
};

struct EditorOperationRegistrationResult {
    bool ok = false;
    EditorOperationRegistrationCode code = EditorOperationRegistrationCode::InvalidDescriptor;
};

class IEditorOperationRegistrar {
public:
    virtual ~IEditorOperationRegistrar() = default;
    // Descriptor strings, arrays and schema text are copied before this call
    // returns. Failed registrations are rolled back as one module transaction.
    virtual EditorOperationRegistrationResult RegisterOperation(const EditorOperationDescriptor& descriptor) = 0;
    // The borrowed service has the same owner-scoped documents exposed to the
    // module's workspace contexts and remains valid while the module is registered.
    virtual IEditorDocumentService* Documents() noexcept = 0;
};

static_assert(std::is_standard_layout_v<EditorOperationParameterDescriptor>);
static_assert(std::is_trivially_copyable_v<EditorOperationParameterDescriptor>);
static_assert(sizeof(EditorOperationParameterDescriptor) == 40u);
static_assert(offsetof(EditorOperationParameterDescriptor, name) == 8u);
static_assert(offsetof(EditorOperationParameterDescriptor, schemaJson) == 32u);
static_assert(std::is_standard_layout_v<EditorOperationDescriptor>);
static_assert(std::is_trivially_copyable_v<EditorOperationDescriptor>);
static_assert(sizeof(EditorOperationDescriptor) == 88u);
static_assert(offsetof(EditorOperationDescriptor, id) == 8u);
static_assert(offsetof(EditorOperationDescriptor, parameters) == 48u);
static_assert(offsetof(EditorOperationDescriptor, handler) == 80u);
static_assert(std::is_standard_layout_v<EditorOperationRegistrationResult>);
static_assert(std::is_trivially_copyable_v<EditorOperationRegistrationResult>);

} // namespace HIKARI::EDITOR_PROJECT
