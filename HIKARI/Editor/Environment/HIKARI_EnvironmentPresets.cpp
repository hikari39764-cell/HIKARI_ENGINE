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
void ApplyEnvironmentPreset(SceneEnvironment &environment, int presetIndex) {
  switch (presetIndex) {
  case 1: // Bright Day
    environment.ambient.color = {0.78f, 0.86f, 1.0f};
    environment.ambient.intensity = 0.35f;
    environment.bloom.enabled = true;
    environment.bloom.intensity = 0.35f;
    environment.fog.enabled = false;
    environment.toneMapping.exposure = 1.0f;
    environment.directionalShadow.strength = 0.65f;
    break;
  case 2: // Sunset
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
    environment.ambient.color = {0.18f, 0.28f, 0.55f};
    environment.ambient.intensity = 0.35f;
    environment.bloom.enabled = true;
    environment.bloom.intensity = 0.8f;
    environment.toneMapping.exposure = 1.15f;
    break;
  case 6: // Warm Indoor
    environment.ambient.color = {1.0f, 0.72f, 0.45f};
    environment.ambient.intensity = 0.32f;
    environment.bloom.intensity = 0.45f;
    environment.fog.enabled = false;
    environment.toneMapping.exposure = 1.05f;
    break;
  case 7: // Fresh Summer - clear noon atmosphere
    environment.ambient.color = {0.72f, 0.84f, 1.0f};
    environment.ambient.intensity = 0.38f;
    environment.directional.enabled = true;
    environment.directional.direction = {0.28f, -1.0f, -0.48f};
    environment.directional.intensity = 1.35f;
    environment.directional.color = {1.0f, 0.93f, 0.82f};
    environment.sky.enabled = true;
    environment.sky.mode = SkyMode::Atmosphere;
    environment.sky.zenithColor = {0.10f, 0.34f, 0.88f};
    environment.sky.horizonColor = {0.72f, 0.90f, 1.0f};
    environment.sky.groundColor = {0.18f, 0.28f, 0.40f};
    environment.sky.horizonPower = 1.55f;
    environment.sky.showSunDisk = true;
    environment.sky.sunDiskIntensity = 2.5f;
    environment.sky.sunDiskSize = 0.035f;
    environment.sky.ambientFromSky = 1.2f;
    environment.sky.reflectionIntensity = 1.0f;
    environment.atmosphere = AtmosphereSettings{};
    environment.atmosphere.enabled = true;
    environment.atmosphere.sunIlluminance = 1.0f;
    environment.atmosphere.sunExposure = 0.0f;
    environment.atmosphere.groundAlbedo = 0.12f;
    environment.atmosphere.sunAngularRadius = 0.0065f;
    environment.atmosphere.preferLut = true;
    environment.sunGlare = SunGlareSettings{};
    environment.sunGlare.enabled = true;
    environment.sunGlare.intensity = 0.10f;
    environment.sunGlare.haloRadius = 0.055f;
    environment.sunGlare.haloFalloff = 3.0f;
    environment.sunGlare.occlusionRadius = 0.012f;
    environment.sunGlare.edgeFade = 0.18f;
    environment.sunGlare.shaftIntensity = 0.34f;
    environment.sunGlare.shaftLength = 0.72f;
    environment.sunGlare.shaftDecay = 0.955f;
    environment.clouds = StylizedCloudSettings{};
    environment.clouds.enabled = true;
    environment.toneMapping.exposure = 1.05f;
    environment.fog.enabled = true;
    environment.fog.volumetric = true;
    environment.fog.density = 0.0065f;
    environment.fog.startDistance = 5.0f;
    environment.fog.endDistance = 150.0f;
    environment.fog.heightFalloff = 0.012f;
    environment.fog.baseHeight = 0.0f;
    environment.fog.anisotropy = 0.68f;
    environment.fog.temporalWeight = 0.86f;
    environment.fog.scatteringIntensity = 0.75f;
    environment.fog.ambientScatteringStrength = 0.55f;
    environment.fog.sunShaftStrength = 1.4f;
    environment.fog.useSkyHorizonColor = true;
    break;
  case 8: // Backlit Golden Hour Atmosphere
    environment.ambient.color = {0.42f, 0.30f, 0.28f};
    environment.ambient.intensity = 0.24f;
    environment.directional.enabled = true;
    environment.directional.direction = {-0.72f, -0.34f, 0.58f};
    environment.directional.intensity = 0.82f;
    environment.directional.color = {1.0f, 0.58f, 0.30f};
    environment.sky.enabled = true;
    environment.sky.mode = SkyMode::Atmosphere;
    environment.sky.zenithColor = {0.08f, 0.12f, 0.30f};
    environment.sky.horizonColor = {0.96f, 0.42f, 0.18f};
    environment.sky.groundColor = {0.16f, 0.08f, 0.06f};
    environment.sky.horizonPower = 1.25f;
    environment.sky.showSunDisk = true;
    environment.sky.sunDiskIntensity = 1.2f;
    environment.sky.sunDiskSize = 0.045f;
    environment.sky.ambientFromSky = 0.85f;
    environment.sky.reflectionIntensity = 0.72f;
    environment.atmosphere = AtmosphereSettings{};
    environment.atmosphere.enabled = true;
    environment.atmosphere.sunIlluminance = 0.72f;
    environment.atmosphere.sunExposure = -0.35f;
    environment.atmosphere.mieAnisotropy = 0.80f;
    environment.atmosphere.groundAlbedo = 0.08f;
    environment.atmosphere.sunAngularRadius = 0.008f;
    environment.atmosphere.preferLut = true;
    environment.sunGlare = SunGlareSettings{};
    environment.sunGlare.enabled = true;
    environment.sunGlare.intensity = 0.10f;
    environment.sunGlare.haloRadius = 0.06f;
    environment.sunGlare.haloFalloff = 3.5f;
    environment.sunGlare.occlusionRadius = 0.014f;
    environment.sunGlare.edgeFade = 0.20f;
    environment.sunGlare.shaftIntensity = 0.55f;
    environment.sunGlare.shaftLength = 0.82f;
    environment.sunGlare.shaftDecay = 0.962f;
    environment.clouds = StylizedCloudSettings{};
    environment.clouds.enabled = false;
    environment.toneMapping.exposure = 1.0f;
    environment.fog.enabled = true;
    environment.fog.volumetric = true;
    environment.fog.color = {0.92f, 0.58f, 0.34f};
    environment.fog.density = 0.012f;
    environment.fog.startDistance = 3.0f;
    environment.fog.endDistance = 135.0f;
    environment.fog.heightFalloff = 0.018f;
    environment.fog.baseHeight = 0.0f;
    environment.fog.anisotropy = 0.76f;
    environment.fog.temporalWeight = 0.84f;
    environment.fog.scatteringIntensity = 0.9f;
    environment.fog.ambientScatteringStrength = 0.45f;
    environment.fog.sunShaftStrength = 1.75f;
    break;
  case 0:
  default:
    environment = SceneEnvironment{};
    break;
  }
}
#endif

} // namespace HIKARI::EDITOR::ENVIRONMENT_PANEL
