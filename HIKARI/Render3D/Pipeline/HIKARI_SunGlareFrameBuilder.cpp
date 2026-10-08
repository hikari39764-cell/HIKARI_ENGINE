#include "Render3D/Pipeline/HIKARI_SunGlareFrameBuilder.h"

#include <algorithm>
#include <cmath>
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/HIKARI_VolumetricCloudStage.h"
#include "Vfx/Post/HIKARI_PostProcessingTypes.h"

namespace HIKARI::RENDER3D::PIPELINE {

POST::SunGlareFrame BuildSunGlareFrame(const Camera3D& camera, const SceneEnvironment& environment) {
    POST::SunGlareFrame frame{};
    const SKYRENDERER::AtmosphereFrameData sun = SKYRENDERER::BuildAtmosphereFrameData(environment, camera);
    const SunGlareSettings& settings = sun.sunGlare;
    if (!sun.valid || !sun.screenSunVisible || !settings.enabled || sun.sunRadiance <= 0.0f) {
        return frame;
    }
    const float sunUvX = sun.screenSunPosition.x;
    const float sunUvY = sun.screenSunPosition.y;
    const float maximumExtent =
        (std::max)(settings.haloRadius, (std::max)(settings.streakLength, settings.shaftLength));
    if (!std::isfinite(sunUvX) || !std::isfinite(sunUvY) || sunUvX < -maximumExtent || sunUvX > 1.0f + maximumExtent ||
        sunUvY < -maximumExtent || sunUvY > 1.0f + maximumExtent) {
        return frame;
    }

    const float angularRadius = std::clamp(sun.sunAngularRadius, 0.0005f, 0.035f);
    const float halfFov = (std::max)(camera.GetFovYRad() * 0.5f, 0.01f);
    const float coreRadius = std::clamp(0.5f * std::tan(angularRadius) / std::tan(halfFov), 0.001f, 0.04f);

    frame.enabled = true;
    frame.sunUvX = sunUvX;
    frame.sunUvY = sunUvY;
    frame.coreRadius = coreRadius;
    frame.colorR = sun.sunColor.x;
    frame.colorG = sun.sunColor.y;
    frame.colorB = sun.sunColor.z;
    // 光芒はレンズの演出なので表示参照の明るさで持つ。大気の透過と地平線への沈み、雲に隠れる割合（天気からの
    // 概算）で弱める。
    const float visibility =
        std::clamp(sun.sunRadiance / (std::max)(environment.sun.illuminance, 1.0e-3f), 0.0f, 1.0f) *
        CLOUDS::EstimateCloudShading(environment).sunVisibility;
    frame.intensity = settings.intensity * visibility;
    frame.haloRadius = settings.haloRadius;
    frame.haloFalloff = settings.haloFalloff;
    frame.streakIntensity = settings.streakIntensity;
    frame.streakLength = settings.streakLength;
    frame.occlusionRadius = settings.occlusionRadius;
    frame.edgeFade = settings.edgeFade;
    frame.maxRadiance = settings.maxRadiance;
    frame.shaftIntensity = settings.shaftIntensity;
    frame.shaftLength = settings.shaftLength;
    frame.shaftDecay = settings.shaftDecay;
    return frame;
}

} // namespace HIKARI::RENDER3D::PIPELINE
