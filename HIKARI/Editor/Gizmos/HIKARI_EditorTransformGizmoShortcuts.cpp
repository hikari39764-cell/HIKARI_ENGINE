#include "Editor/Gizmos/HIKARI_EditorTransformGizmoShortcuts.h"

#include "Editor/Commands/HIKARI_EditorCommandRouter.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

void HandleTransformGizmoShortcuts(EditorTransformGizmoState& state, bool viewportFocused) {
    if (!CanUseEditorShortcut(EditorShortcutScope::Viewport, viewportFocused)) {
        return;
    }
    const auto& io = ImGui::GetIO();
    if (io.KeyCtrl || io.KeyAlt || io.KeySuper)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_Q) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        state.enabled = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_W)) {
        state.enabled = true;
        state.operation = EditorTransformGizmoOperation::Translate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E)) {
        state.enabled = true;
        state.operation = EditorTransformGizmoOperation::Rotate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        state.enabled = true;
        state.operation = EditorTransformGizmoOperation::Scale;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_X)) {
        state.mode = state.mode == EditorTransformGizmoMode::World ? EditorTransformGizmoMode::Local
                                                                   : EditorTransformGizmoMode::World;
    }
}

} // namespace HIKARI::EDITOR
