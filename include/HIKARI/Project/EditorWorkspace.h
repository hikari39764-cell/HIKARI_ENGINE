#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace HIKARI::EDITOR_PROJECT {

    inline constexpr uint32_t kEditorWorkspaceApiVersion = 1u;

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
    };

    class IEditorWorkspaceUi {
    public:
        virtual ~IEditorWorkspaceUi() = default;

        // BeginPanel/EndPanel and BeginCanvas/EndCanvas are paired even when
        // BeginPanel reports that its collapsed contents are not visible.
        virtual bool BeginPanel(
            std::string_view stablePanelId,
            std::string_view displayName,
            bool* open = nullptr) = 0;
        virtual void EndPanel() = 0;
        virtual void Text(std::string_view utf8Text) = 0;
        virtual void Separator() = 0;
        virtual void SameLine() = 0;
        virtual bool Button(
            std::string_view stableControlId,
            std::string_view label) = 0;
        virtual bool InputText(
            std::string_view stableControlId,
            std::string_view label,
            char* utf8Buffer,
            size_t bufferCapacity) = 0;
        virtual bool Checkbox(
            std::string_view stableControlId,
            std::string_view label,
            bool* value) = 0;
        virtual bool InputInt(
            std::string_view stableControlId,
            std::string_view label,
            int32_t* value,
            EditorWorkspaceIntInputOptions options = {}) = 0;
        virtual bool InputFloat(
            std::string_view stableControlId,
            std::string_view label,
            float* value,
            EditorWorkspaceFloatInputOptions options = {}) = 0;
        virtual bool InputFloat2(
            std::string_view stableControlId,
            std::string_view label,
            EditorWorkspacePoint* value,
            EditorWorkspaceFloatInputOptions options = {}) = 0;
        virtual bool Color4(
            std::string_view stableControlId,
            std::string_view label,
            EditorWorkspaceColor* value) = 0;
        virtual bool Selectable(
            std::string_view stableControlId,
            std::string_view label,
            bool selected = false) = 0;
        // EndTable is required only when BeginTable returns true.
        virtual bool BeginTable(
            std::string_view stableTableId,
            uint32_t columnCount) = 0;
        virtual void TableNextRow() = 0;
        virtual bool TableNextColumn() = 0;
        virtual void EndTable() = 0;
        virtual EditorWorkspaceCanvasInput BeginCanvas(
            std::string_view stableCanvasId,
            float requestedHeight) = 0;
        virtual void EndCanvas() = 0;
        virtual void CanvasLine(
            EditorWorkspacePoint from,
            EditorWorkspacePoint to,
            EditorWorkspaceColor color,
            float thickness = 1.0f) = 0;
        virtual void CanvasRect(
            EditorWorkspacePoint minimum,
            EditorWorkspacePoint maximum,
            EditorWorkspaceColor color,
            float thickness = 1.0f) = 0;
        virtual void CanvasCircle(
            EditorWorkspacePoint center,
            float radius,
            EditorWorkspaceColor color,
            float thickness = 1.0f) = 0;
        virtual void CanvasText(
            EditorWorkspacePoint position,
            EditorWorkspaceColor color,
            std::string_view utf8Text) = 0;
    };

    class IEditorWorkspaceCommands {
    public:
        virtual ~IEditorWorkspaceCommands() = default;

        // Navigation requests are queued and applied outside the current
        // workspace callback, so activation never re-enters Draw().
        virtual bool RequestActivateWorkspace(
            std::string_view workspaceId) = 0;
        virtual bool RequestCloseActiveWorkspace() = 0;
        virtual bool RequestSaveActiveWorkspace() = 0;
        virtual void PublishStatus(
            EditorWorkspaceStatusSeverity severity,
            std::string_view message) = 0;
    };

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
    };

    class IEditorWorkspace {
    public:
        virtual ~IEditorWorkspace() = default;

        // Descriptor strings remain valid for the lifetime of this object.
        // Every other callback runs synchronously on the Editor UI thread;
        // context pointers must not be retained beyond that callback.
        virtual EditorWorkspaceDescriptor GetDescriptor() const = 0;
        virtual void OnActivate(EditorWorkspaceHostContext& context) = 0;
        virtual void OnDeactivate(EditorWorkspaceHostContext& context) = 0;
        virtual void Draw(EditorWorkspaceHostContext& context) = 0;
        virtual bool IsDirty() const = 0;
        virtual EditorWorkspaceSaveResult Save(
            EditorWorkspaceHostContext& context) = 0;
        virtual EditorWorkspaceCloseDisposition QueryClose(
            EditorWorkspaceCloseReason reason) const = 0;
    };

    static_assert(std::is_standard_layout_v<EditorWorkspaceDescriptor>);
    static_assert(std::is_trivially_copyable_v<EditorWorkspaceDescriptor>);
    static_assert(sizeof(EditorWorkspaceDescriptor) == 24u);
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
    static_assert(
        std::is_trivially_copyable_v<EditorWorkspaceIntInputOptions>);
    static_assert(sizeof(EditorWorkspaceIntInputOptions) == 16u);
    static_assert(
        std::is_standard_layout_v<EditorWorkspaceFloatInputOptions>);
    static_assert(
        std::is_trivially_copyable_v<EditorWorkspaceFloatInputOptions>);
    static_assert(sizeof(EditorWorkspaceFloatInputOptions) == 16u);
    static_assert(std::is_standard_layout_v<EditorWorkspaceCanvasInput>);
    static_assert(std::is_trivially_copyable_v<EditorWorkspaceCanvasInput>);
    static_assert(sizeof(EditorWorkspaceCanvasInput) == 32u);
    static_assert(offsetof(EditorWorkspaceCanvasInput, size) == 0u);
    static_assert(offsetof(EditorWorkspaceCanvasInput, pointer) == 8u);
    static_assert(offsetof(EditorWorkspaceCanvasInput, heldButtons) == 16u);
    static_assert(offsetof(EditorWorkspaceCanvasInput, pressedButtons) == 20u);
    static_assert(offsetof(EditorWorkspaceCanvasInput, releasedButtons) == 24u);
    static_assert(offsetof(EditorWorkspaceCanvasInput, hovered) == 28u);
    static_assert(std::is_standard_layout_v<EditorWorkspaceHostContext>);
    static_assert(std::is_trivially_copyable_v<EditorWorkspaceHostContext>);
    static_assert(sizeof(EditorWorkspaceHostContext) == 64u);
    static_assert(offsetof(EditorWorkspaceHostContext, structSize) == 0u);
    static_assert(offsetof(EditorWorkspaceHostContext, apiVersion) == 4u);
    static_assert(offsetof(EditorWorkspaceHostContext, frameIndex) == 8u);
    static_assert(
        offsetof(EditorWorkspaceHostContext, unscaledDeltaSeconds) == 16u);
    static_assert(offsetof(EditorWorkspaceHostContext, reserved) == 20u);
    static_assert(
        offsetof(EditorWorkspaceHostContext, projectRootUtf8) == 24u);
    static_assert(offsetof(EditorWorkspaceHostContext, moduleId) == 32u);
    static_assert(offsetof(EditorWorkspaceHostContext, workspaceId) == 40u);
    static_assert(offsetof(EditorWorkspaceHostContext, ui) == 48u);
    static_assert(offsetof(EditorWorkspaceHostContext, commands) == 56u);

} // namespace HIKARI::EDITOR_PROJECT
