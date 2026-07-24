#include "Editor/Views/HIKARI_EditorViewInputGate.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    EditorViewInputBlockState QueryEditorViewInputBlockState() noexcept {
#if defined(HIKARI_WITH_EDITOR)
        if (ImGui::GetCurrentContext() == nullptr) {
            return {};
        }

        const ImGuiIO& io = ImGui::GetIO();
        const bool popupOpen =
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        const bool dragDropActive =
            ImGui::GetDragDropPayload() != nullptr;
        EditorViewInputBlockState state{};
        state.pointer = popupOpen || dragDropActive;
        state.keyboard =
            state.pointer || io.WantTextInput || ImGui::IsAnyItemActive();
        return state;
#else
        return {};
#endif
    }

} // namespace HIKARI::EDITOR
