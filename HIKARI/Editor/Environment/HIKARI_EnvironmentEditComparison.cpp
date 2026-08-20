#include "Editor/Environment/HIKARI_EnvironmentEditComparison.h"

namespace HIKARI::EDITOR::ENVIRONMENT_EDIT {

namespace {

bool EqualVec3(const MATH::Vec3 &lhs, const MATH::Vec3 &rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool EqualFloat4(const DirectX::XMFLOAT4 &lhs, const DirectX::XMFLOAT4 &rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

bool EqualSkySettings(const SkySettings &lhs, const SkySettings &rhs) {
  return lhs.enabled == rhs.enabled && lhs.mode == rhs.mode &&
         lhs.skyAsset == rhs.skyAsset && lhs.scale == rhs.scale &&
         lhs.yaw == rhs.yaw && lhs.exposure == rhs.exposure &&
         EqualVec3(lhs.tint, rhs.tint) &&
         lhs.followCamera == rhs.followCamera &&
         EqualVec3(lhs.zenithColor, rhs.zenithColor) &&
         EqualVec3(lhs.horizonColor, rhs.horizonColor) &&
         EqualVec3(lhs.groundColor, rhs.groundColor) &&
         lhs.horizonPower == rhs.horizonPower &&
         lhs.showSunDisk == rhs.showSunDisk &&
         lhs.sunDiskIntensity == rhs.sunDiskIntensity &&
         lhs.sunDiskSize == rhs.sunDiskSize &&
         lhs.ambientFromSky == rhs.ambientFromSky &&
         lhs.reflectionIntensity == rhs.reflectionIntensity &&
         lhs.showDebugTexture == rhs.showDebugTexture;
}

bool EqualAtmosphereSettings(const AtmosphereSettings &lhs,
                             const AtmosphereSettings &rhs) {
  return lhs.enabled == rhs.enabled &&
         lhs.planetRadius == rhs.planetRadius &&
         lhs.atmosphereHeight == rhs.atmosphereHeight &&
         lhs.rayleighScaleHeight == rhs.rayleighScaleHeight &&
         lhs.mieScaleHeight == rhs.mieScaleHeight &&
         EqualVec3(lhs.rayleighScattering, rhs.rayleighScattering) &&
         EqualVec3(lhs.mieScattering, rhs.mieScattering) &&
         EqualVec3(lhs.ozoneAbsorption, rhs.ozoneAbsorption) &&
         lhs.ozoneCenterHeight == rhs.ozoneCenterHeight &&
         lhs.ozoneWidth == rhs.ozoneWidth &&
         lhs.mieAnisotropy == rhs.mieAnisotropy &&
         lhs.sunIlluminance == rhs.sunIlluminance &&
         lhs.sunExposure == rhs.sunExposure &&
         lhs.groundAlbedo == rhs.groundAlbedo &&
         lhs.aerialPerspectiveStrength == rhs.aerialPerspectiveStrength &&
         lhs.sunAngularRadius == rhs.sunAngularRadius &&
         lhs.transmittanceLutWidth == rhs.transmittanceLutWidth &&
         lhs.transmittanceLutHeight == rhs.transmittanceLutHeight &&
         lhs.multiScatteringLutSize == rhs.multiScatteringLutSize &&
         lhs.skyViewLutWidth == rhs.skyViewLutWidth &&
         lhs.skyViewLutHeight == rhs.skyViewLutHeight &&
         lhs.preferLut == rhs.preferLut;
}

bool EqualSunGlareSettings(const SunGlareSettings &lhs,
                           const SunGlareSettings &rhs) {
  return lhs.enabled == rhs.enabled &&
         lhs.intensity == rhs.intensity &&
         lhs.haloRadius == rhs.haloRadius &&
         lhs.haloFalloff == rhs.haloFalloff &&
         lhs.streakIntensity == rhs.streakIntensity &&
         lhs.streakLength == rhs.streakLength &&
         lhs.occlusionRadius == rhs.occlusionRadius &&
         lhs.edgeFade == rhs.edgeFade &&
         lhs.maxRadiance == rhs.maxRadiance &&
         lhs.shaftIntensity == rhs.shaftIntensity &&
         lhs.shaftLength == rhs.shaftLength &&
         lhs.shaftDecay == rhs.shaftDecay;
}

bool EqualVec2(const MATH::Vec2 &lhs, const MATH::Vec2 &rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

bool EqualStylizedCloudSettings(const StylizedCloudSettings &lhs,
                                const StylizedCloudSettings &rhs) {
  return lhs.enabled == rhs.enabled &&
         lhs.coverage == rhs.coverage && lhs.density == rhs.density &&
         lhs.altitude == rhs.altitude && lhs.thickness == rhs.thickness &&
         lhs.scale == rhs.scale &&
         EqualVec2(lhs.windDirection, rhs.windDirection) &&
         lhs.windSpeed == rhs.windSpeed &&
         lhs.shapeDetail == rhs.shapeDetail &&
         lhs.softness == rhs.softness &&
         lhs.lightAbsorption == rhs.lightAbsorption &&
         lhs.silverLining == rhs.silverLining &&
         lhs.horizonFade == rhs.horizonFade &&
         EqualVec3(lhs.brightColor, rhs.brightColor) &&
         EqualVec3(lhs.shadowColor, rhs.shadowColor);
}

bool EqualReflectionProbeSettings(const ReflectionProbeSettings &lhs,
                                  const ReflectionProbeSettings &rhs) {
  return lhs.enabled == rhs.enabled &&
         lhs.sourceCubemapAsset == rhs.sourceCubemapAsset &&
         EqualVec3(lhs.position, rhs.position) && lhs.radius == rhs.radius &&
         lhs.intensity == rhs.intensity &&
         lhs.influenceShape == rhs.influenceShape &&
         EqualVec3(lhs.influenceBoxCenter, rhs.influenceBoxCenter) &&
         EqualVec3(lhs.influenceBoxSize, rhs.influenceBoxSize) &&
         lhs.projectionShape == rhs.projectionShape &&
         EqualVec3(lhs.projectionBoxCenter, rhs.projectionBoxCenter) &&
         EqualVec3(lhs.projectionBoxSize, rhs.projectionBoxSize) &&
         lhs.blendDistance == rhs.blendDistance && lhs.priority == rhs.priority;
}

bool EqualAmbientOcclusionSettings(const AmbientOcclusionSettings &lhs,
                                   const AmbientOcclusionSettings &rhs,
                                   bool includeEditorViewportState) {
  return lhs.enabled == rhs.enabled && lhs.mode == rhs.mode &&
         lhs.radius == rhs.radius && lhs.bias == rhs.bias &&
         lhs.strength == rhs.strength && lhs.power == rhs.power &&
         lhs.diffuseStrength == rhs.diffuseStrength &&
         lhs.specularStrength == rhs.specularStrength &&
         lhs.sampleCount == rhs.sampleCount &&
         lhs.blurIterations == rhs.blurIterations &&
         (!includeEditorViewportState ||
          lhs.editorViewportSuppressed == rhs.editorViewportSuppressed);
}

bool EqualScreenSpaceReflectionSettings(
    const ScreenSpaceReflectionSettings &lhs,
    const ScreenSpaceReflectionSettings &rhs) {
  return lhs.enabled == rhs.enabled && lhs.quality == rhs.quality &&
         lhs.intensity == rhs.intensity &&
         lhs.maxDistance == rhs.maxDistance &&
         lhs.thickness == rhs.thickness &&
         lhs.maxRoughness == rhs.maxRoughness &&
         lhs.edgeFade == rhs.edgeFade && lhs.stride == rhs.stride;
}

bool EqualDirectionalShadowSettings(const DirectionalShadowSettings &lhs,
                                    const DirectionalShadowSettings &rhs) {
  return lhs.enabled == rhs.enabled && lhs.resolution == rhs.resolution &&
         lhs.cascadeCount == rhs.cascadeCount &&
         lhs.cascadeSplitLambda == rhs.cascadeSplitLambda &&
         lhs.cascadeBlend == rhs.cascadeBlend &&
         lhs.nearPlane == rhs.nearPlane &&
         lhs.farPlane == rhs.farPlane && lhs.depthBias == rhs.depthBias &&
         lhs.normalBias == rhs.normalBias && lhs.strength == rhs.strength &&
         lhs.pcfEnabled == rhs.pcfEnabled && lhs.pcfRadius == rhs.pcfRadius &&
         lhs.stabilize == rhs.stabilize &&
         lhs.showDebugTexture == rhs.showDebugTexture &&
         lhs.shadowDistance == rhs.shadowDistance &&
         lhs.showDebugFrustum == rhs.showDebugFrustum;
}

bool EqualPointLight(const PointLight &lhs, const PointLight &rhs) {
  return lhs.enabled == rhs.enabled && EqualVec3(lhs.position, rhs.position) &&
         lhs.range == rhs.range && EqualVec3(lhs.color, rhs.color) &&
         lhs.intensity == rhs.intensity && lhs.priority == rhs.priority;
}

bool EqualSpotLight(const SpotLight &lhs, const SpotLight &rhs) {
  return lhs.enabled == rhs.enabled && EqualVec3(lhs.position, rhs.position) &&
         lhs.range == rhs.range && EqualVec3(lhs.direction, rhs.direction) &&
         lhs.innerConeAngleDegrees == rhs.innerConeAngleDegrees &&
         lhs.outerConeAngleDegrees == rhs.outerConeAngleDegrees &&
         lhs.angularFalloff == rhs.angularFalloff &&
         EqualVec3(lhs.color, rhs.color) &&
         lhs.intensity == rhs.intensity && lhs.priority == rhs.priority;
}

bool EqualPostSettings(const ScenePostSettings &lhs,
                       const ScenePostSettings &rhs) {
  if (lhs.enabled != rhs.enabled ||
      lhs.globalPostProfileId != rhs.globalPostProfileId ||
      lhs.valuesInitialized != rhs.valuesInitialized) {
    return false;
  }
  for (int i = 0; i < 16; ++i) {
    if (!EqualFloat4(lhs.paramValues[i], rhs.paramValues[i])) {
      return false;
    }
  }
  return true;
}

} // namespace

bool AreSceneEnvironmentValuesEqual(const SceneEnvironment &lhs,
                                    const SceneEnvironment &rhs) {
  if (!EqualVec3(lhs.ambient.color, rhs.ambient.color) ||
      lhs.ambient.intensity != rhs.ambient.intensity ||
      lhs.ambient.useSkyColor != rhs.ambient.useSkyColor ||
      lhs.ambient.skyBlend != rhs.ambient.skyBlend ||
      lhs.directional.enabled != rhs.directional.enabled ||
      !EqualVec3(lhs.directional.direction, rhs.directional.direction) ||
      lhs.directional.intensity != rhs.directional.intensity ||
      !EqualVec3(lhs.directional.color, rhs.directional.color) ||
      !EqualDirectionalShadowSettings(lhs.directionalShadow,
                                      rhs.directionalShadow) ||
      !EqualSkySettings(lhs.sky, rhs.sky) ||
      !EqualAtmosphereSettings(lhs.atmosphere, rhs.atmosphere) ||
      !EqualSunGlareSettings(lhs.sunGlare, rhs.sunGlare) ||
      !EqualStylizedCloudSettings(lhs.clouds, rhs.clouds) ||
      !EqualReflectionProbeSettings(lhs.reflectionProbe, rhs.reflectionProbe) ||
      !EqualAmbientOcclusionSettings(lhs.ambientOcclusion, rhs.ambientOcclusion,
                                    false) ||
      lhs.surfaceLightmaps.enabled !=
          rhs.surfaceLightmaps.enabled ||
      !EqualScreenSpaceReflectionSettings(
          lhs.screenSpaceReflections, rhs.screenSpaceReflections) ||
      lhs.bloom.enabled != rhs.bloom.enabled ||
      lhs.bloom.threshold != rhs.bloom.threshold ||
      lhs.bloom.intensity != rhs.bloom.intensity ||
      lhs.bloom.radius != rhs.bloom.radius ||
      lhs.bloom.downsampleCount != rhs.bloom.downsampleCount ||
      lhs.fog.enabled != rhs.fog.enabled ||
      lhs.fog.volumetric != rhs.fog.volumetric ||
      !EqualVec3(lhs.fog.color, rhs.fog.color) ||
      lhs.fog.density != rhs.fog.density ||
      lhs.fog.startDistance != rhs.fog.startDistance ||
      lhs.fog.endDistance != rhs.fog.endDistance ||
      lhs.fog.heightFalloff != rhs.fog.heightFalloff ||
      lhs.fog.baseHeight != rhs.fog.baseHeight ||
      lhs.fog.anisotropy != rhs.fog.anisotropy ||
      lhs.fog.temporalWeight != rhs.fog.temporalWeight ||
      lhs.fog.scatteringIntensity != rhs.fog.scatteringIntensity ||
      lhs.fog.ambientScatteringStrength !=
          rhs.fog.ambientScatteringStrength ||
      lhs.fog.sunShaftStrength != rhs.fog.sunShaftStrength ||
      lhs.fog.useSkyHorizonColor != rhs.fog.useSkyHorizonColor ||
      lhs.toneMapping.enabled != rhs.toneMapping.enabled ||
      lhs.toneMapping.exposureMode != rhs.toneMapping.exposureMode ||
      lhs.toneMapping.exposure != rhs.toneMapping.exposure ||
      lhs.toneMapping.automaticExposure.targetLuminance !=
          rhs.toneMapping.automaticExposure.targetLuminance ||
      lhs.toneMapping.automaticExposure.compensationEv !=
          rhs.toneMapping.automaticExposure.compensationEv ||
      lhs.toneMapping.automaticExposure.minExposureEv !=
          rhs.toneMapping.automaticExposure.minExposureEv ||
      lhs.toneMapping.automaticExposure.maxExposureEv !=
          rhs.toneMapping.automaticExposure.maxExposureEv ||
      lhs.toneMapping.automaticExposure.speedUp !=
          rhs.toneMapping.automaticExposure.speedUp ||
      lhs.toneMapping.automaticExposure.speedDown !=
          rhs.toneMapping.automaticExposure.speedDown ||
      lhs.toneMapping.whiteBalanceTemperature !=
          rhs.toneMapping.whiteBalanceTemperature ||
      lhs.toneMapping.whiteBalanceTint !=
          rhs.toneMapping.whiteBalanceTint ||
      lhs.toneMapping.contrast != rhs.toneMapping.contrast ||
      lhs.toneMapping.saturation != rhs.toneMapping.saturation ||
      lhs.toneMapping.gamma != rhs.toneMapping.gamma ||
      lhs.toneMapping.mode != rhs.toneMapping.mode ||
      lhs.specularIntensity != rhs.specularIntensity ||
      lhs.specularPower != rhs.specularPower ||
      lhs.showLightDebug != rhs.showLightDebug ||
      lhs.showSkyDebugInfo != rhs.showSkyDebugInfo ||
      !EqualPostSettings(lhs.post, rhs.post) ||
      lhs.pointLights.size() != rhs.pointLights.size() ||
      lhs.spotLights.size() != rhs.spotLights.size()) {
    return false;
  }

  for (size_t i = 0; i < lhs.pointLights.size(); ++i) {
    if (!EqualPointLight(lhs.pointLights[i], rhs.pointLights[i])) {
      return false;
    }
  }
  for (size_t i = 0; i < lhs.spotLights.size(); ++i) {
    if (!EqualSpotLight(lhs.spotLights[i], rhs.spotLights[i])) {
      return false;
    }
  }
  return true;
}

bool AreQualityEnvironmentValuesEqual(const SceneEnvironment &lhs,
                                      const SceneEnvironment &rhs) {
  return EqualDirectionalShadowSettings(lhs.directionalShadow,
                                        rhs.directionalShadow) &&
         EqualAmbientOcclusionSettings(lhs.ambientOcclusion,
                                       rhs.ambientOcclusion, true) &&
         lhs.surfaceLightmaps.enabled ==
             rhs.surfaceLightmaps.enabled &&
         EqualScreenSpaceReflectionSettings(
             lhs.screenSpaceReflections, rhs.screenSpaceReflections) &&
         lhs.bloom.enabled == rhs.bloom.enabled &&
         lhs.bloom.threshold == rhs.bloom.threshold &&
         lhs.bloom.intensity == rhs.bloom.intensity &&
         lhs.bloom.radius == rhs.bloom.radius &&
         lhs.bloom.downsampleCount == rhs.bloom.downsampleCount &&
         lhs.toneMapping.enabled == rhs.toneMapping.enabled &&
         lhs.toneMapping.exposureMode == rhs.toneMapping.exposureMode &&
         lhs.toneMapping.exposure == rhs.toneMapping.exposure &&
         lhs.toneMapping.automaticExposure.targetLuminance ==
             rhs.toneMapping.automaticExposure.targetLuminance &&
         lhs.toneMapping.automaticExposure.compensationEv ==
             rhs.toneMapping.automaticExposure.compensationEv &&
         lhs.toneMapping.automaticExposure.minExposureEv ==
             rhs.toneMapping.automaticExposure.minExposureEv &&
         lhs.toneMapping.automaticExposure.maxExposureEv ==
             rhs.toneMapping.automaticExposure.maxExposureEv &&
         lhs.toneMapping.automaticExposure.speedUp ==
             rhs.toneMapping.automaticExposure.speedUp &&
         lhs.toneMapping.automaticExposure.speedDown ==
             rhs.toneMapping.automaticExposure.speedDown &&
         lhs.toneMapping.whiteBalanceTemperature ==
             rhs.toneMapping.whiteBalanceTemperature &&
         lhs.toneMapping.whiteBalanceTint ==
             rhs.toneMapping.whiteBalanceTint &&
         lhs.toneMapping.contrast == rhs.toneMapping.contrast &&
         lhs.toneMapping.saturation == rhs.toneMapping.saturation &&
         lhs.toneMapping.gamma == rhs.toneMapping.gamma &&
         lhs.toneMapping.mode == rhs.toneMapping.mode &&
         EqualPostSettings(lhs.post, rhs.post);
}

int ToSsaoModeIndex(const AmbientOcclusionSettings &settings) {
  if (!settings.enabled || settings.mode == SsaoMode::Off) {
    return 0;
  }
  switch (settings.mode) {
  case SsaoMode::Reference:
    return 1;
  case SsaoMode::OptimizedHigh:
    return 2;
  case SsaoMode::Balanced:
    return 3;
  case SsaoMode::Off:
  default:
    return 0;
  }
}

SsaoMode SsaoModeFromIndex(int index) {
  switch (index) {
  case 1:
    return SsaoMode::Reference;
  case 2:
    return SsaoMode::OptimizedHigh;
  case 3:
    return SsaoMode::Balanced;
  case 0:
  default:
    return SsaoMode::Off;
  }
}

} // namespace HIKARI::EDITOR::ENVIRONMENT_EDIT
