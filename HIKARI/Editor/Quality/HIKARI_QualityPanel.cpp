#include "HIKARI_QualityPanel.h"

#include "Editor/Environment/HIKARI_EnvironmentEditComparison.h"
#include "Editor/Quality/HIKARI_QualityPanelSections.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_PostProfileParameterWidget.h"
#include "HIKARI_Services.h"
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
  if (!ImGui::Begin("Quality")) {
    ImGui::End();
    return {};
  }

  const SceneEnvironment beforeEdit = environment;
  const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot runtimeSnapshot =
      RENDER3D::DIAGNOSTICS::CaptureEnvironmentSnapshot(&environment);

  const bool renderQualityChanged = DrawRenderSettings();

  if (ImGui::TreeNodeEx("Directional Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawDirectionalShadow(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawAmbientOcclusion(environment, runtimeSnapshot);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Bloom")) {
    DrawBloom(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Tone Mapping")) {
    DrawToneMapping(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Global Post")) {
    DrawGlobalPost(environment);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Transition Debug")) {
    DrawTransitionDebug();
    ImGui::TreePop();
  }

  ImGui::End();
  return {
      !EDITOR::ENVIRONMENT_EDIT::AreQualityEnvironmentValuesEqual(beforeEdit,
                                                                  environment),
      renderQualityChanged,
  };
}
#else
QualityPanelResult QualityPanel::Draw(SceneEnvironment &) const { return {}; }
#endif

} // namespace HIKARI
