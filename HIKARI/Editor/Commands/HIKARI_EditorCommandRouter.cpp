#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

#include <utility>

#include <json.hpp>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
        size_t ToIndex(EditorCommandId command) noexcept {
            return static_cast<size_t>(command);
        }
    }

    void EditorCommandRouter::BeginFrame() noexcept {
        for (EditorCommandBinding& binding : bindings_) {
            binding = {};
        }
        supported_.fill(false);
    }

    void EditorCommandRouter::Bind(
        EditorCommandId command,
        std::string label,
        bool enabled,
        std::function<void()> execute) {

        const size_t index = ToIndex(command);
        if (index >= kCommandCount) {
            return;
        }
        bindings_[index] = {
            std::move(label),
            enabled,
            {},
            std::move(execute)
        };
        supported_[index] = true;
    }

    void EditorCommandRouter::BindOperation(
        EditorCommandId command,
        std::string label,
        bool enabled,
        std::string operationId) {

        const size_t index = ToIndex(command);
        if (index >= kCommandCount) {
            return;
        }
        bindings_[index] = {
            std::move(label),
            enabled,
            std::move(operationId),
            {}
        };
        supported_[index] = true;
    }

    void EditorCommandRouter::SetOperationRegistry(
        const EditorOperationRegistry* registry) noexcept {
        operationRegistry_ = registry;
    }

    const EditorCommandBinding* EditorCommandRouter::Find(
        EditorCommandId command) const noexcept {

        const size_t index = ToIndex(command);
        return index < kCommandCount && supported_[index]
            ? &bindings_[index]
            : nullptr;
    }

    bool EditorCommandRouter::IsSupported(
        EditorCommandId command) const noexcept {

        return Find(command) != nullptr;
    }

    bool EditorCommandRouter::CanExecute(
        EditorCommandId command) const noexcept {

        const EditorCommandBinding* binding = Find(command);
        if (binding == nullptr || !binding->enabled) {
            return false;
        }
        if (!binding->operationId.empty()) {
            return operationRegistry_ != nullptr;
        }
        return static_cast<bool>(binding->execute);
    }

    bool EditorCommandRouter::Execute(EditorCommandId command) const {
        const EditorCommandBinding* binding = Find(command);
        if (binding == nullptr || !binding->enabled) {
            return false;
        }
        if (!binding->operationId.empty()) {
            if (operationRegistry_ == nullptr) {
                return false;
            }
            const nlohmann::json parameters =
                nlohmann::json::object();
            return operationRegistry_->Execute(
                binding->operationId,
                parameters).ok;
        }
        if (!binding->execute) {
            return false;
        }
        binding->execute();
        return true;
    }

    bool EditorCommandRouter::ProcessDocumentShortcuts() const {
#if defined(HIKARI_WITH_EDITOR)
        if (ImGui::GetCurrentContext() == nullptr) {
            return false;
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl) {
            return false;
        }

        if (CanUseEditorShortcut(EditorShortcutScope::Document) &&
            ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            return Execute(EditorCommandId::SaveDocument);
        }
        if (!CanUseEditorShortcut(EditorShortcutScope::Editing)) {
            return false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
            (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))) {
            return Execute(EditorCommandId::Redo);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            return Execute(EditorCommandId::Undo);
        }
#endif
        return false;
    }

    bool EditorCommandRouter::ProcessViewportShortcuts(
        bool focused) const {
#if defined(HIKARI_WITH_EDITOR)
        if (!CanUseEditorShortcut(
                EditorShortcutScope::Viewport,
                focused)) {
            return false;
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            return Execute(EditorCommandId::DuplicateSelection);
        }
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            return Execute(EditorCommandId::DeleteSelection);
        }
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            return Execute(EditorCommandId::FocusSelection);
        }
#else
        (void)focused;
#endif
        return false;
    }

    const char* EditorCommandRouter::Shortcut(
        EditorCommandId command) noexcept {

        switch (command) {
        case EditorCommandId::SaveDocument: return "Ctrl+S";
        case EditorCommandId::Undo: return "Ctrl+Z";
        case EditorCommandId::Redo: return "Ctrl+Y / Ctrl+Shift+Z";
        case EditorCommandId::DuplicateSelection: return "Ctrl+D";
        case EditorCommandId::DeleteSelection: return "Delete";
        case EditorCommandId::FocusSelection: return "F";
        case EditorCommandId::Count:
        default:
            return "";
        }
    }

    bool CanUseEditorShortcut(
        EditorShortcutScope scope,
        bool focused) noexcept {
#if defined(HIKARI_WITH_EDITOR)
        if (ImGui::GetCurrentContext() == nullptr) {
            return false;
        }
        if (scope != EditorShortcutScope::Document && !focused) {
            return false;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
            ImGui::GetDragDropPayload() != nullptr) {
            return false;
        }
        if (scope != EditorShortcutScope::Document &&
            (io.WantTextInput || ImGui::IsAnyItemActive())) {
            return false;
        }
        if (scope == EditorShortcutScope::Viewport &&
            ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            return false;
        }
        return true;
#else
        (void)scope;
        (void)focused;
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
