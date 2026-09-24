#include "Scene/Components/Rendering/Vfx/HIKARI_GhostPossessionComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace HIKARI {
namespace {
float SanitizeFinite(float value, float fallback, float minimum, float maximum) noexcept {
    if (!std::isfinite(value)) {
        value = fallback;
    }
    return std::clamp(value, minimum, maximum);
}

float SmoothStep01(float value) noexcept {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}
} // namespace

GhostPossessionSettings SanitizeGhostPossessionSettings(GhostPossessionSettings settings) noexcept {
    const GhostPossessionSettings defaults{};
    settings.intensity = SanitizeFinite(settings.intensity, defaults.intensity, 0.0f, 2.0f);
    settings.blendInSeconds = SanitizeFinite(settings.blendInSeconds, defaults.blendInSeconds, 0.0f, 30.0f);
    settings.blendOutSeconds = SanitizeFinite(settings.blendOutSeconds, defaults.blendOutSeconds, 0.0f, 30.0f);
    settings.rimStrength = SanitizeFinite(settings.rimStrength, defaults.rimStrength, 0.0f, 16.0f);
    settings.rimPower = SanitizeFinite(settings.rimPower, defaults.rimPower, 0.01f, 32.0f);
    settings.dissolveAmount = SanitizeFinite(settings.dissolveAmount, defaults.dissolveAmount, 0.0f, 0.95f);
    settings.edgeWidth = SanitizeFinite(settings.edgeWidth, defaults.edgeWidth, 0.001f, 0.5f);
    settings.pulseSpeed = SanitizeFinite(settings.pulseSpeed, defaults.pulseSpeed, 0.0f, 30.0f);
    settings.edgeBoost = SanitizeFinite(settings.edgeBoost, defaults.edgeBoost, 0.0f, 16.0f);
    settings.noiseScale = SanitizeFinite(settings.noiseScale, defaults.noiseScale, 0.01f, 50.0f);
    settings.flowSpeed = SanitizeFinite(settings.flowSpeed, defaults.flowSpeed, 0.0f, 12.0f);
    settings.flowStrength = SanitizeFinite(settings.flowStrength, defaults.flowStrength, 0.0f, 2.0f);
    settings.distortionStrength = SanitizeFinite(settings.distortionStrength, defaults.distortionStrength, 0.0f, 0.1f);
    settings.sceneColorMix = SanitizeFinite(settings.sceneColorMix, defaults.sceneColorMix, 0.0f, 1.0f);
    settings.sceneColorNoiseScale =
        SanitizeFinite(settings.sceneColorNoiseScale, defaults.sceneColorNoiseScale, 0.01f, 50.0f);
    return settings;
}

void ResolveGhostPossessionMaterialValues(const GhostPossessionSettings& sourceSettings, float blendWeight,
                                          DirectX::XMFLOAT4 (&outValues)[VFX::kMaterialFxUserCount]) noexcept {
    const GhostPossessionSettings settings = SanitizeGhostPossessionSettings(sourceSettings);
    for (DirectX::XMFLOAT4& value : outValues) {
        value = {};
    }

    const float transition = SmoothStep01(blendWeight);
    const float weightedIntensity = transition * settings.intensity;
    const float dissolveIntensity = std::clamp(settings.intensity, 0.0f, 1.0f);
    const float settledDissolve = settings.dissolveAmount * dissolveIntensity;
    constexpr float kTransitionDissolve = 0.92f;

    outValues[0] = {settings.rimStrength * weightedIntensity, settings.rimPower,
                    std::lerp(kTransitionDissolve, settledDissolve, transition), settings.edgeWidth};
    outValues[1] = {settings.pulseSpeed, settings.edgeBoost * weightedIntensity, settings.noiseScale,
                    settings.flowSpeed};
    outValues[2] = {settings.distortionStrength * weightedIntensity,
                    settings.sceneColorMix * transition * std::clamp(settings.intensity, 0.0f, 1.0f),
                    settings.sceneColorNoiseScale, settings.flowStrength * weightedIntensity};
}

const GhostPossessionSettings& GhostPossessionComponent::GetSettings() const noexcept { return settings_; }

void GhostPossessionComponent::SetSettings(GhostPossessionSettings settings) noexcept {
    settings_ = SanitizeGhostPossessionSettings(std::move(settings));
}

bool GhostPossessionComponent::IsEnabled() const noexcept { return settings_.enabled; }

void GhostPossessionComponent::SetEnabled(bool enabled) noexcept { settings_.enabled = enabled; }

} // namespace HIKARI
