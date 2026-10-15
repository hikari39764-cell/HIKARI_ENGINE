#include "Editor/Views/HIKARI_EditorViewInputGate.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

EditorViewInputBlockState QueryEditorViewInputBlockState() noexcept {
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const ImGuiIO& io = ImGui::GetIO();
    const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool dragDropActive = ImGui::GetDragDropPayload() != nullptr;
    EditorViewInputBlockState state{};
    state.pointer = popupOpen || dragDropActive;
    state.keyboard = state.pointer || io.WantTextInput || ImGui::IsAnyItemActive();
    return state;
}

} // namespace HIKARI::EDITOR
