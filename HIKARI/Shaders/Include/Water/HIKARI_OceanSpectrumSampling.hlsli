#ifndef HIKARI_OCEAN_SPECTRUM_SAMPLING_INCLUDED
#define HIKARI_OCEAN_SPECTRUM_SAMPLING_INCLUDED

#include "Include/Math/HIKARI_FiniteMath.hlsli"

static const uint HIKARI_OCEAN_SPECTRUM_CASCADE_COUNT = 3u;
static const float HIKARI_OCEAN_SPECTRUM_LONG_SCALE = 240.0f;
static const float HIKARI_OCEAN_SPECTRUM_WIND_SCALE = 64.0f;
static const float HIKARI_OCEAN_SPECTRUM_CAPILLARY_SCALE = 12.0f;
static const float HIKARI_OCEAN_SPECTRUM_CANONICAL_WIND_ANGLE = -0.48f;

// One contiguous root table owned by OceanSpectrumRuntime.
Texture2D<float4> gOceanSpectrumDisplacement[3] : register(t5, space7);
Texture2D<float4> gOceanSpectrumSlope[3] : register(t8, space7);

cbuffer HikariOceanSpectrumControl : register(b11)
{
    uint gOceanSpectrumValid;
    uint gOceanSpectrumResolution;
};

struct HikariOceanSpectrumSample
{
    uint connected;
    float3 displacement;
    float2 slope;
    float2 macroSlope;
    float2 detailSlope;
    float jacobian;
    float variance;
};

float2 HikariOceanSpectrumRotate(float2 value, float angle)
{
    const float cosine = cos(angle);
    const float sine = sin(angle);
    return float2(
        cosine * value.x - sine * value.y,
        sine * value.x + cosine * value.y);
}

uint2 HikariOceanSpectrumWrap(int2 coord, uint resolution)
{
    const int signedResolution = (int)max(resolution, 1u);
    int2 wrapped = coord % signedResolution;
    wrapped += (int2)(wrapped < 0) * signedResolution;
    return (uint2)wrapped;
}

float4 HikariOceanSpectrumLoadDisplacement(uint cascade, int2 coord)
{
    const uint2 wrapped = HikariOceanSpectrumWrap(
        coord,
        gOceanSpectrumResolution);
    if (cascade == 0u)
    {
        return gOceanSpectrumDisplacement[0].Load(int3(wrapped, 0));
    }
    if (cascade == 1u)
    {
        return gOceanSpectrumDisplacement[1].Load(int3(wrapped, 0));
    }
    return gOceanSpectrumDisplacement[2].Load(int3(wrapped, 0));
}

float4 HikariOceanSpectrumLoadSlope(uint cascade, int2 coord)
{
    const uint2 wrapped = HikariOceanSpectrumWrap(
        coord,
        gOceanSpectrumResolution);
    if (cascade == 0u)
    {
        return gOceanSpectrumSlope[0].Load(int3(wrapped, 0));
    }
    if (cascade == 1u)
    {
        return gOceanSpectrumSlope[1].Load(int3(wrapped, 0));
    }
    return gOceanSpectrumSlope[2].Load(int3(wrapped, 0));
}

float4 HikariOceanSpectrumSampleBilinear(
    uint cascade,
    float2 canonicalXZ,
    float lengthScale,
    bool sampleSlope)
{
    const uint resolution = clamp(
        gOceanSpectrumResolution,
        2u,
        4096u);
    const float2 gridPosition =
        canonicalXZ / max(lengthScale, 1.0e-3f) * (float)resolution - 0.5f;
    const int2 baseCoord = (int2)floor(gridPosition);
    const float2 blend = frac(gridPosition);
    const float4 v00 = sampleSlope
        ? HikariOceanSpectrumLoadSlope(cascade, baseCoord)
        : HikariOceanSpectrumLoadDisplacement(cascade, baseCoord);
    const float4 v10 = sampleSlope
        ? HikariOceanSpectrumLoadSlope(cascade, baseCoord + int2(1, 0))
        : HikariOceanSpectrumLoadDisplacement(cascade, baseCoord + int2(1, 0));
    const float4 v01 = sampleSlope
        ? HikariOceanSpectrumLoadSlope(cascade, baseCoord + int2(0, 1))
        : HikariOceanSpectrumLoadDisplacement(cascade, baseCoord + int2(0, 1));
    const float4 v11 = sampleSlope
        ? HikariOceanSpectrumLoadSlope(cascade, baseCoord + int2(1, 1))
        : HikariOceanSpectrumLoadDisplacement(cascade, baseCoord + int2(1, 1));
    return HikariFiniteOr(
        lerp(lerp(v00, v10, blend.x), lerp(v01, v11, blend.x), blend.y),
        0.0f.xxxx);
}

HikariOceanSpectrumSample HikariSampleOceanSpectrum(
    float2 worldXZ,
    float windDirectionRadians,
    float shortWaveStrength,
    float capillaryStrength)
{
    HikariOceanSpectrumSample result = (HikariOceanSpectrumSample)0;
    if (gOceanSpectrumValid == 0u ||
        gOceanSpectrumResolution < 2u)
    {
        return result;
    }

    const float rotation = HikariFiniteOr(
        windDirectionRadians,
        HIKARI_OCEAN_SPECTRUM_CANONICAL_WIND_ANGLE) -
        HIKARI_OCEAN_SPECTRUM_CANONICAL_WIND_ANGLE;
    const float2 canonicalXZ = HikariOceanSpectrumRotate(
        HikariFiniteOr(worldXZ, 0.0f.xx),
        -rotation);
    const float shortWeight = saturate(
        HikariFiniteOr(shortWaveStrength, 0.0f) * 0.5f);
    const float capillaryWeight = saturate(
        HikariFiniteOr(capillaryStrength, 0.0f) * 0.5f);

    const float4 longDisplacement = HikariOceanSpectrumSampleBilinear(
        0u,
        canonicalXZ,
        HIKARI_OCEAN_SPECTRUM_LONG_SCALE,
        false);
    const float4 windDisplacement = HikariOceanSpectrumSampleBilinear(
        1u,
        canonicalXZ,
        HIKARI_OCEAN_SPECTRUM_WIND_SCALE,
        false);
    const float4 capillaryDisplacement = HikariOceanSpectrumSampleBilinear(
        2u,
        canonicalXZ,
        HIKARI_OCEAN_SPECTRUM_CAPILLARY_SCALE,
        false);
    const float4 longSlope = HikariOceanSpectrumSampleBilinear(
        0u,
        canonicalXZ,
        HIKARI_OCEAN_SPECTRUM_LONG_SCALE,
        true);
    const float4 windSlope = HikariOceanSpectrumSampleBilinear(
        1u,
        canonicalXZ,
        HIKARI_OCEAN_SPECTRUM_WIND_SCALE,
        true);
    const float4 capillarySlope = HikariOceanSpectrumSampleBilinear(
        2u,
        canonicalXZ,
        HIKARI_OCEAN_SPECTRUM_CAPILLARY_SCALE,
        true);

    float3 canonicalDisplacement =
        longDisplacement.xyz * 0.86f +
        windDisplacement.xyz * lerp(0.30f, 0.82f, shortWeight) +
        capillaryDisplacement.xyz * (0.08f * capillaryWeight);
    float2 canonicalHorizontal = canonicalDisplacement.xz;
    const float horizontalLength = length(canonicalHorizontal);
    if (horizontalLength > 1.75f)
    {
        canonicalHorizontal *= 1.75f / horizontalLength;
    }
    result.displacement.xz = HikariOceanSpectrumRotate(
        canonicalHorizontal,
        rotation);
    result.displacement.y = clamp(
        HikariFiniteOr(canonicalDisplacement.y, 0.0f),
        -2.5f,
        2.5f);

    const float2 canonicalMacroSlope =
        longSlope.xy * 0.32f +
        windSlope.xy * lerp(0.30f, 0.76f, shortWeight);
    const float2 canonicalDetailSlope =
        windSlope.xy * lerp(0.04f, 0.16f, shortWeight) +
        capillarySlope.xy * lerp(0.0f, 0.72f, capillaryWeight);
    result.macroSlope = clamp(
        HikariOceanSpectrumRotate(canonicalMacroSlope, rotation),
        -0.22f.xx,
        0.22f.xx);
    result.detailSlope = clamp(
        HikariOceanSpectrumRotate(canonicalDetailSlope, rotation),
        -0.16f.xx,
        0.16f.xx);
    result.slope = clamp(
        result.macroSlope + result.detailSlope,
        -0.26f.xx,
        0.26f.xx);
    result.jacobian = clamp(
        min(longSlope.z, min(windSlope.z, capillarySlope.z)),
        -1.0f,
        2.0f);
    result.variance = saturate(
        0.035f + shortWeight * 0.08f + capillaryWeight * 0.14f);
    result.connected = 1u;
    return result;
}

#endif
