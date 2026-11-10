#pragma once

#include <cstddef>
#include <cstdint>
#include "HIKARI/Project/AssetKind.h"
#include "HIKARI/Project/UiTypes.h"
#include <span>
#include <string_view>
#include <type_traits>

namespace HIKARI {
class IPropertyFieldDeclaration;
}

namespace HIKARI::EDITOR_PROJECT {

struct EditorNodeGraphDesc;
struct EditorNodeGraphResult;

inline constexpr uint32_t kEditorWorkspaceApiVersion = 7u;

enum class EditorWorkspaceDockRegion : uint32_t {
    Center,
    // A workspace has one toolbar, titled with the workspace's name. The host draws its own row first (back to
    // Scene, save, reset layout, the module the workspace belongs to, status). A panel that the workspace opens in
    // this region continues in that same window: its displayName and open flag are not used.
    Toolbar,
    Left,
    Right,
    Bottom,
    Count,
};

struct EditorWorkspaceInteraction {
    uint32_t editing = 0u;
    // The author asked to undo / redo (Ctrl+Z, Ctrl+Y, the Edit menu or the toolbar) while this workspace is active.
    // Reported only to workspaces that edit their own documents. A workspace that shows the Scene viewport
    // (RequestSceneViewport) shares the Scene history instead: record entries with IEditorSceneServices::RecordUndo.
    uint32_t undoPressed = 0u;
    uint32_t redoPressed = 0u;
    uint32_t deletePressed = 0u;
};

enum class EditorWorkspaceCloseReason : uint32_t {
    SwitchWorkspace = 0u,
    CloseWorkspace = 1u,
    ModuleUnload = 2u,
    HostShutdown = 3u,
};

enum class EditorWorkspaceCloseDisposition : uint32_t {
    Allow = 0u,
    RequiresSave = 1u,
    Block = 2u,
};

enum class EditorWorkspaceSaveResult : uint32_t {
    Saved = 0u,
    Failed = 1u,
    Canceled = 2u,
};

enum class EditorWorkspaceStatusSeverity : uint32_t {
    Info = 0u,
    Warning = 1u,
    Error = 2u,
};

enum class EditorWorkspaceOption : uint32_t {
    None = 0u,
    RequestSceneViewport = 1u << 0u,
};

enum class EditorWorkspacePointerButton : uint32_t {
    None = 0u,
    Primary = 1u << 0u,
    Secondary = 1u << 1u,
    Middle = 1u << 2u,
};

struct EditorWorkspaceDescriptor {
    uint32_t structSize = sizeof(EditorWorkspaceDescriptor);
    uint32_t apiVersion = kEditorWorkspaceApiVersion;
    const char* workspaceId = nullptr;
    const char* displayName = nullptr;
    uint32_t options = static_cast<uint32_t>(EditorWorkspaceOption::None);
    uint32_t reserved = 0u;
};

struct EditorWorkspacePoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct EditorWorkspaceColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct EditorWorkspaceIntInputOptions {
    int32_t step = 1;
    int32_t minimum = 0;
    int32_t maximum = 0;
    uint32_t clampToRange = 0u;
};

struct EditorWorkspaceFloatInputOptions {
    float step = 0.1f;
    float minimum = 0.0f;
    float maximum = 0.0f;
    uint32_t clampToRange = 0u;
};

struct EditorWorkspaceCanvasInput {
    EditorWorkspacePoint size{};
    EditorWorkspacePoint pointer{};
    uint32_t heldButtons = 0u;
    uint32_t pressedButtons = 0u;
    uint32_t releasedButtons = 0u;
    uint32_t hovered = 0u;
    float wheel = 0.0f;
    uint32_t shift = 0u;
    uint32_t control = 0u;
    uint32_t reserved = 0u;
};

class IEditorWorkspaceUi {
public:
    virtual ~IEditorWorkspaceUi() = default;

    // BeginPanel/EndPanel and BeginCanvas/EndCanvas are paired even when
    // BeginPanel reports that its collapsed contents are not visible.
    virtual bool BeginPanel(std::string_view stablePanelId, std::string_view displayName, bool* open = nullptr,
                            EditorWorkspaceDockRegion region = EditorWorkspaceDockRegion::Center) = 0;
    virtual EditorWorkspaceInteraction GetInteraction() const = 0;
    virtual bool InputTextMultiline(std::string_view stableControlId, std::string_view label, char* utf8Buffer,
                                    size_t bufferCapacity, float height = 120.0f) = 0;
    virtual void EndPanel() = 0;
    virtual void Text(std::string_view utf8Text) = 0;
    virtual void Separator() = 0;
    virtual void SameLine() = 0;
    virtual bool Button(std::string_view stableControlId, std::string_view label) = 0;
    virtual bool InputText(std::string_view stableControlId, std::string_view label, char* utf8Buffer,
                           size_t bufferCapacity) = 0;
    virtual bool Checkbox(std::string_view stableControlId, std::string_view label, bool* value) = 0;
    virtual bool InputInt(std::string_view stableControlId, std::string_view label, int32_t* value,
                          EditorWorkspaceIntInputOptions options = {}) = 0;
    virtual bool InputFloat(std::string_view stableControlId, std::string_view label, float* value,
                            EditorWorkspaceFloatInputOptions options = {}) = 0;
    virtual bool InputFloat2(std::string_view stableControlId, std::string_view label, EditorWorkspacePoint* value,
                             EditorWorkspaceFloatInputOptions options = {}) = 0;
    virtual bool Color4(std::string_view stableControlId, std::string_view label, EditorWorkspaceColor* value) = 0;
    virtual bool Selectable(std::string_view stableControlId, std::string_view label, bool selected = false) = 0;
    // EndTable is required only when BeginTable returns true.
    virtual bool BeginTable(std::string_view stableTableId, uint32_t columnCount) = 0;
    virtual void TableNextRow() = 0;
    virtual bool TableNextColumn() = 0;
    virtual void EndTable() = 0;
    virtual EditorWorkspaceCanvasInput BeginCanvas(std::string_view stableCanvasId, float requestedHeight) = 0;
    virtual void EndCanvas() = 0;
    virtual void CanvasLine(EditorWorkspacePoint from, EditorWorkspacePoint to, EditorWorkspaceColor color,
                            float thickness = 1.0f) = 0;
    virtual void CanvasRect(EditorWorkspacePoint minimum, EditorWorkspacePoint maximum, EditorWorkspaceColor color,
                            float thickness = 1.0f) = 0;
    virtual void CanvasCircle(EditorWorkspacePoint center, float radius, EditorWorkspaceColor color,
                              float thickness = 1.0f) = 0;
    virtual void CanvasText(EditorWorkspacePoint position, EditorWorkspaceColor color, std::string_view utf8Text) = 0;
    // Asset picker for one engine asset kind. assetGuidUtf8 holds the GUID (empty = none). Also accepts an asset of
    // that kind dragged from the asset browser.
    virtual bool AssetField(std::string_view stableControlId, std::string_view label, GameProjectAssetKind kind,
                            char* assetGuidUtf8, size_t bufferCapacity) = 0;
    virtual bool CanvasImage(std::string_view textureAssetGuid, EditorWorkspacePoint minimum,
                             EditorWorkspacePoint maximum, EditorWorkspaceColor tint = {}) = 0;
    virtual void CanvasFill(EditorWorkspacePoint minimum, EditorWorkspacePoint maximum, EditorWorkspaceColor color) = 0;
    virtual void CanvasStyledText(EditorWorkspacePoint position, float pixelHeight, float wrapWidth,
                                  EditorWorkspaceColor color, std::string_view utf8Text) = 0;
    virtual bool BeginCanvasPreview(uint32_t width, uint32_t height, GameProjectUiColor clearColor) = 0;
    virtual bool CanvasPreviewSprite(std::string_view textureGuid, const GameProjectUiSpriteRequest&) = 0;
    virtual bool CanvasPreviewText(std::string_view fontPath, std::string_view text,
                                   const GameProjectUiTextRequest&) = 0;
    virtual bool EndCanvasPreview(EditorWorkspacePoint minimum, EditorWorkspacePoint maximum) = 0;
    // Selectable sized to its text, for several choices on one line.
    virtual bool Choice(std::string_view stableControlId, std::string_view label, bool selected) = 0;
    // Slider over a range. Ctrl + click types a value. Wide positive ranges use a logarithmic scale.
    virtual bool SliderFloat(std::string_view stableControlId, std::string_view label, float* value, float minimum,
                             float maximum) = 0;
    // Collapsible heading. Returns whether its contents should be drawn; there is no matching end call.
    virtual bool Section(std::string_view stableControlId, std::string_view label, bool defaultOpen = true) = 0;
    // Shown when the pointer rests on the control drawn just before this call.
    virtual void Tooltip(std::string_view utf8Text) = 0;
    // Drop-down list; *selected is the index into items.
    virtual bool Combo(std::string_view stableControlId, std::string_view label,
                       std::span<const std::string_view> items, int32_t* selected) = 0;
    // Asset picker for an asset type that a module declares in its manifest (assetTypes[].typeId).
    virtual bool ModuleAssetField(std::string_view stableControlId, std::string_view label,
                                  std::string_view moduleAssetTypeId, char* assetGuidUtf8, size_t bufferCapacity) = 0;
    // List row with a picture. rgba8Pixels is sRGB, width * height * 4 bytes. The host keeps its own copy per control
    // and reads the pixels again only when revision changes. Null pixels draw a row without a picture.
    virtual bool ImageSelectable(std::string_view stableControlId, std::string_view label, const uint8_t* rgba8Pixels,
                                 uint32_t width, uint32_t height, uint64_t revision, bool selected,
                                 float rowHeight) = 0;
    // Draws the controls of a PropertyFields declaration (the same Describe that loads and saves the data, see
    // LoadPropertyFields / SavePropertyFields). Each field call inside Describe returns true when its value changed.
    virtual void Fields(std::string_view stableControlId, IPropertyFieldDeclaration& declaration) = 0;
    // Node graph control; types are in HIKARI/Project/EditorNodeGraph.h.
    virtual void NodeGraph(std::string_view stableGraphId, const EditorNodeGraphDesc& desc,
                           EditorNodeGraphResult& outResult) = 0;
    virtual void SetClipboardText(std::string_view utf8Text) = 0;
    // Copies the clipboard text with its terminator and returns the bytes the text needs including the terminator.
    // When the buffer is too small, it receives an empty string; call again with the returned size.
    virtual size_t GetClipboardText(char* utf8Buffer, size_t bufferCapacity) = 0;
};

// Results are borrowed for this callback only. Copy strings in the project
// if they are needed later; the host never transfers allocator ownership.
class IEditorOperationResultSink {
public:
    virtual ~IEditorOperationResultSink() = default;
    virtual void Receive(bool ok, std::string_view code, std::string_view message, std::string_view dataJson) = 0;
};

class IEditorWorkspaceCommands {
public:
    virtual ~IEditorWorkspaceCommands() = default;

    // Navigation requests are queued and applied outside the current
    // workspace callback, so activation never re-enters Draw(). A request made
    // while another editor workspace (such as Scene) is in front, for example
    // from an operation handler, also brings the project workspace to the front.
    virtual bool RequestActivateWorkspace(std::string_view workspaceId) = 0;
    virtual bool RequestCloseActiveWorkspace() = 0;
    virtual bool RequestSaveActiveWorkspace() = 0;
    virtual void PublishStatus(EditorWorkspaceStatusSeverity severity, std::string_view message) = 0;
    // Synchronous UI-thread domain operation. Uses the same declarations,
    // transactions and diagnostics as AI. Worker-only actions are rejected.
    virtual void ExecuteOperation(std::string_view operationId, std::string_view parametersJson,
                                  IEditorOperationResultSink& result) = 0;
};

class IEditorDocumentService;

struct EditorWorkspaceHostContext {
    uint32_t structSize = sizeof(EditorWorkspaceHostContext);
    uint32_t apiVersion = kEditorWorkspaceApiVersion;
    uint64_t frameIndex = 0u;
    float unscaledDeltaSeconds = 0.0f;
    uint32_t reserved = 0u;
    // These stable UTF-8 strings remain valid until the module is
    // unregistered. Project-relative writes remain project-owned.
    const char* projectRootUtf8 = nullptr;
    const char* moduleId = nullptr;
    const char* workspaceId = nullptr;
    IEditorWorkspaceUi* ui = nullptr;
    IEditorWorkspaceCommands* commands = nullptr;
    // Host-owned module-scoped service. Unlike frame-scoped pointers above,
    // this pointer remains valid until the module is unregistered.
    IEditorDocumentService* documents = nullptr;
};

class IEditorWorkspace {
public:
    virtual ~IEditorWorkspace() = default;

    // Descriptor strings remain valid for the lifetime of this object.
    // Every other callback runs synchronously on the Editor UI thread;
    // ui/commands pointers are frame-scoped. documents is the stable
    // module-scoped exception and remains valid until the module is unregistered.
    virtual EditorWorkspaceDescriptor GetDescriptor() const = 0;
    // Workspaces editing the same live document can switch without closing it.
    virtual std::string_view GetDocumentIdentity() const { return {}; }
    virtual void OnActivate(EditorWorkspaceHostContext& context) = 0;
    virtual void OnDeactivate(EditorWorkspaceHostContext& context) = 0;
    virtual void Draw(EditorWorkspaceHostContext& context) = 0;
    virtual bool IsDirty() const = 0;
    virtual EditorWorkspaceSaveResult Save(EditorWorkspaceHostContext& context) = 0;
    // Called exactly once when the author explicitly discards a draft.
    virtual void DiscardChanges(EditorWorkspaceHostContext&) {}
    virtual EditorWorkspaceCloseDisposition QueryClose(EditorWorkspaceCloseReason reason) const = 0;
    // The module-declared asset type (assetTypes[].typeId in the manifest) that this workspace edits, or empty.
    // When the author opens such an asset in the asset browser, the host activates this workspace and then calls
    // OpenAsset once, before the next Draw. The string must stay valid for the lifetime of this object.
    virtual std::string_view GetAssetTypeId() const { return {}; }
    virtual void OpenAsset(EditorWorkspaceHostContext&, std::string_view /*assetGuid*/) {}
};

static_assert(std::is_standard_layout_v<EditorWorkspaceDescriptor>);
static_assert(std::is_trivially_copyable_v<EditorWorkspaceDescriptor>);
static_assert(sizeof(EditorWorkspaceDescriptor) == 32u);
static_assert(offsetof(EditorWorkspaceDescriptor, options) == 24u);
static_assert(offsetof(EditorWorkspaceDescriptor, structSize) == 0u);
static_assert(offsetof(EditorWorkspaceDescriptor, apiVersion) == 4u);
static_assert(offsetof(EditorWorkspaceDescriptor, workspaceId) == 8u);
static_assert(offsetof(EditorWorkspaceDescriptor, displayName) == 16u);
static_assert(std::is_standard_layout_v<EditorWorkspacePoint>);
static_assert(std::is_trivially_copyable_v<EditorWorkspacePoint>);
static_assert(sizeof(EditorWorkspacePoint) == 8u);
static_assert(std::is_standard_layout_v<EditorWorkspaceColor>);
static_assert(std::is_trivially_copyable_v<EditorWorkspaceColor>);
static_assert(sizeof(EditorWorkspaceColor) == 16u);
static_assert(std::is_standard_layout_v<EditorWorkspaceIntInputOptions>);
static_assert(std::is_trivially_copyable_v<EditorWorkspaceIntInputOptions>);
static_assert(sizeof(EditorWorkspaceIntInputOptions) == 16u);
static_assert(std::is_standard_layout_v<EditorWorkspaceFloatInputOptions>);
static_assert(std::is_trivially_copyable_v<EditorWorkspaceFloatInputOptions>);
static_assert(sizeof(EditorWorkspaceFloatInputOptions) == 16u);
static_assert(std::is_standard_layout_v<EditorWorkspaceCanvasInput>);
static_assert(std::is_trivially_copyable_v<EditorWorkspaceCanvasInput>);
static_assert(sizeof(EditorWorkspaceCanvasInput) == 48u);
static_assert(offsetof(EditorWorkspaceCanvasInput, size) == 0u);
static_assert(offsetof(EditorWorkspaceCanvasInput, pointer) == 8u);
static_assert(offsetof(EditorWorkspaceCanvasInput, heldButtons) == 16u);
static_assert(offsetof(EditorWorkspaceCanvasInput, pressedButtons) == 20u);
static_assert(offsetof(EditorWorkspaceCanvasInput, releasedButtons) == 24u);
static_assert(offsetof(EditorWorkspaceCanvasInput, hovered) == 28u);
static_assert(std::is_standard_layout_v<EditorWorkspaceHostContext>);
static_assert(std::is_trivially_copyable_v<EditorWorkspaceHostContext>);
static_assert(sizeof(EditorWorkspaceHostContext) == 72u);
static_assert(offsetof(EditorWorkspaceHostContext, structSize) == 0u);
static_assert(offsetof(EditorWorkspaceHostContext, apiVersion) == 4u);
static_assert(offsetof(EditorWorkspaceHostContext, frameIndex) == 8u);
static_assert(offsetof(EditorWorkspaceHostContext, unscaledDeltaSeconds) == 16u);
static_assert(offsetof(EditorWorkspaceHostContext, reserved) == 20u);
static_assert(offsetof(EditorWorkspaceHostContext, projectRootUtf8) == 24u);
static_assert(offsetof(EditorWorkspaceHostContext, moduleId) == 32u);
static_assert(offsetof(EditorWorkspaceHostContext, workspaceId) == 40u);
static_assert(offsetof(EditorWorkspaceHostContext, ui) == 48u);
static_assert(offsetof(EditorWorkspaceHostContext, commands) == 56u);
static_assert(offsetof(EditorWorkspaceHostContext, documents) == 64u);

} // namespace HIKARI::EDITOR_PROJECT
