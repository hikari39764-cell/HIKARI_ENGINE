#include "Scene/Water/HIKARI_WaterSurfaceTypes.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Math/HIKARI_ScalarMath.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::WATER {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kGravity = 9.81f;
constexpr float kTimeWrapSeconds = 4096.0f;
constexpr float kMaximumHorizontalSlope = 0.92f;
constexpr float kMinimumShallowBlendDepthDelta = 1.0e-3f;
constexpr float kMaximumShallowDepth = 1000.0f;

float WrapDegrees(float degrees) noexcept {
    float wrapped = std::fmod(MATH::FiniteOr(degrees, 0.0f) + 180.0f, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped - 180.0f;
}

MATH::Vec3 SanitizeColor(const MATH::Vec3& value, const MATH::Vec3& fallback, float minimum, float maximum) noexcept {
    return {MATH::ClampFinite(value.x, fallback.x, minimum, maximum),
            MATH::ClampFinite(value.y, fallback.y, minimum, maximum),
            MATH::ClampFinite(value.z, fallback.z, minimum, maximum)};
}

float WrappedTime(float timeSeconds) noexcept {
    if (!std::isfinite(timeSeconds)) {
        return 0.0f;
    }
    return std::fmod(timeSeconds, kTimeWrapSeconds);
}

float ResolveHorizontalSlopeScale(const WaterSurfaceSettings& settings) noexcept {
    float totalMaximumSlope = 0.0f;
    for (const WaterGerstnerWave& wave : settings.waves) {
        if (wave.amplitude <= 1.0e-7f || wave.wavelength <= 1.0e-7f) {
            continue;
        }
        const float waveNumber = kTwoPi / wave.wavelength;
        totalMaximumSlope += settings.choppiness * wave.steepness * wave.amplitude * waveNumber;
    }
    if (!std::isfinite(totalMaximumSlope) || totalMaximumSlope <= kMaximumHorizontalSlope) {
        return std::isfinite(totalMaximumSlope) ? 1.0f : 0.0f;
    }
    return kMaximumHorizontalSlope / totalMaximumSlope;
}
} // namespace

WaterSurfaceSettings MakeWaterSurfacePresetSettings(WaterSurfacePreset preset) noexcept {
    WaterSurfaceSettings settings{};
    if (preset == WaterSurfacePreset::RealisticClearSummer) {
        settings.preset = preset;
        settings.timeScale = 0.92f;
        settings.choppiness = 0.46f;
        settings.microWaveStrength = 0.52f;
        settings.depthTransmittance = {0.10f, 0.52f, 0.72f};
        settings.scatteringColor = {0.015f, 0.24f, 0.30f};
        settings.shallowClearDepth = 0.20f;
        settings.shallowBlendDepth = 2.80f;
        settings.shallowGraphicWeight = 0.14f;
        settings.foamColor = {0.96f, 0.99f, 1.0f};
        settings.foamAmount = 0.30f;
        settings.absorptionDistance = 11.0f;
        // Keep the GGX lobe above the sub-pixel/TAA loss region. The
        // shader still applies derivative AA for authored values.
        settings.surfaceRoughness = 0.12f;
        settings.directSunStrength = 1.0f;
        settings.transmissionStrength = 1.0f;
        settings.shadowStrength = 0.82f;
        settings.reflectionStrength = 1.0f;
        settings.sunGlintStrength = 0.64f;
        settings.glintDensity = 0.46f;
        settings.glintFlowSpeed = 0.38f;
        settings.ribbonWidth = 0.08f;
        settings.windDirectionDeg = 28.0f;
        settings.windSpeed = 14.0f;
        settings.shortWaveStrength = 0.80f;
        settings.capillaryStrength = 0.48f;
        settings.waves = {{
            {12.0f, 0.30f, 15.0f, 0.66f},
            {61.0f, 0.16f, 8.3f, 0.55f},
            {-34.0f, 0.075f, 4.6f, 0.40f},
            {126.0f, 0.035f, 2.7f, 0.26f},
        }};
        return settings;
    }

    settings.preset = WaterSurfacePreset::AnimeSummer;
    settings.directSunStrength = 0.82f;
    settings.transmissionStrength = 0.92f;
    settings.shadowStrength = 0.72f;
    settings.reflectionStrength = 1.0f;
    settings.sunGlintStrength = 0.52f;
    settings.glintDensity = 0.30f;
    settings.glintFlowSpeed = 0.62f;
    settings.ribbonWidth = 0.16f;
    return settings;
}

WaterSurfaceSettings SanitizeWaterSurfaceSettings(WaterSurfaceSettings settings) noexcept {
    if (settings.preset != WaterSurfacePreset::AnimeSummer &&
        settings.preset != WaterSurfacePreset::RealisticClearSummer) {
        settings.preset = WaterSurfacePreset::AnimeSummer;
    }
    if (settings.geometryMode != WaterSurfaceGeometryMode::FiniteAuthored &&
        settings.geometryMode != WaterSurfaceGeometryMode::CameraCenteredOcean) {
        settings.geometryMode = WaterSurfaceGeometryMode::FiniteAuthored;
    }
    const WaterSurfaceSettings fallback = MakeWaterSurfacePresetSettings(settings.preset);
    settings.timeScale = MATH::ClampFinite(settings.timeScale, fallback.timeScale, 0.0f, 4.0f);
    settings.choppiness = MATH::ClampFinite(settings.choppiness, fallback.choppiness, 0.0f, 1.0f);
    settings.microWaveStrength = MATH::ClampFinite(settings.microWaveStrength, fallback.microWaveStrength, 0.0f, 2.0f);
    settings.depthTransmittance = SanitizeColor(settings.depthTransmittance, fallback.depthTransmittance, 0.001f, 1.0f);
    settings.scatteringColor = SanitizeColor(settings.scatteringColor, fallback.scatteringColor, 0.0f, 8.0f);
    settings.shallowClearDepth = MATH::ClampFinite(settings.shallowClearDepth, fallback.shallowClearDepth, 0.0f,
                                                   kMaximumShallowDepth - kMinimumShallowBlendDepthDelta);
    settings.shallowBlendDepth =
        MATH::ClampFinite(settings.shallowBlendDepth, fallback.shallowBlendDepth,
                          settings.shallowClearDepth + kMinimumShallowBlendDepthDelta, kMaximumShallowDepth);
    settings.shallowGraphicWeight =
        MATH::ClampFinite(settings.shallowGraphicWeight, fallback.shallowGraphicWeight, 0.0f, 1.0f);
    settings.foamColor = SanitizeColor(settings.foamColor, fallback.foamColor, 0.0f, 8.0f);
    settings.foamAmount = MATH::ClampFinite(settings.foamAmount, fallback.foamAmount, 0.0f, 2.0f);
    settings.absorptionDistance =
        MATH::ClampFinite(settings.absorptionDistance, fallback.absorptionDistance, 0.05f, 1000.0f);
    settings.surfaceRoughness = MATH::ClampFinite(settings.surfaceRoughness, fallback.surfaceRoughness, 0.02f, 1.0f);
    settings.directSunStrength = MATH::ClampFinite(settings.directSunStrength, fallback.directSunStrength, 0.0f, 2.0f);
    settings.transmissionStrength =
        MATH::ClampFinite(settings.transmissionStrength, fallback.transmissionStrength, 0.0f, 2.0f);
    settings.shadowStrength = MATH::ClampFinite(settings.shadowStrength, fallback.shadowStrength, 0.0f, 1.0f);
    settings.reflectionStrength =
        MATH::ClampFinite(settings.reflectionStrength, fallback.reflectionStrength, 0.0f, 2.0f);
    settings.sunGlintStrength = MATH::ClampFinite(settings.sunGlintStrength, fallback.sunGlintStrength, 0.0f, 2.0f);
    settings.glintDensity = MATH::ClampFinite(settings.glintDensity, fallback.glintDensity, 0.0f, 1.0f);
    settings.glintFlowSpeed = MATH::ClampFinite(settings.glintFlowSpeed, fallback.glintFlowSpeed, 0.0f, 4.0f);
    settings.ribbonWidth = MATH::ClampFinite(settings.ribbonWidth, fallback.ribbonWidth, 0.02f, 0.5f);
    settings.windDirectionDeg = WrapDegrees(MATH::FiniteOr(settings.windDirectionDeg, fallback.windDirectionDeg));
    settings.windSpeed = MATH::ClampFinite(settings.windSpeed, fallback.windSpeed, 0.0f, 50.0f);
    settings.shortWaveStrength = MATH::ClampFinite(settings.shortWaveStrength, fallback.shortWaveStrength, 0.0f, 2.0f);
    settings.capillaryStrength = MATH::ClampFinite(settings.capillaryStrength, fallback.capillaryStrength, 0.0f, 2.0f);

    for (std::size_t index = 0; index < settings.waves.size(); ++index) {
        WaterGerstnerWave& wave = settings.waves[index];
        const WaterGerstnerWave& fallbackWave = fallback.waves[index];
        wave.directionDeg = WrapDegrees(wave.directionDeg);
        wave.amplitude = MATH::ClampFinite(wave.amplitude, fallbackWave.amplitude, 0.0f, 25.0f);
        wave.wavelength = MATH::ClampFinite(wave.wavelength, fallbackWave.wavelength, 0.05f, 10000.0f);
        wave.steepness = MATH::ClampFinite(wave.steepness, fallbackWave.steepness, 0.0f, 1.0f);
    }
    return settings;
}

WaterSurfaceSettings ResolveWaterSurfaceSettingsForMesh(const WaterSurfaceSettings& rawSettings,
                                                        float maximumVertexSpacing) noexcept {
    WaterSurfaceSettings settings = SanitizeWaterSurfaceSettings(rawSettings);
    const float spacing = MATH::ClampFinite(maximumVertexSpacing, 0.0f, 0.0f, 100000.0f);
    if (spacing <= 1.0e-6f) {
        return settings;
    }

    const float fadeStart = 2.0f * spacing;
    const float fadeEnd = 4.0f * spacing;
    for (WaterGerstnerWave& wave : settings.waves) {
        const float resolved =
            MATH::SmoothStep01((wave.wavelength - fadeStart) / (std::max)(fadeEnd - fadeStart, 1.0e-6f));
        wave.amplitude *= resolved;
        wave.steepness *= resolved;
    }
    return settings;
}

WaterSurfaceSettings ResolveWaterSurfaceRuntimeSettings(const WaterSurfaceSettings& rawSettings,
                                                        float maximumVertexSpacing, const MATH::Mat4& world) noexcept {
    WaterSurfaceSettings settings = ResolveWaterSurfaceSettingsForMesh(rawSettings, maximumVertexSpacing);
    if (settings.geometryMode != WaterSurfaceGeometryMode::CameraCenteredOcean) {
        return settings;
    }

    const MATH::Vec3 basisX{world.m[0][0], world.m[0][1], world.m[0][2]};
    const MATH::Vec3 basisY{world.m[1][0], world.m[1][1], world.m[1][2]};
    const MATH::Vec3 basisZ{world.m[2][0], world.m[2][1], world.m[2][2]};
    const MATH::Vec3 planeNormal = MATH::Normalize(MATH::Cross(basisZ, basisX));
    const MATH::Vec3 authoredUp = MATH::Normalize(basisY);
    constexpr float kMinimumWorldUpAlignment = 0.9999f;
    if (MATH::Length(planeNormal) <= 1.0e-6f || MATH::Length(authoredUp) <= 1.0e-6f ||
        planeNormal.y < kMinimumWorldUpAlignment || authoredUp.y < kMinimumWorldUpAlignment) {
        settings.geometryMode = WaterSurfaceGeometryMode::FiniteAuthored;
    }
    return settings;
}

WaterSurfaceEvaluation EvaluateWaterSurface(const WaterSurfaceSettings& rawSettings, const MATH::Vec2& sourceLocalXZ,
                                            float timeSeconds) noexcept {
    const WaterSurfaceSettings settings = SanitizeWaterSurfaceSettings(rawSettings);
    WaterSurfaceEvaluation result{};
    result.positionLocal = {MATH::FiniteOr(sourceLocalXZ.x, 0.0f), 0.0f, MATH::FiniteOr(sourceLocalXZ.y, 0.0f)};
    MATH::Vec3 tangentX{1.0f, 0.0f, 0.0f};
    MATH::Vec3 tangentZ{0.0f, 0.0f, 1.0f};

    const float animationTime = WrappedTime(timeSeconds) * settings.timeScale;
    const float horizontalSlopeScale = ResolveHorizontalSlopeScale(settings);
    for (const WaterGerstnerWave& wave : settings.waves) {
        if (wave.amplitude <= 1.0e-7f || wave.wavelength <= 1.0e-7f) {
            continue;
        }

        const float directionRadians = wave.directionDeg * (kPi / 180.0f);
        const float directionX = std::cos(directionRadians);
        const float directionZ = std::sin(directionRadians);
        const float waveNumber = kTwoPi / wave.wavelength;
        const float angularFrequency = std::sqrt(kGravity * waveNumber);
        const float phase = waveNumber * (directionX * sourceLocalXZ.x + directionZ * sourceLocalXZ.y) -
                            angularFrequency * animationTime;
        const float sine = std::sin(phase);
        const float cosine = std::cos(phase);
        const float horizontalAmplitude = settings.choppiness * wave.steepness * wave.amplitude * horizontalSlopeScale;

        result.positionLocal.x += directionX * horizontalAmplitude * cosine;
        result.positionLocal.y += wave.amplitude * sine;
        result.positionLocal.z += directionZ * horizontalAmplitude * cosine;

        const float horizontalDerivative = horizontalAmplitude * waveNumber * sine;
        const float verticalDerivative = wave.amplitude * waveNumber * cosine;
        tangentX.x -= directionX * directionX * horizontalDerivative;
        tangentX.y += directionX * verticalDerivative;
        tangentX.z -= directionX * directionZ * horizontalDerivative;
        tangentZ.x -= directionX * directionZ * horizontalDerivative;
        tangentZ.y += directionZ * verticalDerivative;
        tangentZ.z -= directionZ * directionZ * horizontalDerivative;

        const float timeDerivative = angularFrequency * settings.timeScale;
        result.velocityLocal.x += directionX * horizontalAmplitude * timeDerivative * sine;
        result.velocityLocal.y -= wave.amplitude * timeDerivative * cosine;
        result.velocityLocal.z += directionZ * horizontalAmplitude * timeDerivative * sine;
    }

    result.normalLocal = MATH::Normalize(MATH::Cross(tangentZ, tangentX));
    if (MATH::Length(result.normalLocal) <= 1.0e-6f) {
        result.normalLocal = {0.0f, 1.0f, 0.0f};
    }
    result.horizontalJacobian = tangentX.x * tangentZ.z - tangentX.z * tangentZ.x;
    result.compression = std::clamp(1.0f - result.horizontalJacobian, 0.0f, 1.0f);
    return result;
}

WaterSurfaceEvaluation EvaluateWaterSurfaceAtHorizontalPosition(const WaterSurfaceSettings& settings,
                                                                const MATH::Vec2& deformedLocalXZ, float timeSeconds,
                                                                MATH::Vec2* outSourceLocalXZ) noexcept {
    MATH::Vec2 source{MATH::FiniteOr(deformedLocalXZ.x, 0.0f), MATH::FiniteOr(deformedLocalXZ.y, 0.0f)};
    WaterSurfaceEvaluation evaluation{};
    // Amplitude-coupled choppiness can legitimately move a tall authored
    // wave farther than the legacy wavelength-only approximation. Keep a
    // bounded fixed solve, but give it enough contractions to preserve
    // buoyancy/render parity for those larger yet valid waves.
    for (uint32_t iteration = 0u; iteration < 8u; ++iteration) {
        evaluation = EvaluateWaterSurface(settings, source, timeSeconds);
        source.x += deformedLocalXZ.x - evaluation.positionLocal.x;
        source.y += deformedLocalXZ.y - evaluation.positionLocal.z;
    }
    evaluation = EvaluateWaterSurface(settings, source, timeSeconds);
    if (outSourceLocalXZ != nullptr) {
        *outSourceLocalXZ = source;
    }
    return evaluation;
}

float CalculateWaterMaximumHorizontalDisplacement(const WaterSurfaceSettings& rawSettings) noexcept {
    const WaterSurfaceSettings settings = SanitizeWaterSurfaceSettings(rawSettings);
    const float horizontalSlopeScale = ResolveHorizontalSlopeScale(settings);
    float maximum = 0.0f;
    for (const WaterGerstnerWave& wave : settings.waves) {
        if (wave.amplitude <= 1.0e-7f) {
            continue;
        }
        maximum += settings.choppiness * wave.steepness * wave.amplitude * horizontalSlopeScale;
    }
    return maximum;
}

bool WaterSurfaceSettingsEqual(const WaterSurfaceSettings& lhs, const WaterSurfaceSettings& rhs) noexcept {
    if (lhs.preset != rhs.preset || lhs.geometryMode != rhs.geometryMode || lhs.timeScale != rhs.timeScale ||
        lhs.choppiness != rhs.choppiness || lhs.microWaveStrength != rhs.microWaveStrength ||
        lhs.depthTransmittance != rhs.depthTransmittance || lhs.scatteringColor != rhs.scatteringColor ||
        lhs.shallowClearDepth != rhs.shallowClearDepth || lhs.shallowBlendDepth != rhs.shallowBlendDepth ||
        lhs.shallowGraphicWeight != rhs.shallowGraphicWeight || lhs.foamColor != rhs.foamColor ||
        lhs.foamAmount != rhs.foamAmount || lhs.absorptionDistance != rhs.absorptionDistance ||
        lhs.surfaceRoughness != rhs.surfaceRoughness || lhs.directSunStrength != rhs.directSunStrength ||
        lhs.transmissionStrength != rhs.transmissionStrength || lhs.shadowStrength != rhs.shadowStrength ||
        lhs.reflectionStrength != rhs.reflectionStrength || lhs.sunGlintStrength != rhs.sunGlintStrength ||
        lhs.glintDensity != rhs.glintDensity || lhs.glintFlowSpeed != rhs.glintFlowSpeed ||
        lhs.ribbonWidth != rhs.ribbonWidth || lhs.windDirectionDeg != rhs.windDirectionDeg ||
        lhs.windSpeed != rhs.windSpeed || lhs.shortWaveStrength != rhs.shortWaveStrength ||
        lhs.capillaryStrength != rhs.capillaryStrength) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.waves.size(); ++index) {
        const WaterGerstnerWave& left = lhs.waves[index];
        const WaterGerstnerWave& right = rhs.waves[index];
        if (left.directionDeg != right.directionDeg || left.amplitude != right.amplitude ||
            left.wavelength != right.wavelength || left.steepness != right.steepness) {
            return false;
        }
    }
    return true;
}

} // namespace HIKARI::WATER
