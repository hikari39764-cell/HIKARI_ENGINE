#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "HIKARI/Project/ProjectDataTypes.h"

namespace HIKARI::EDITOR_PROJECT {

inline constexpr uint32_t kEditorDocumentsApiVersion = 2u;
inline constexpr size_t kMaximumProjectDocumentCount = 16u;
inline constexpr size_t kProjectDocumentHistoryBytes = 16u * 1024u * 1024u;
inline constexpr size_t kProjectDocumentHistorySteps = 128u;

using EditorDocumentHandle = uint64_t;
inline constexpr EditorDocumentHandle kInvalidEditorDocumentHandle = 0u;

enum class EditorDocumentCode : uint32_t {
    Ok = 0u,
    NoChange,
    InvalidPath,
    ReparsePoint,
    NotFound,
    AlreadyExists,
    TooManyDocuments,
    TooLarge,
    InvalidUtf8,
    InvalidHandle,
    RevisionConflict,
    Dirty,
    ExternalConflict,
    HistoryBudgetExceeded,
    IoFailure,
};

enum EditorDocumentStateFlag : uint32_t {
    EditorDocumentStateNone = 0u,
    EditorDocumentStateDirty = 1u << 0u,
    EditorDocumentStateCanUndo = 1u << 1u,
    EditorDocumentStateCanRedo = 1u << 2u,
};

// Every document call returns the state observed by that call. After a
// successful Close, the handle is invalid even though this result preserves
// the final revision and state flags for diagnostics.
struct EditorDocumentResult {
    uint32_t structSize = sizeof(EditorDocumentResult);
    uint32_t apiVersion = kEditorDocumentsApiVersion;
    EditorDocumentCode code = EditorDocumentCode::IoFailure;
    uint32_t stateFlags = EditorDocumentStateNone;
    EditorDocumentHandle document = kInvalidEditorDocumentHandle;
    uint64_t revision = 0u;
    uint64_t byteCount = 0u;

    constexpr bool Succeeded() const noexcept {
        return code == EditorDocumentCode::Ok || code == EditorDocumentCode::NoChange;
    }
    constexpr bool IsDirty() const noexcept { return (stateFlags & EditorDocumentStateDirty) != 0u; }
    constexpr bool CanUndo() const noexcept { return (stateFlags & EditorDocumentStateCanUndo) != 0u; }
    constexpr bool CanRedo() const noexcept { return (stateFlags & EditorDocumentStateCanRedo) != 0u; }
};

// Text is borrowed only for this callback. Projects should parse and validate
// it into their own model before applying edits; the host treats it as UTF-8.
class IEditorDocumentReadSink {
public:
    virtual ~IEditorDocumentReadSink() = default;
    virtual void Receive(const EditorDocumentResult& result, std::string_view utf8Text) = 0;
};

// Paths are UTF-8 and relative to the project's Data directory. Calls are
// synchronous on the Editor owner thread. A service pointer borrowed from a
// workspace context or operation registrar remains valid while the module is
// registered. Open documents are owner-scoped and same-path opens share one
// handle and draft.
// Asset documents are source files under Assets whose type a module declares
// in its manifest. They share the same drafts, history and limits; saving one
// makes the asset database read it again, which changes the revision that
// ProjectDataView::GetModuleAssetRevision reports to running game code.
class IEditorDocumentService {
public:
    virtual ~IEditorDocumentService() = default;

    virtual EditorDocumentResult Open(std::string_view relativePath) = 0;
    virtual EditorDocumentResult Create(std::string_view relativePath, std::string_view initialUtf8Text) = 0;
    virtual EditorDocumentResult Read(EditorDocumentHandle document, uint64_t expectedRevision,
                                      IEditorDocumentReadSink& sink) const = 0;
    virtual EditorDocumentResult Apply(EditorDocumentHandle document, uint64_t expectedRevision,
                                       std::string_view validatedUtf8Text) = 0;
    virtual EditorDocumentResult Undo(EditorDocumentHandle document, uint64_t expectedRevision) = 0;
    virtual EditorDocumentResult Redo(EditorDocumentHandle document, uint64_t expectedRevision) = 0;
    virtual EditorDocumentResult Save(EditorDocumentHandle document, uint64_t expectedRevision) = 0;
    // Dirty documents remain open unless discardChanges is explicitly true.
    virtual EditorDocumentResult Close(EditorDocumentHandle document, bool discardChanges) = 0;
    // NotFound when the GUID is unknown or is not a module-declared asset.
    virtual EditorDocumentResult OpenAsset(std::string_view assetGuid) = 0;
    // assetsRelativePath is relative to the project's Assets directory and must end with the extension of a
    // module-declared asset type. The file and its GUID exist after the first successful Save.
    virtual EditorDocumentResult CreateAsset(std::string_view assetsRelativePath, std::string_view initialUtf8Text) = 0;
    // Copies the GUID of an asset document with its terminator and returns its length; zero for Data documents,
    // for asset documents that have not been saved yet, and when the buffer is too small.
    virtual size_t GetAssetGuid(EditorDocumentHandle document, char* utf8Buffer, size_t bufferCapacity) const = 0;
};

static_assert(std::is_standard_layout_v<EditorDocumentResult>);
static_assert(std::is_trivially_copyable_v<EditorDocumentResult>);
static_assert(sizeof(EditorDocumentResult) == 40u);
static_assert(offsetof(EditorDocumentResult, code) == 8u);
static_assert(offsetof(EditorDocumentResult, document) == 16u);
static_assert(offsetof(EditorDocumentResult, revision) == 24u);
static_assert(offsetof(EditorDocumentResult, byteCount) == 32u);

} // namespace HIKARI::EDITOR_PROJECT
