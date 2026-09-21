#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

#include <utility>

#include <json.hpp>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
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
        if (!io.KeyCtrl || io.KeyAlt || io.KeySuper) {
            return false;
        }

        if (CanUseEditorShortcut(EditorShortcutScope::Document) &&
            ConsumeEditorShortcut(EditorShortcutScope::Document, ImGuiKey_S)) {
            return Execute(EditorCommandId::SaveDocument);
        }
        if (!CanUseEditorShortcut(EditorShortcutScope::Editing)) {
            return false;
        }
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Y) ||
            (io.KeyShift && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Z))) {
            return Execute(EditorCommandId::Redo);
        }
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Z)) {
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
        if (io.KeyAlt || io.KeySuper) return false;
        if (io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Viewport, ImGuiKey_D, focused)) {
            return Execute(EditorCommandId::DuplicateSelection);
        }
        if (!io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Viewport, ImGuiKey_Delete, focused)) {
            return Execute(EditorCommandId::DeleteSelection);
        }
        if (!io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Viewport, ImGuiKey_F, focused)) {
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

    bool ConsumeEditorShortcut(EditorShortcutScope scope, int key, bool focused, bool repeat) noexcept {
#if defined(HIKARI_WITH_EDITOR)
        if (!CanUseEditorShortcut(scope, focused)) return false;
        const auto& io = ImGui::GetIO();
        if (io.KeyAlt || io.KeySuper || (io.KeyCtrl && (key == ImGuiKey_F || key == ImGuiKey_Delete))) return false;
        if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(key),
                repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None, ImGuiKeyOwner_Any)) return false;
        ImGui::SetKeyOwner(static_cast<ImGuiKey>(key), ImHashStr("HIKARI.EditorShortcut"), ImGuiInputFlags_LockThisFrame);
        return true;
#else
        (void)scope; (void)key; (void)focused; (void)repeat; return false;
#endif
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
        if (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) return false;
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
            ImGui::GetDragDropPayload() != nullptr) {
            return false;
        }
        if (scope != EditorShortcutScope::Document &&
            (io.WantTextInput ||
                (GImGui->ActiveId != 0 && GImGui->ActiveId == GImGui->InputTextState.ID) ||
                (ImGui::IsAnyItemActive() && (GImGui->ActiveIdSource == ImGuiInputSource_Keyboard ||
                    ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || ImGui::IsMouseDown(2))))) {
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
