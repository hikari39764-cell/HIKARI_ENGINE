#include "Editor/Panels/HIKARI_InputActionMapPanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>

#include "Input/Assets/HIKARI_InputActionMap.h"
#include "Input/Runtime/HIKARI_InputService.h"

#include "imgui.h"

namespace HIKARI {

void InputActionMapPanel::RequestOpen() noexcept { openRequested_ = true; }

void InputActionMapPanel::DrawModal(INPUT::InputService& inputService) {
    constexpr const char* kPopupId = "Project Input Editor###HikariProjectInputEditor";
    if (openRequested_) {
        ImGui::OpenPopup(kPopupId);
        openRequested_ = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 modalSize{(std::min)(1180.0f, viewport->WorkSize.x * 0.92f),
                           (std::min)(780.0f, viewport->WorkSize.y * 0.90f)};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_Appearing);

    bool keepOpen = true;
    if (ImGui::BeginPopupModal(kPopupId, &keepOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + 6.0f;
        if (ImGui::BeginChild("##InputEditorBody", ImVec2(0.0f, -footerHeight), false, ImGuiWindowFlags_NoScrollbar)) {
            editor_.Draw(inputService);
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (editor_.IsDirty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s",
                               TEXT::Localize("ui.input.unsavedChanges").c_str());
            ImGui::SameLine();
        } else {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.input.allChangesSaved").c_str());
            ImGui::SameLine();
        }
        const float closeWidth = 110.0f;
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - closeWidth));
        if (ImGui::Button(TEXT::Localize("ui.input.close").c_str(), ImVec2(closeWidth, 0.0f))) {
            keepOpen = false;
        }
        if (!keepOpen) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace HIKARI
