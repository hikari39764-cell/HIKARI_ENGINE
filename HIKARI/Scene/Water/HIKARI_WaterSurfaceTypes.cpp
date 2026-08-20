#include "Scene/Water/HIKARI_WaterSurfaceTypes.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::WATER {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTwoPi = 6.28318530717958647692f;
        constexpr float kGravity = 9.81f;
        constexpr float kTimeWrapSeconds = 4096.0f;
        constexpr float kMinimumShallowBlendDepthDelta = 1.0e-3f;
        constexpr float kMaximumShallowDepth = 1000.0f;

        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        float ClampFinite(
            float value,
            float fallback,
            float minimum,
            float maximum) noexcept {

            return std::clamp(
                FiniteOr(value, fallback),
                minimum,
                maximum);
        }

        float WrapDegrees(float degrees) noexcept {
            float wrapped = std::fmod(
                FiniteOr(degrees, 0.0f) + 180.0f,
                360.0f);
            if (wrapped < 0.0f) {
                wrapped += 360.0f;
            }
            return wrapped - 180.0f;
        }

        MATH::Vec3 SanitizeColor(
            const MATH::Vec3& value,
            const MATH::Vec3& fallback,
            float minimum,
            float maximum) noexcept {

            return {
                ClampFinite(value.x, fallback.x, minimum, maximum),
                ClampFinite(value.y, fallback.y, minimum, maximum),
                ClampFinite(value.z, fallback.z, minimum, maximum)
            };
        }

        float WrappedTime(float timeSeconds) noexcept {
            if (!std::isfinite(timeSeconds)) {
                return 0.0f;
            }
            return std::fmod(timeSeconds, kTimeWrapSeconds);
        }

        float SmoothStep01(float value) noexcept {
            const float t = std::clamp(value, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    }

    const char* ToString(WaterSurfacePreset preset) noexcept {
        switch (preset) {
        case WaterSurfacePreset::RealisticClearSummer:
            return "realistic_clear_summer";
        case WaterSurfacePreset::AnimeSummer:
        default:
            return "anime_summer";
        }
    }

    const char* ToString(WaterSurfaceGeometryMode mode) noexcept {
        switch (mode) {
        case WaterSurfaceGeometryMode::CameraCenteredOcean:
            return "camera_centered_ocean";
        case WaterSurfaceGeometryMode::FiniteAuthored:
        default:
            return "finite_authored";
        }
    }

    WaterSurfacePreset ParseWaterSurfacePreset(
        std::string_view value,
        WaterSurfacePreset fallback) noexcept {

        if (value == "anime_summer" || value == "AnimeSummer" ||
            value == "stylized" || value == "Stylized") {
            return WaterSurfacePreset::AnimeSummer;
        }
        if (value == "realistic_clear_summer" ||
            value == "RealisticClearSummer" || value == "realistic" ||
            value == "Realistic") {
            return WaterSurfacePreset::RealisticClearSummer;
        }
        // v5 compatibility: the removed sparkling preset is now the single
        // AnimeSummer product. Keep old display/name tokens readable without
        // exposing a third enum value to authoring or runtime code.
        if (value == "anime_sparkling_summer" ||
            value == "AnimeSparklingSummer" ||
            value == "sparkling" || value == "Sparkling") {
            return WaterSurfacePreset::AnimeSummer;
        }
        // v5 compatibility: the removed spectral preset is now the single
        // RealisticClearSummer product.
        if (value == "realistic_spectral_summer" ||
            value == "RealisticSpectralSummer" ||
            value == "spectral" || value == "Spectral") {
            return WaterSurfacePreset::RealisticClearSummer;
        }
        return fallback;
    }

    WaterSurfacePreset ParseWaterSurfacePreset(
        int32_t value,
        WaterSurfacePreset fallback) noexcept {

        switch (value) {
        case 0:
        case 2: // v5 AnimeSparklingSummer -> AnimeSummer
            return WaterSurfacePreset::AnimeSummer;
        case 1:
        case 3: // v5 RealisticSpectralSummer -> RealisticClearSummer
            return WaterSurfacePreset::RealisticClearSummer;
        default:
            return fallback;
        }
    }

    WaterSurfaceGeometryMode ParseWaterSurfaceGeometryMode(
        std::string_view value,
        WaterSurfaceGeometryMode fallback) noexcept {

        if (value == "finite_authored" || value == "FiniteAuthored" ||
            value == "finite" || value == "Finite") {
            return WaterSurfaceGeometryMode::FiniteAuthored;
        }
        if (value == "camera_centered_ocean" ||
            value == "CameraCenteredOcean" || value == "ocean" ||
            value == "Ocean") {
            return WaterSurfaceGeometryMode::CameraCenteredOcean;
        }
        return fallback;
    }

    WaterSurfaceSettings MakeWaterSurfacePresetSettings(
        WaterSurfacePreset preset) noexcept {

        WaterSurfaceSettings settings{};
        if (preset == WaterSurfacePreset::RealisticClearSummer) {
            settings.preset = preset;
            settings.timeScale = 0.92f;
            settings.choppiness = 0.46f;
            settings.microWaveStrength = 0.52f;
            settings.depthTransmittance = { 0.10f, 0.52f, 0.72f };
            settings.scatteringColor = { 0.015f, 0.24f, 0.30f };
            settings.shallowClearDepth = 0.20f;
            settings.shallowBlendDepth = 2.80f;
            settings.shallowGraphicWeight = 0.14f;
            settings.foamColor = { 0.96f, 0.99f, 1.0f };
            settings.foamAmount = 0.30f;
            settings.absorptionDistance = 11.0f;
            // Keep the GGX lobe above the sub-pixel/TAA loss region. The
            // shader still applies derivative AA for authored values.
            settings.surfaceRoughness = 0.12f;
            settings.directSunStrength = 1.0f;
            settings.transmissionStrength = 1.0f;
            settings.shadowStrength = 0.82f;
            settings.reflectionStrength = 1.0f;
            settings.sunGlintStrength = 0.0f;
            settings.glintDensity = 0.0f;
            settings.glintFlowSpeed = 0.0f;
            settings.ribbonWidth = 0.08f;
            settings.windDirectionDeg = 28.0f;
            settings.windSpeed = 14.0f;
            settings.shortWaveStrength = 0.80f;
            settings.capillaryStrength = 0.48f;
            settings.waves = {{
                { 12.0f, 0.30f, 15.0f, 0.66f },
                { 61.0f, 0.16f, 8.3f, 0.55f },
                { -34.0f, 0.075f, 4.6f, 0.40f },
                { 126.0f, 0.035f, 2.7f, 0.26f },
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

    WaterSurfaceSettings SanitizeWaterSurfaceSettings(
        WaterSurfaceSettings settings) noexcept {

        if (settings.preset != WaterSurfacePreset::AnimeSummer &&
            settings.preset != WaterSurfacePreset::RealisticClearSummer) {
            settings.preset = WaterSurfacePreset::AnimeSummer;
        }
        if (settings.geometryMode !=
                WaterSurfaceGeometryMode::FiniteAuthored &&
            settings.geometryMode !=
                WaterSurfaceGeometryMode::CameraCenteredOcean) {
            settings.geometryMode =
                WaterSurfaceGeometryMode::FiniteAuthored;
        }
        const WaterSurfaceSettings fallback =
            MakeWaterSurfacePresetSettings(settings.preset);
        settings.timeScale = ClampFinite(
            settings.timeScale, fallback.timeScale, 0.0f, 4.0f);
        settings.choppiness = ClampFinite(
            settings.choppiness, fallback.choppiness, 0.0f, 1.0f);
        settings.microWaveStrength = ClampFinite(
            settings.microWaveStrength,
            fallback.microWaveStrength,
            0.0f,
            2.0f);
        settings.depthTransmittance = SanitizeColor(
            settings.depthTransmittance,
            fallback.depthTransmittance,
            0.001f,
            1.0f);
        settings.scatteringColor = SanitizeColor(
            settings.scatteringColor,
            fallback.scatteringColor,
            0.0f,
            8.0f);
        settings.shallowClearDepth = ClampFinite(
            settings.shallowClearDepth,
            fallback.shallowClearDepth,
            0.0f,
            kMaximumShallowDepth - kMinimumShallowBlendDepthDelta);
        settings.shallowBlendDepth = ClampFinite(
            settings.shallowBlendDepth,
            fallback.shallowBlendDepth,
            settings.shallowClearDepth + kMinimumShallowBlendDepthDelta,
            kMaximumShallowDepth);
        settings.shallowGraphicWeight = ClampFinite(
            settings.shallowGraphicWeight,
            fallback.shallowGraphicWeight,
            0.0f,
            1.0f);
        settings.foamColor = SanitizeColor(
            settings.foamColor,
            fallback.foamColor,
            0.0f,
            8.0f);
        settings.foamAmount = ClampFinite(
            settings.foamAmount, fallback.foamAmount, 0.0f, 2.0f);
        settings.absorptionDistance = ClampFinite(
            settings.absorptionDistance,
            fallback.absorptionDistance,
            0.05f,
            1000.0f);
        settings.surfaceRoughness = ClampFinite(
            settings.surfaceRoughness,
            fallback.surfaceRoughness,
            0.02f,
            1.0f);
        settings.directSunStrength = ClampFinite(
            settings.directSunStrength,
            fallback.directSunStrength,
            0.0f,
            2.0f);
        settings.transmissionStrength = ClampFinite(
            settings.transmissionStrength,
            fallback.transmissionStrength,
            0.0f,
            2.0f);
        settings.shadowStrength = ClampFinite(
            settings.shadowStrength,
            fallback.shadowStrength,
            0.0f,
            1.0f);
        settings.reflectionStrength = ClampFinite(
            settings.reflectionStrength,
            fallback.reflectionStrength,
            0.0f,
            2.0f);
        settings.sunGlintStrength = ClampFinite(
            settings.sunGlintStrength,
            fallback.sunGlintStrength,
            0.0f,
            2.0f);
        settings.glintDensity = ClampFinite(
            settings.glintDensity,
            fallback.glintDensity,
            0.0f,
            1.0f);
        settings.glintFlowSpeed = ClampFinite(
            settings.glintFlowSpeed,
            fallback.glintFlowSpeed,
            0.0f,
            4.0f);
        settings.ribbonWidth = ClampFinite(
            settings.ribbonWidth,
            fallback.ribbonWidth,
            0.02f,
            0.5f);
        settings.windDirectionDeg = WrapDegrees(
            FiniteOr(settings.windDirectionDeg, fallback.windDirectionDeg));
        settings.windSpeed = ClampFinite(
            settings.windSpeed,
            fallback.windSpeed,
            0.0f,
            50.0f);
        settings.shortWaveStrength = ClampFinite(
            settings.shortWaveStrength,
            fallback.shortWaveStrength,
            0.0f,
            2.0f);
        settings.capillaryStrength = ClampFinite(
            settings.capillaryStrength,
            fallback.capillaryStrength,
            0.0f,
            2.0f);

        for (std::size_t index = 0; index < settings.waves.size(); ++index) {
            WaterGerstnerWave& wave = settings.waves[index];
            const WaterGerstnerWave& fallbackWave = fallback.waves[index];
            wave.directionDeg = WrapDegrees(wave.directionDeg);
            wave.amplitude = ClampFinite(
                wave.amplitude, fallbackWave.amplitude, 0.0f, 25.0f);
            wave.wavelength = ClampFinite(
                wave.wavelength, fallbackWave.wavelength, 0.05f, 10000.0f);
            wave.steepness = ClampFinite(
                wave.steepness, fallbackWave.steepness, 0.0f, 1.0f);
        }
        return settings;
    }

    WaterSurfaceSettings ResolveWaterSurfaceSettingsForMesh(
        const WaterSurfaceSettings& rawSettings,
        float maximumVertexSpacing) noexcept {

        WaterSurfaceSettings settings =
            SanitizeWaterSurfaceSettings(rawSettings);
        const float spacing = ClampFinite(
            maximumVertexSpacing,
            0.0f,
            0.0f,
            100000.0f);
        if (spacing <= 1.0e-6f) {
            return settings;
        }

        const float fadeStart = 2.0f * spacing;
        const float fadeEnd = 4.0f * spacing;
        for (WaterGerstnerWave& wave : settings.waves) {
            const float resolved = SmoothStep01(
                (wave.wavelength - fadeStart) /
                (std::max)(fadeEnd - fadeStart, 1.0e-6f));
            wave.amplitude *= resolved;
            wave.steepness *= resolved;
        }
        return settings;
    }

    WaterSurfaceEvaluation EvaluateWaterSurface(
        const WaterSurfaceSettings& rawSettings,
        const MATH::Vec2& sourceLocalXZ,
        float timeSeconds) noexcept {

        const WaterSurfaceSettings settings =
            SanitizeWaterSurfaceSettings(rawSettings);
        WaterSurfaceEvaluation result{};
        result.positionLocal = {
            FiniteOr(sourceLocalXZ.x, 0.0f),
            0.0f,
            FiniteOr(sourceLocalXZ.y, 0.0f)
        };
        MATH::Vec3 tangentX{ 1.0f, 0.0f, 0.0f };
        MATH::Vec3 tangentZ{ 0.0f, 0.0f, 1.0f };

        const float animationTime =
            WrappedTime(timeSeconds) * settings.timeScale;
        const float inverseWaveCount =
            1.0f / static_cast<float>(kMaxWaterWaves);

        for (const WaterGerstnerWave& wave : settings.waves) {
            if (wave.amplitude <= 1.0e-7f ||
                wave.wavelength <= 1.0e-7f) {
                continue;
            }

            const float directionRadians =
                wave.directionDeg * (kPi / 180.0f);
            const float directionX = std::cos(directionRadians);
            const float directionZ = std::sin(directionRadians);
            const float waveNumber = kTwoPi / wave.wavelength;
            const float angularFrequency =
                std::sqrt(kGravity * waveNumber);
            const float phase =
                waveNumber *
                    (directionX * sourceLocalXZ.x +
                        directionZ * sourceLocalXZ.y) -
                angularFrequency * animationTime;
            const float sine = std::sin(phase);
            const float cosine = std::cos(phase);
            const float horizontalAmplitude =
                settings.choppiness * wave.steepness *
                inverseWaveCount / waveNumber;

            result.positionLocal.x +=
                directionX * horizontalAmplitude * cosine;
            result.positionLocal.y += wave.amplitude * sine;
            result.positionLocal.z +=
                directionZ * horizontalAmplitude * cosine;

            const float horizontalDerivative =
                horizontalAmplitude * waveNumber * sine;
            const float verticalDerivative =
                wave.amplitude * waveNumber * cosine;
            tangentX.x -=
                directionX * directionX * horizontalDerivative;
            tangentX.y += directionX * verticalDerivative;
            tangentX.z -=
                directionX * directionZ * horizontalDerivative;
            tangentZ.x -=
                directionX * directionZ * horizontalDerivative;
            tangentZ.y += directionZ * verticalDerivative;
            tangentZ.z -=
                directionZ * directionZ * horizontalDerivative;

            const float timeDerivative =
                angularFrequency * settings.timeScale;
            result.velocityLocal.x +=
                directionX * horizontalAmplitude *
                timeDerivative * sine;
            result.velocityLocal.y -=
                wave.amplitude * timeDerivative * cosine;
            result.velocityLocal.z +=
                directionZ * horizontalAmplitude *
                timeDerivative * sine;
        }

        result.normalLocal = MATH::Normalize(
            MATH::Cross(tangentZ, tangentX));
        if (MATH::Length(result.normalLocal) <= 1.0e-6f) {
            result.normalLocal = { 0.0f, 1.0f, 0.0f };
        }
        result.horizontalJacobian =
            tangentX.x * tangentZ.z - tangentX.z * tangentZ.x;
        result.compression = std::clamp(
            1.0f - result.horizontalJacobian,
            0.0f,
            1.0f);
        return result;
    }

    WaterSurfaceEvaluation EvaluateWaterSurfaceAtHorizontalPosition(
        const WaterSurfaceSettings& settings,
        const MATH::Vec2& deformedLocalXZ,
        float timeSeconds,
        MATH::Vec2* outSourceLocalXZ) noexcept {

        MATH::Vec2 source{
            FiniteOr(deformedLocalXZ.x, 0.0f),
            FiniteOr(deformedLocalXZ.y, 0.0f)
        };
        WaterSurfaceEvaluation evaluation{};
        for (uint32_t iteration = 0u; iteration < 4u; ++iteration) {
            evaluation = EvaluateWaterSurface(
                settings,
                source,
                timeSeconds);
            source.x += deformedLocalXZ.x - evaluation.positionLocal.x;
            source.y += deformedLocalXZ.y - evaluation.positionLocal.z;
        }
        evaluation = EvaluateWaterSurface(settings, source, timeSeconds);
        if (outSourceLocalXZ != nullptr) {
            *outSourceLocalXZ = source;
        }
        return evaluation;
    }

    float CalculateWaterMaximumHorizontalDisplacement(
        const WaterSurfaceSettings& rawSettings) noexcept {

        const WaterSurfaceSettings settings =
            SanitizeWaterSurfaceSettings(rawSettings);
        float maximum = 0.0f;
        for (const WaterGerstnerWave& wave : settings.waves) {
            if (wave.amplitude <= 1.0e-7f) {
                continue;
            }
            const float waveNumber = kTwoPi / wave.wavelength;
            maximum += settings.choppiness * wave.steepness /
                (waveNumber * static_cast<float>(kMaxWaterWaves));
        }
        return maximum;
    }

    bool WaterSurfaceSettingsEqual(
        const WaterSurfaceSettings& lhs,
        const WaterSurfaceSettings& rhs) noexcept {

        const auto equalVec3 = [](
            const MATH::Vec3& a,
            const MATH::Vec3& b) noexcept {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        };
        if (lhs.preset != rhs.preset ||
            lhs.geometryMode != rhs.geometryMode ||
            lhs.timeScale != rhs.timeScale ||
            lhs.choppiness != rhs.choppiness ||
            lhs.microWaveStrength != rhs.microWaveStrength ||
            !equalVec3(
                lhs.depthTransmittance,
                rhs.depthTransmittance) ||
            !equalVec3(lhs.scatteringColor, rhs.scatteringColor) ||
            lhs.shallowClearDepth != rhs.shallowClearDepth ||
            lhs.shallowBlendDepth != rhs.shallowBlendDepth ||
            lhs.shallowGraphicWeight != rhs.shallowGraphicWeight ||
            !equalVec3(lhs.foamColor, rhs.foamColor) ||
            lhs.foamAmount != rhs.foamAmount ||
            lhs.absorptionDistance != rhs.absorptionDistance ||
            lhs.surfaceRoughness != rhs.surfaceRoughness ||
            lhs.directSunStrength != rhs.directSunStrength ||
            lhs.transmissionStrength != rhs.transmissionStrength ||
            lhs.shadowStrength != rhs.shadowStrength ||
            lhs.reflectionStrength != rhs.reflectionStrength ||
            lhs.sunGlintStrength != rhs.sunGlintStrength ||
            lhs.glintDensity != rhs.glintDensity ||
            lhs.glintFlowSpeed != rhs.glintFlowSpeed ||
            lhs.ribbonWidth != rhs.ribbonWidth ||
            lhs.windDirectionDeg != rhs.windDirectionDeg ||
            lhs.windSpeed != rhs.windSpeed ||
            lhs.shortWaveStrength != rhs.shortWaveStrength ||
            lhs.capillaryStrength != rhs.capillaryStrength) {
            return false;
        }
        for (std::size_t index = 0; index < lhs.waves.size(); ++index) {
            const WaterGerstnerWave& left = lhs.waves[index];
            const WaterGerstnerWave& right = rhs.waves[index];
            if (left.directionDeg != right.directionDeg ||
                left.amplitude != right.amplitude ||
                left.wavelength != right.wavelength ||
                left.steepness != right.steepness) {
                return false;
            }
        }
        return true;
    }

} // namespace HIKARI::WATER
