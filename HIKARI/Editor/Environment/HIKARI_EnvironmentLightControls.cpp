#include "Editor/Environment/HIKARI_EnvironmentPanelSections.h"

#if defined(HIKARI_WITH_EDITOR)

#include "imgui.h"

namespace HIKARI::EDITOR::ENVIRONMENT_PANEL {

void DrawEnvironmentLightingControls(SceneEnvironment &environment) {
  if (ImGui::TreeNodeEx("Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit3("Ambient Color", &environment.ambient.color.x);
    ImGui::DragFloat("Ambient Intensity", &environment.ambient.intensity, 0.01f,
                     0.0f, 10.0f);
    ImGui::Checkbox("Use Sky Color", &environment.ambient.useSkyColor);
    ImGui::DragFloat("Sky Ambient Blend", &environment.ambient.skyBlend, 0.01f,
                     0.0f, 1.0f);
    ImGui::TreePop();
  }

  ImGui::SeparatorText("Scene Lights");
  ImGui::TextWrapped(
      "Directional, point and spot lights are scene objects. "
      "Create them from Create > Lighting and edit the Light component.");
}

} // namespace HIKARI::EDITOR::ENVIRONMENT_PANEL

#endif
