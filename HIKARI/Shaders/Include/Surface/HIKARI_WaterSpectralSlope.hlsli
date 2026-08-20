#ifndef HIKARI_WATER_SPECTRAL_SLOPE_INCLUDED
#define HIKARI_WATER_SPECTRAL_SLOPE_INCLUDED

// 水平方向の world-anchored analytic slope field。テクスチャや明るさの
// noise ではなく、短波/毛細波の height 微分を返すため法線応答にだけ使う。
//
#include "Include/Water/HIKARI_OceanSpectrumSampling.hlsli"

// GPU spectrum が有効なら三つの独立 cascade を権威データとして使い、
// デバイス初期化や dispatch が未完了のフレームだけ解析 slope へ退避する。
struct HikariWaterSpectralSlopeResult
{
    float2 slope;
    float variance;
};

struct HikariWaterSpectrumDataSample
{
    uint connected;
    float2 slope;
    float variance;
};

float2 HikariWaterSpectralSafeNormalize2(
    float2 value,
    float2 fallback)
{
    const float lengthSquared = dot(value, value);
    return HikariIsFiniteFloat(lengthSquared) &&
            lengthSquared > 1.0e-12f
        ? value * rsqrt(lengthSquared)
        : fallback;
}

float HikariWaterExactDielectricFresnel(
    float cosineIncident,
    float incidentIor,
    float transmittedIor)
{
    const float cosIncident = saturate(abs(HikariFiniteOr(
        cosineIncident,
        0.0f)));
    const float etaIncident = max(
        HikariFiniteOr(incidentIor, 1.0f),
        1.0e-4f);
    const float etaTransmitted = max(
        HikariFiniteOr(transmittedIor, 1.333f),
        1.0e-4f);
    const float eta = etaIncident / etaTransmitted;
    const float sinIncidentSquared = max(
        1.0f - cosIncident * cosIncident,
        0.0f);
    const float sinTransmittedSquared = eta * eta *
        sinIncidentSquared;
    if (sinTransmittedSquared >= 1.0f)
    {
        return 1.0f;
    }
    const float cosTransmitted = sqrt(max(
        1.0f - sinTransmittedSquared,
        0.0f));
    const float rsNumerator = etaIncident * cosIncident -
        etaTransmitted * cosTransmitted;
    const float rsDenominator = max(
        etaIncident * cosIncident +
            etaTransmitted * cosTransmitted,
        1.0e-4f);
    const float rpNumerator = etaTransmitted * cosIncident -
        etaIncident * cosTransmitted;
    const float rpDenominator = max(
        etaTransmitted * cosIncident +
            etaIncident * cosTransmitted,
        1.0e-4f);
    const float rs = rsNumerator / rsDenominator;
    const float rp = rpNumerator / rpDenominator;
    return saturate(0.5f * (rs * rs + rp * rp));
}

float2 HikariWaterSpectralBoundSlope(float2 slope)
{
    // A bad provider sample must soften into roughness, never flip the
    // water normal and turn one narrow phase into a black contour.
    return clamp(HikariFiniteOr(slope, 0.0f.xx), -0.26f.xx, 0.26f.xx);
}

HikariWaterSpectrumDataSample HikariWaterSampleSpectrumData(
    float2 worldXZ,
    float windDirectionRadians,
    float shortWaveStrength,
    float capillaryStrength)
{
    HikariWaterSpectrumDataSample sample =
        (HikariWaterSpectrumDataSample)0;
    const HikariOceanSpectrumSample spectrum = HikariSampleOceanSpectrum(
        worldXZ,
        windDirectionRadians,
        shortWaveStrength,
        capillaryStrength);
    sample.connected = spectrum.connected;
    // Geometry already carries long/wind macro slope. Pixel shading adds
    // only the residual wind/capillary band so the normal is not doubled.
    sample.slope = spectrum.detailSlope;
    sample.variance = spectrum.variance;
    return sample;
}

float HikariWaterSpectralHashPhase(float seed)
{
    return frac(sin(seed * 91.731f) * 43758.5453f) *
        6.28318530718f;
}

float HikariWaterSpectralDirectionalWeight(
    float2 direction,
    float2 wind,
    float exponent)
{
    return pow(
        saturate(0.5f + 0.5f * dot(direction, wind)),
        max(exponent, 0.25f));
}

void HikariWaterSpectralAddSlopeTerm(
    inout float2 slope,
    float2 worldXZ,
    float2 wind,
    float2 direction,
    float waveNumber,
    float animationTime,
    float windSpeed,
    float heightScale,
    float capillaryWeight,
    float seed)
{
    const float safeWaveNumber = max(waveNumber, 0.05f);
    const float safeWindSpeed = max(HikariFiniteOr(windSpeed, 0.0f), 0.0f);
    const float deepWaterGravity = 9.81f * safeWaveNumber;
    const float capillaryDispersion =
        7.2e-5f * capillaryWeight *
        safeWaveNumber * safeWaveNumber * safeWaveNumber;
    const float angularFrequency = sqrt(
        max(deepWaterGravity + capillaryDispersion, 1.0e-5f));
    // Dispersion owns the phase velocity. A small bounded current keeps the
    // field wind-advected without turning high-k capillary terms into a
    // frame-to-frame strobe at authored wind speeds.
    const float boundedAdvectionSpeed = min(safeWindSpeed * 0.02f, 0.75f);
    const float2 advectedPosition = HikariFiniteOr(worldXZ, 0.0f.xx) -
        wind * boundedAdvectionSpeed * HikariFiniteOr(animationTime, 0.0f);
    const float phase = dot(advectedPosition, direction) * safeWaveNumber -
        angularFrequency * HikariFiniteOr(animationTime, 0.0f) +
        HikariWaterSpectralHashPhase(seed);
    // The derivative of A sin(k dot x) is direction * A * k * cos(...).
    slope += direction * cos(phase) * heightScale * safeWaveNumber;
}

HikariWaterSpectralSlopeResult HikariWaterEvaluateSpectralSlope(
    float2 worldXZ,
    float animationTime,
    float windDirectionRadians,
    float windSpeed,
    float shortWaveStrength,
    float capillaryStrength)
{
    HikariWaterSpectralSlopeResult result =
        (HikariWaterSpectralSlopeResult)0;
    const float shortStrength = clamp(
        HikariFiniteOr(shortWaveStrength, 0.0f),
        0.0f,
        2.0f);
    const float capillary = clamp(
        HikariFiniteOr(capillaryStrength, 0.0f),
        0.0f,
        2.0f);
    const float direction = HikariFiniteOr(
        windDirectionRadians,
        0.0f);
    const float2 wind = float2(cos(direction), sin(direction));

    // 10 deterministic terms span deep-water short waves through capillary
    // wavelengths. Their directions diffuse around wind instead of forming a
    // repeated cross-wind stripe; the final spread normalization preserves
    // authored amplitude as direction count changes.
    const float2 direction0 = float2(cos(direction - 1.08f), sin(direction - 1.08f));
    const float2 direction1 = float2(cos(direction - 0.79f), sin(direction - 0.79f));
    const float2 direction2 = float2(cos(direction - 0.53f), sin(direction - 0.53f));
    const float2 direction3 = float2(cos(direction - 0.28f), sin(direction - 0.28f));
    const float2 direction4 = float2(cos(direction - 0.06f), sin(direction - 0.06f));
    const float2 direction5 = float2(cos(direction + 0.19f), sin(direction + 0.19f));
    const float2 direction6 = float2(cos(direction + 0.43f), sin(direction + 0.43f));
    const float2 direction7 = float2(cos(direction + 0.70f), sin(direction + 0.70f));
    const float2 direction8 = float2(cos(direction + 1.02f), sin(direction + 1.02f));
    const float2 direction9 = float2(cos(direction + 1.34f), sin(direction + 1.34f));
    const float spread0 = HikariWaterSpectralDirectionalWeight(direction0, wind, 2.8f);
    const float spread1 = HikariWaterSpectralDirectionalWeight(direction1, wind, 2.8f);
    const float spread2 = HikariWaterSpectralDirectionalWeight(direction2, wind, 2.8f);
    const float spread3 = HikariWaterSpectralDirectionalWeight(direction3, wind, 2.8f);
    const float spread4 = HikariWaterSpectralDirectionalWeight(direction4, wind, 2.8f);
    const float spread5 = HikariWaterSpectralDirectionalWeight(direction5, wind, 2.8f);
    const float spread6 = HikariWaterSpectralDirectionalWeight(direction6, wind, 2.8f);
    const float spread7 = HikariWaterSpectralDirectionalWeight(direction7, wind, 3.4f);
    const float spread8 = HikariWaterSpectralDirectionalWeight(direction8, wind, 3.4f);
    const float spread9 = HikariWaterSpectralDirectionalWeight(direction9, wind, 3.4f);
    const float spreadNormalization = rcp(max(
        spread0 + spread1 + spread2 + spread3 + spread4 +
        spread5 + spread6 + spread7 + spread8 + spread9,
        1.0e-3f));

    const float heightScale0 = 0.018f * shortStrength * spread0;
    const float heightScale1 = 0.015f * shortStrength * spread1;
    const float heightScale2 = 0.012f * shortStrength * spread2;
    const float heightScale3 = 0.009f * shortStrength * spread3;
    const float heightScale4 = 0.007f * shortStrength * spread4;
    const float heightScale5 = 0.0055f * shortStrength * spread5;
    const float heightScale6 = 0.0040f * shortStrength * spread6;
    const float heightScale7 = 0.0028f * capillary * spread7;
    const float heightScale8 = 0.0020f * capillary * spread8;
    const float heightScale9 = 0.0014f * capillary * spread9;

    // Height amplitudes are inversely scaled by k so each term contributes a
    // bounded slope derivative. Terms 0..6 are deep-water short waves;
    // terms 7..9 add higher-frequency capillary dispersion.
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction0, 0.44f,
        animationTime, windSpeed, heightScale0,
        0.0f, 0.0f);
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction1, 0.68f,
        animationTime, windSpeed, heightScale1,
        0.0f, 1.0f);
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction2, 1.02f,
        animationTime, windSpeed, heightScale2,
        0.0f, 2.0f);
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction3, 1.46f,
        animationTime, windSpeed, heightScale3,
        0.0f, 3.0f);
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction4, 2.05f,
        animationTime, windSpeed, heightScale4,
        0.0f, 4.0f);
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction5, 2.86f,
        animationTime, windSpeed, heightScale5,
        0.0f, 5.0f);
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction6, 4.08f,
        animationTime, windSpeed, heightScale6,
        0.0f, 6.0f);
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction7, 6.40f,
        animationTime, windSpeed, heightScale7,
        1.0f, 7.0f);
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction8, 9.80f,
        animationTime, windSpeed, heightScale8,
        1.0f, 8.0f);
    HikariWaterSpectralAddSlopeTerm(
        result.slope, worldXZ, wind, direction9, 14.60f,
        animationTime, windSpeed, heightScale9,
        1.0f, 9.0f);
    result.slope *= spreadNormalization;
    result.slope = HikariFiniteOr(result.slope, 0.0f.xx);
    result.slope = HikariWaterSpectralBoundSlope(result.slope);
    // Roughness uses expected squared slope energy, not the instantaneous
    // phase sample, so unresolved high-frequency waves do not shimmer.
    const float expectedSlopeEnergy = 0.5f * (
        (heightScale0 * 0.44f) * (heightScale0 * 0.44f) +
        (heightScale1 * 0.68f) * (heightScale1 * 0.68f) +
        (heightScale2 * 1.02f) * (heightScale2 * 1.02f) +
        (heightScale3 * 1.46f) * (heightScale3 * 1.46f) +
        (heightScale4 * 2.05f) * (heightScale4 * 2.05f) +
        (heightScale5 * 2.86f) * (heightScale5 * 2.86f) +
        (heightScale6 * 4.08f) * (heightScale6 * 4.08f) +
        (heightScale7 * 6.40f) * (heightScale7 * 6.40f) +
        (heightScale8 * 9.80f) * (heightScale8 * 9.80f) +
        (heightScale9 * 14.60f) * (heightScale9 * 14.60f)) *
        spreadNormalization * spreadNormalization;
    result.variance = saturate(expectedSlopeEnergy * 18.0f);
    return result;
}

HikariWaterSpectralSlopeResult HikariWaterResolveSpectralSlope(
    float2 worldXZ,
    float animationTime,
    float windDirectionRadians,
    float windSpeed,
    float shortWaveStrength,
    float capillaryStrength)
{
    const HikariWaterSpectrumDataSample spectrumData =
        HikariWaterSampleSpectrumData(
            worldXZ,
            windDirectionRadians,
            shortWaveStrength,
            capillaryStrength);
    if (spectrumData.connected != 0u)
    {
        HikariWaterSpectralSlopeResult result;
        result.slope = HikariWaterSpectralBoundSlope(
            spectrumData.slope);
        result.variance = saturate(HikariFiniteOr(
            spectrumData.variance,
            0.0f));
        return result;
    }

    // Fallback only: the production seam above is where a future FFT
    // spectrum sample becomes authoritative. This deterministic analytic
    // field must never be treated as a brightness/noise overlay.
    return HikariWaterEvaluateSpectralSlope(
        worldXZ,
        animationTime,
        windDirectionRadians,
        windSpeed,
        shortWaveStrength,
        capillaryStrength);
}

float HikariWaterSpectralResolvedSlopeWeight(
    float2 worldXZ,
    float distanceToCamera)
{
    const float footprint = max(
        length(fwidth(HikariFiniteOr(worldXZ, 0.0f.xx))),
        1.0e-4f);
    const float footprintWeight = 1.0f - smoothstep(
        0.035f,
        0.32f,
        footprint);
    const float distanceWeight = 1.0f - smoothstep(
        48.0f,
        280.0f,
        max(HikariFiniteOr(distanceToCamera, 0.0f), 0.0f));
    return footprintWeight * distanceWeight;
}

float3 HikariWaterSpectralNormalFromSlope(
    float3 geometricNormal,
    float2 slope,
    float resolvedWeight)
{
    const float3 baseNormal = HikariSafeNormalize(
        geometricNormal,
        float3(0.0f, 1.0f, 0.0f));
    const float3 tangentX = HikariSafeNormalize(
        float3(1.0f, 0.0f, 0.0f) - baseNormal * baseNormal.x,
        float3(1.0f, 0.0f, 0.0f));
    const float3 tangentZ = HikariSafeNormalize(
        cross(tangentX, baseNormal),
        float3(0.0f, 0.0f, 1.0f));
    const float2 boundedSlope = HikariWaterSpectralBoundSlope(slope);
    const float3 slopeNormal = HikariSafeNormalize(
        baseNormal - tangentX * boundedSlope.x - tangentZ * boundedSlope.y,
        baseNormal);
    return HikariSafeNormalize(
        lerp(baseNormal, slopeNormal, saturate(resolvedWeight)),
        baseNormal);
}

float HikariWaterSpectralRoughnessFromFootprint(
    float baseRoughness,
    float slopeVariance,
    float2 worldXZ,
    float distanceToCamera,
    float resolvedWeight)
{
    const float footprint = max(
        length(fwidth(HikariFiniteOr(worldXZ, 0.0f.xx))),
        1.0e-4f);
    const float unresolvedFraction = 1.0f - saturate(resolvedWeight);
    const float footprintEnergy = saturate(footprint * 0.34f);
    const float distanceEnergy = smoothstep(
        48.0f,
        280.0f,
        max(HikariFiniteOr(distanceToCamera, 0.0f), 0.0f));
    const float unresolvedSlopeEnergy = saturate(
        unresolvedFraction * (slopeVariance * 0.46f +
            footprintEnergy * 0.18f) +
        distanceEnergy * slopeVariance * 0.12f);
    return clamp(
        HikariFiniteOr(baseRoughness, 0.1f) +
            unresolvedSlopeEnergy,
        0.02f,
        1.0f);
}

#endif
