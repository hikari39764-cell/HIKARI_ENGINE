#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace HIKARI::EDITOR {

    class EditorOperationRegistry;

    enum class EditorCommandId : uint8_t {
        SaveDocument,
        Undo,
        Redo,
        DuplicateSelection,
        DeleteSelection,
        FocusSelection,
        Count,
    };

    enum class EditorShortcutScope : uint8_t {
        Document,
        Editing,
        Viewport,
    };

    struct EditorCommandBinding {
        std::string label{};
        bool enabled = false;
        std::string operationId{};
        std::function<void()> execute{};
    };

    class EditorCommandRouter {
    public:
        void BeginFrame() noexcept;
        void Bind(
            EditorCommandId command,
            std::string label,
            bool enabled,
            std::function<void()> execute);
        // Binds a human command to a registered editor operation. Execute()
        // routes through the operation registry with an empty parameter
        // object, so human UI and AI share the same implementation path.
        void BindOperation(
            EditorCommandId command,
            std::string label,
            bool enabled,
            std::string operationId);
        void SetOperationRegistry(
            const EditorOperationRegistry* registry) noexcept;

        const EditorCommandBinding* Find(
            EditorCommandId command) const noexcept;
        bool IsSupported(EditorCommandId command) const noexcept;
        bool CanExecute(EditorCommandId command) const noexcept;
        bool Execute(EditorCommandId command) const;

        bool ProcessDocumentShortcuts() const;
        bool ProcessViewportShortcuts(bool focused) const;

        static const char* Shortcut(EditorCommandId command) noexcept;

    private:
        static constexpr size_t kCommandCount =
            static_cast<size_t>(EditorCommandId::Count);

        std::array<EditorCommandBinding, kCommandCount> bindings_{};
        std::array<bool, kCommandCount> supported_{};
        const EditorOperationRegistry* operationRegistry_ = nullptr;
    };

    // Consumes the key once per frame; another canvas must not also act on it.
    bool ConsumeEditorShortcut(EditorShortcutScope scope, int key,
        bool focused = true, bool repeat = false) noexcept;

    bool CanUseEditorShortcut(
        EditorShortcutScope scope,
        bool focused = true) noexcept;

} // namespace HIKARI::EDITOR
