#include "Render3D/Water/HIKARI_WaterSurfaceRenderData.h"
#include "Render3D/Water/HIKARI_WaterSurfaceGpuData.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D::WATER {

    namespace {
        constexpr float kTwoPi = 6.28318530717958647692f;
        constexpr float kGravity = 9.81f;
        constexpr float kMinimumWavelength = 0.05f;
        constexpr float kDefaultShallowClearDepth = 0.20f;
        constexpr float kDefaultShallowBlendDepth = 2.80f;
        constexpr float kDefaultShallowGraphicWeight = 0.14f;
        constexpr float kMinimumShallowDepthRange = 1.0e-3f;
        constexpr float kMaximumShallowDepth = 1000.0f;
        constexpr float kDefaultReflectionStrength = 1.0f;
        constexpr float kDefaultRibbonWidth = 0.02f;

        bool Vec3Equal(
            const MATH::Vec3& left,
            const MATH::Vec3& right) noexcept {
            return left.x == right.x && left.y == right.y &&
                left.z == right.z;
        }

        bool Vec4Equal(
            const MATH::Vec4& left,
            const MATH::Vec4& right) noexcept {
            return left.x == right.x && left.y == right.y &&
                left.z == right.z && left.w == right.w;
        }

        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        float NonNegative(float value, float fallback = 0.0f) noexcept {
            return (std::max)(FiniteOr(value, fallback), 0.0f);
        }

        MATH::Vec3 NonNegative(const MATH::Vec3& value) noexcept {
            return {
                NonNegative(value.x),
                NonNegative(value.y),
                NonNegative(value.z)
            };
        }
    }

    bool WaterSurfaceRenderDataEqual(
        const WaterSurfaceRenderData& left,
        const WaterSurfaceRenderData& right) noexcept {

        if (left.shadingModel != right.shadingModel ||
            left.geometryMode != right.geometryMode ||
            left.timeScale != right.timeScale ||
            left.choppiness != right.choppiness ||
            left.microWaveStrength != right.microWaveStrength ||
            !Vec3Equal(
                left.depthTransmittance,
                right.depthTransmittance) ||
            !Vec3Equal(left.scatteringColor, right.scatteringColor) ||
            !Vec3Equal(left.foamColor, right.foamColor) ||
            left.foamAmount != right.foamAmount ||
            left.absorptionDistance != right.absorptionDistance ||
            left.surfaceRoughness != right.surfaceRoughness ||
            left.shallowClearDepth != right.shallowClearDepth ||
            left.shallowBlendDepth != right.shallowBlendDepth ||
            left.shallowGraphicWeight != right.shallowGraphicWeight ||
            left.reflectionStrength != right.reflectionStrength ||
            left.sunGlintStrength != right.sunGlintStrength ||
            left.glintDensity != right.glintDensity ||
            left.glintFlowSpeed != right.glintFlowSpeed ||
            left.ribbonWidth != right.ribbonWidth ||
            left.windDirectionRadians != right.windDirectionRadians ||
            left.windSpeed != right.windSpeed ||
            left.shortWaveStrength != right.shortWaveStrength ||
            left.capillaryStrength != right.capillaryStrength ||
            !Vec4Equal(left.geometryParams, right.geometryParams) ||
            left.coverageResolution != right.coverageResolution ||
            left.liquidCoverage != right.liquidCoverage) {
            return false;
        }
        for (std::size_t index = 0u; index < left.waves.size(); ++index) {
            const WaterWaveRenderData& a = left.waves[index];
            const WaterWaveRenderData& b = right.waves[index];
            if (a.directionRadians != b.directionRadians ||
                a.amplitude != b.amplitude ||
                a.wavelength != b.wavelength ||
                a.steepness != b.steepness) {
                return false;
            }
        }
        return true;
    }

    WaterSurfaceGpuData BuildWaterSurfaceGpuDataSentinel(
        uint32_t recordCount) noexcept {

        WaterSurfaceGpuData result{};
        // sentinel は schema を識別できるが feature 自体は無効のままにする。
        // recordCount は Root SRV が持たない bounds 情報を明示的に運ぶ。
        result.schemaVersion = kWaterSurfaceGpuSchemaVersion;
        result.recordCount = (std::max)(recordCount, 1u);
        return result;
    }

    WaterSurfaceGpuData BuildWaterSurfaceGpuData(
        const WaterSurfaceRenderData& source,
        uint32_t liquidCoverageOffset) noexcept {

        WaterSurfaceGpuData result{};
        result.schemaVersion = kWaterSurfaceGpuSchemaVersion;
        result.flags = static_cast<uint32_t>(WaterSurfaceGpuFlags::Enabled);
        if (source.geometryMode ==
                WaterSurfaceGeometryMode::CameraCenteredOcean) {
            result.flags |= static_cast<uint32_t>(
                WaterSurfaceGpuFlags::CameraCenteredOcean);
        }
        result.waveCount = static_cast<uint32_t>(kWaterSurfaceWaveCount);
        // 非 sentinel の recordCount は liquidCoverage の buffer offset を運ぶ。
        result.recordCount = liquidCoverageOffset;
        const float shallowClearDepth = std::clamp(
            FiniteOr(
                source.shallowClearDepth,
                kDefaultShallowClearDepth),
            0.0f,
            kMaximumShallowDepth - kMinimumShallowDepthRange);
        const float shallowBlendDepth = std::clamp(
            FiniteOr(
                source.shallowBlendDepth,
                kDefaultShallowBlendDepth),
            shallowClearDepth + kMinimumShallowDepthRange,
            kMaximumShallowDepth);
        const float shallowGraphicWeight = std::clamp(
            FiniteOr(
                source.shallowGraphicWeight,
                kDefaultShallowGraphicWeight),
            0.0f,
            1.0f);
        result.coverageParams = {
            static_cast<float>((std::max)(source.coverageResolution, 1u)),
            shallowClearDepth,
            shallowBlendDepth,
            shallowGraphicWeight
        };

        const WaterSurfaceShadingModel shadingModel =
            source.shadingModel == WaterSurfaceShadingModel::
                    RealisticSpectralSummer
                ? WaterSurfaceShadingModel::RealisticSpectralSummer
                : source.shadingModel == WaterSurfaceShadingModel::
                    AnimeSparklingSummer
                ? WaterSurfaceShadingModel::AnimeSparklingSummer
                : source.shadingModel == WaterSurfaceShadingModel::
                    RealisticClearSummer
                    ? WaterSurfaceShadingModel::RealisticClearSummer
                    : WaterSurfaceShadingModel::AnimeSummer;
        result.surfaceParams = {
            FiniteOr(source.timeScale, 1.0f),
            NonNegative(source.choppiness),
            static_cast<float>(shadingModel),
            NonNegative(source.microWaveStrength)
        };

        const MATH::Vec3 depthTransmittance =
            NonNegative(source.depthTransmittance);
        result.depthTransmittanceFoam = {
            depthTransmittance.x,
            depthTransmittance.y,
            depthTransmittance.z,
            std::clamp(FiniteOr(source.foamAmount, 0.0f), 0.0f, 1.0f)
        };
        const MATH::Vec3 scattering = NonNegative(source.scatteringColor);
        result.scatteringAbsorption = {
            scattering.x,
            scattering.y,
            scattering.z,
            (std::max)(FiniteOr(source.absorptionDistance, 1.0f), 0.001f)
        };
        const MATH::Vec3 foam = NonNegative(source.foamColor);
        result.foamRoughness = {
            foam.x,
            foam.y,
            foam.z,
            std::clamp(FiniteOr(source.surfaceRoughness, 0.1f), 0.02f, 1.0f)
        };
        result.stylizedOptics = {
            std::clamp(
                FiniteOr(
                    source.reflectionStrength,
                    kDefaultReflectionStrength),
                0.0f,
                2.0f),
            std::clamp(FiniteOr(source.sunGlintStrength, 0.0f), 0.0f, 2.0f),
            std::clamp(FiniteOr(source.glintDensity, 0.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(source.glintFlowSpeed, 0.0f), 0.0f, 4.0f)
        };
        result.stylizedRibbon = {
            std::clamp(
                FiniteOr(source.ribbonWidth, kDefaultRibbonWidth),
                0.02f,
                0.5f),
            0.0f,
            0.0f,
            0.0f
        };
        result.spectralOptics = {
            FiniteOr(source.windDirectionRadians, 0.0f),
            std::clamp(FiniteOr(source.windSpeed, 0.0f), 0.0f, 50.0f),
            std::clamp(
                FiniteOr(source.shortWaveStrength, 0.0f),
                0.0f,
                2.0f),
            std::clamp(
                FiniteOr(source.capillaryStrength, 0.0f),
                0.0f,
                2.0f)
        };
        result.geometryParams = {
            FiniteOr(source.geometryParams.x, 0.0f),
            FiniteOr(source.geometryParams.y, 0.0f),
            (std::max)(NonNegative(source.geometryParams.z, 0.5f), 0.001f),
            (std::max)(NonNegative(source.geometryParams.w, 0.5f), 0.001f)
        };

        for (std::size_t index = 0u; index < source.waves.size(); ++index) {
            const WaterWaveRenderData& wave = source.waves[index];
            const float directionRadians =
                FiniteOr(wave.directionRadians, 0.0f);
            const float wavelength = (std::max)(
                FiniteOr(wave.wavelength, 1.0f),
                kMinimumWavelength);
            const float waveNumber = kTwoPi / wavelength;
            const float angularFrequency = std::sqrt(kGravity * waveNumber);
            result.waves[index].directionAmplitudeWaveNumber = {
                std::cos(directionRadians),
                std::sin(directionRadians),
                NonNegative(wave.amplitude),
                waveNumber
            };
            result.waves[index].angularFrequencySteepness = {
                angularFrequency,
                NonNegative(wave.steepness),
                0.0f,
                0.0f
            };
        }
        return result;
    }

} // namespace HIKARI::RENDER3D::WATER
