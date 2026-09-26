#include "Scene/Terrain/HIKARI_TerrainSurfaceTypes.h"
#include "Core/Text/HIKARI_EnumNames.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>

namespace HIKARI::TERRAIN {

namespace {
float FiniteOr(float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; }

float ClampPositive(float value, float minimum, float maximum) noexcept {
    return std::clamp(FiniteOr(value, minimum), minimum, maximum);
}

} // namespace

TerrainSurfaceSettings SanitizeTerrainSurfaceSettings(TerrainSurfaceSettings settings) noexcept {
    settings.gridResolution =
        std::clamp(settings.gridResolution, kTerrainSurfaceMinGridResolution, kTerrainSurfaceMaxGridResolution);
    settings.heightScale = ClampPositive(settings.heightScale, 0.0f, 2.0f);
    settings.maxDepth = ClampPositive(settings.maxDepth, 0.01f, 2.0f);
    settings.stampRadius = ClampPositive(settings.stampRadius, 0.02f, 10.0f);
    settings.rimStrength = std::clamp(FiniteOr(settings.rimStrength, 0.32f), 0.0f, 2.0f);
    settings.responseResolutionScale = std::clamp(settings.responseResolutionScale, 1u, 4u);
    settings.restDepthScale = std::clamp(FiniteOr(settings.restDepthScale, 0.36f), 0.0f, 2.0f);
    settings.minimumDepth = ClampPositive(settings.minimumDepth, 0.0f, 2.0f);
    settings.settleStartRatio = std::clamp(FiniteOr(settings.settleStartRatio, 0.72f), 0.0f, 1.0f);
    settings.settleSeconds = ClampPositive(settings.settleSeconds, 0.0f, 10.0f);
    settings.impactDepthScale = ClampPositive(settings.impactDepthScale, 0.0f, 2.0f);
    settings.rimWidthScale = std::clamp(FiniteOr(settings.rimWidthScale, 0.42f), 0.05f, 2.0f);
    settings.refillPerSecond = ClampPositive(settings.refillPerSecond, 0.0f, 10.0f);
    settings.diffusionPerSecond = ClampPositive(settings.diffusionPerSecond, 0.0f, 100.0f);
    settings.plasticity = std::clamp(FiniteOr(settings.plasticity, 0.93f), 0.0f, 1.0f);
    settings.plasticRefillPerSecond = ClampPositive(settings.plasticRefillPerSecond, 0.0f, 10.0f);
    settings.recoveryDelaySeconds = ClampPositive(settings.recoveryDelaySeconds, 0.0f, 60.0f);
    settings.edgeFalloff = ClampPositive(settings.edgeFalloff, 0.0f, 1000.0f);
    settings.edgeDepth = std::clamp(FiniteOr(settings.edgeDepth, 1.5f), 0.0f, 100.0f);

    settings.lightTint = {
        std::clamp(FiniteOr(settings.lightTint.x, 1.0f), 0.0f, 1.0f),
        std::clamp(FiniteOr(settings.lightTint.y, 1.0f), 0.0f, 1.0f),
        std::clamp(FiniteOr(settings.lightTint.z, 1.0f), 0.0f, 1.0f),
    };
    settings.darkTint = {
        std::clamp(FiniteOr(settings.darkTint.x, 0.0f), 0.0f, 1.0f),
        std::clamp(FiniteOr(settings.darkTint.y, 0.0f), 0.0f, 1.0f),
        std::clamp(FiniteOr(settings.darkTint.z, 0.0f), 0.0f, 1.0f),
    };
    settings.sparkleStrength = ClampPositive(settings.sparkleStrength, 0.0f, 2.0f);
    settings.rippleAmount = ClampPositive(settings.rippleAmount, 0.0f, 3.0f);
    settings.pitDarkening = std::clamp(FiniteOr(settings.pitDarkening, 0.32f), 0.0f, 1.0f);
    settings.toonBandCount = std::clamp(settings.toonBandCount, 2u, 4u);
    settings.toonBandHardness = std::clamp(FiniteOr(settings.toonBandHardness, 0.14f), 0.0f, 0.5f);
    return settings;
}

bool TerrainSurfaceSettingsEqual(const TerrainSurfaceSettings& lhs, const TerrainSurfaceSettings& rhs) noexcept {
    return lhs.presetId == rhs.presetId && lhs.deformationEnabled == rhs.deformationEnabled &&
           lhs.gridResolution == rhs.gridResolution && lhs.heightScale == rhs.heightScale &&
           lhs.maxDepth == rhs.maxDepth && lhs.stampRadius == rhs.stampRadius && lhs.rimStrength == rhs.rimStrength &&
           lhs.responseResolutionScale == rhs.responseResolutionScale && lhs.restDepthScale == rhs.restDepthScale &&
           lhs.minimumDepth == rhs.minimumDepth && lhs.settleStartRatio == rhs.settleStartRatio &&
           lhs.settleSeconds == rhs.settleSeconds && lhs.impactDepthScale == rhs.impactDepthScale &&
           lhs.rimWidthScale == rhs.rimWidthScale && lhs.refillPerSecond == rhs.refillPerSecond &&
           lhs.diffusionPerSecond == rhs.diffusionPerSecond && lhs.plasticity == rhs.plasticity &&
           lhs.plasticRefillPerSecond == rhs.plasticRefillPerSecond &&
           lhs.recoveryDelaySeconds == rhs.recoveryDelaySeconds && lhs.edgeFalloff == rhs.edgeFalloff &&
           lhs.edgeDepth == rhs.edgeDepth && lhs.lightTint.x == rhs.lightTint.x && lhs.lightTint.y == rhs.lightTint.y &&
           lhs.lightTint.z == rhs.lightTint.z && lhs.darkTint.x == rhs.darkTint.x && lhs.darkTint.y == rhs.darkTint.y &&
           lhs.darkTint.z == rhs.darkTint.z && lhs.sparkleStrength == rhs.sparkleStrength &&
           lhs.rippleAmount == rhs.rippleAmount && lhs.pitDarkening == rhs.pitDarkening &&
           lhs.toonBandCount == rhs.toonBandCount && lhs.toonBandHardness == rhs.toonBandHardness;
}

float EvaluateTerrainSurfaceEffectiveHeight(float baseHeight, float coverage, float edgeDepth,
                                            float fallbackDepth) noexcept {
    const float edge = std::clamp(FiniteOr(coverage, 0.0f), 0.0f, 1.0f);
    const float depth = edgeDepth > 1.0e-4f ? edgeDepth : (std::max)(fallbackDepth, 0.0f);
    const float smooth = edge * edge * (3.0f - 2.0f * edge);
    return baseHeight - depth * (1.0f - smooth);
}

} // namespace HIKARI::TERRAIN
