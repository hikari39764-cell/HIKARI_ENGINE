#include "Editor/Environment/HIKARI_EnvironmentPanelSections.h"

#if defined(HIKARI_WITH_EDITOR)

#include "imgui.h"
#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI::EDITOR::ENVIRONMENT_PANEL {

void DrawEnvironmentLightingControls(SceneEnvironment& environment) {
    if (ImGui::TreeNodeEx(TEXT::Localize("ui.environment.ambient").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3(TEXT::Localize("ui.environment.ambientColor").c_str(), &environment.ambient.color.x);
        ImGui::DragFloat(TEXT::Localize("ui.environment.ambientIntensity").c_str(), &environment.ambient.intensity,
                         0.01f, 0.0f, 10.0f);
        ImGui::Checkbox(TEXT::Localize("ui.environment.useSkyColor").c_str(), &environment.ambient.useSkyColor);
        ImGui::DragFloat(TEXT::Localize("ui.environment.skyAmbientBlend").c_str(), &environment.ambient.skyBlend, 0.01f,
                         0.0f, 1.0f);
        ImGui::TreePop();
    }

    ImGui::SeparatorText(TEXT::Localize("ui.environment.sceneLights").c_str());
    ImGui::TextWrapped("%s", TEXT::Localize("ui.environment.sceneLightsHint").c_str());
}

} // namespace HIKARI::EDITOR::ENVIRONMENT_PANEL

#endif
