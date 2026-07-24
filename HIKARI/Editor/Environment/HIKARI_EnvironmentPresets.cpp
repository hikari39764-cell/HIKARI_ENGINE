#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Environment/HIKARI_EnvironmentEditComparison.h"
#include "Editor/Environment/HIKARI_EnvironmentPanel.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>
#endif
#include "Editor/Environment/HIKARI_EnvironmentPanelSections.h"

namespace HIKARI::EDITOR::ENVIRONMENT_PANEL {

#if defined(HIKARI_WITH_EDITOR)
void NormalizeDirectionalLight(DirectionalLight &light) {
  light.direction = MATH::Normalize(light.direction);
  if (MATH::Length(light.direction) <= 1e-6f) {
    light.direction = MATH::Normalize(MATH::Vec3{0.4f, -1.0f, -0.6f});
  }
}

void ApplyEnvironmentPreset(SceneEnvironment &environment, int presetIndex) {
  switch (presetIndex) {
  case 1: // Bright Day
    environment.directional.color = {1.0f, 0.96f, 0.88f};
    environment.directional.intensity = 1.6f;
    environment.ambient.color = {0.78f, 0.86f, 1.0f};
    environment.ambient.intensity = 0.35f;
    environment.bloom.enabled = true;
    environment.bloom.intensity = 0.35f;
    environment.fog.enabled = false;
    environment.toneMapping.exposure = 1.0f;
    environment.directionalShadow.strength = 0.65f;
    break;
  case 2: // Sunset
    environment.directional.color = {1.0f, 0.58f, 0.32f};
    environment.directional.intensity = 1.2f;
    environment.ambient.color = {0.35f, 0.35f, 0.65f};
    environment.ambient.intensity = 0.25f;
    environment.fog.enabled = true;
    environment.fog.color = {0.9f, 0.5f, 0.35f};
    environment.fog.density = 0.015f;
    environment.bloom.enabled = true;
    environment.bloom.intensity = 0.7f;
    environment.toneMapping.exposure = 1.1f;
    environment.directionalShadow.strength = 0.7f;
    break;
  case 3: // Night
    environment.directional.color = {0.45f, 0.55f, 1.0f};
    environment.directional.intensity = 0.25f;
    environment.ambient.color = {0.08f, 0.1f, 0.18f};
    environment.ambient.intensity = 0.18f;
    environment.bloom.enabled = true;
    environment.bloom.intensity = 0.9f;
    environment.fog.enabled = true;
    environment.fog.color = {0.05f, 0.07f, 0.13f};
    environment.fog.density = 0.01f;
    environment.toneMapping.exposure = 1.2f;
    environment.directionalShadow.strength = 0.45f;
    break;
  case 4: // Overcast
    environment.directional.color = {0.85f, 0.9f, 1.0f};
    environment.directional.intensity = 0.65f;
    environment.ambient.color = {0.7f, 0.75f, 0.82f};
    environment.ambient.intensity = 0.5f;
    environment.bloom.intensity = 0.2f;
    environment.fog.enabled = true;
    environment.fog.color = {0.65f, 0.7f, 0.75f};
    environment.fog.density = 0.012f;
    environment.toneMapping.exposure = 0.95f;
    environment.directionalShadow.strength = 0.35f;
    break;
  case 5: // Stylized Blue
    environment.directional.color = {0.65f, 0.85f, 1.0f};
    environment.directional.intensity = 1.1f;
    environment.ambient.color = {0.18f, 0.28f, 0.55f};
    environment.ambient.intensity = 0.35f;
    environment.bloom.enabled = true;
    environment.bloom.intensity = 0.8f;
    environment.toneMapping.exposure = 1.15f;
    break;
  case 6: // Warm Indoor
    environment.directional.color = {1.0f, 0.78f, 0.5f};
    environment.directional.intensity = 0.75f;
    environment.ambient.color = {1.0f, 0.72f, 0.45f};
    environment.ambient.intensity = 0.32f;
    environment.bloom.intensity = 0.45f;
    environment.fog.enabled = false;
    environment.toneMapping.exposure = 1.05f;
    break;
  case 0:
  default:
    environment = SceneEnvironment{};
    break;
  }
  NormalizeDirectionalLight(environment.directional);
}
#endif

} // namespace HIKARI::EDITOR::ENVIRONMENT_PANEL
