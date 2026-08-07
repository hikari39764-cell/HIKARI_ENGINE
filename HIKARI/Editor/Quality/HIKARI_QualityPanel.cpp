#include "HIKARI_QualityPanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Environment/HIKARI_EnvironmentEditComparison.h"
#include "Editor/Quality/HIKARI_QualityPanelSections.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_PostProfileParameterWidget.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#endif

namespace HIKARI {

using namespace EDITOR::QUALITY_PANEL;

#if defined(HIKARI_WITH_EDITOR)
namespace {}

QualityPanelResult QualityPanel::Draw(SceneEnvironment &environment) const {
  if (!ImGui::Begin(
          TEXT::LocalizeWindow(
              "ui.window.quality", "Quality").c_str())) {
    ImGui::End();
    return {};
  }

  const SceneEnvironment beforeEdit = environment;
  const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot runtimeSnapshot =
      RENDER3D::DIAGNOSTICS::CaptureEnvironmentSnapshot(&environment);

  const bool renderQualityChanged = DrawRenderSettings();

  if (ImGui::TreeNodeEx(
          TEXT::Localize("ui.quality.directionalShadow").c_str(),
          ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawDirectionalShadow(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx(
          TEXT::Localize("ui.quality.ambientOcclusion").c_str(),
          ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawAmbientOcclusion(environment, runtimeSnapshot);
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx(
          TEXT::Localize("ui.quality.bakedIndirectLighting").c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSurfaceLightmaps(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx(
          TEXT::Localize("ui.quality.screenSpaceReflections").c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawScreenSpaceReflections(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode(TEXT::Localize("ui.quality.bloom").c_str())) {
    DrawBloom(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode(TEXT::Localize("ui.quality.toneMapping").c_str())) {
    DrawToneMapping(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode(TEXT::Localize("ui.quality.globalPost").c_str())) {
    DrawGlobalPost(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode(TEXT::Localize("ui.quality.transitionDebug").c_str())) {
    DrawTransitionDebug();
    ImGui::TreePop();
  }

  const bool editActive =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsAnyItemActive();
  ImGui::End();
  return {
      !EDITOR::ENVIRONMENT_EDIT::AreQualityEnvironmentValuesEqual(beforeEdit,
                                                                  environment),
      renderQualityChanged,
      editActive,
  };
}
#else
QualityPanelResult QualityPanel::Draw(SceneEnvironment &) const { return {}; }
#endif

} // namespace HIKARI
