#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace HIKARI::EDITOR {

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
    };

    bool CanUseEditorShortcut(
        EditorShortcutScope scope,
        bool focused = true) noexcept;

} // namespace HIKARI::EDITOR
