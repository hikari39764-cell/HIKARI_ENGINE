#ifndef HIKARI_STYLIZED_CLOUD_VOLUME_INCLUDED
#define HIKARI_STYLIZED_CLOUD_VOLUME_INCLUDED

#ifndef HIKARI_NOISE_INCLUDED
#include "Include/Math/HIKARI_Noise.hlsli"
#endif

// Renderer-owned, texture-backed cloud volume helpers.
//
// Shared noise math lives in Math/HIKARI_Noise.hlsli.
// This file owns the cloud density model.

static const float HIKARI_CLOUD_PI = 3.14159265359f;
static const float HIKARI_CLOUD_TWO_PI = 6.28318530718f;
static const float HIKARI_CLOUD_EPSILON = 1.0e-5f;
static const float HIKARI_CLOUD_DENSITY_THRESHOLD = 0.001f;
static const float HIKARI_CLOUD_TRANSMITTANCE_EPSILON = 0.003f;
static const uint HIKARI_CLOUD_MAX_PRIMARY_STEPS = 64u;
static const uint HIKARI_CLOUD_MAX_LIGHT_STEPS = 8u;

struct HikariStylizedCloudDensityTerms
{
    float heightFraction;
    float baseDensity;
    float density;
    float weatherCoverage;
    float shapeValue;
    float detailValue;
};

struct HikariStylizedCloudShellInterval
{
    float nearDistance;
    float farDistance;
    float valid;
};

float3 HikariStylizedCloudSafeNormalize(
    float3 value,
    float3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > HIKARI_CLOUD_EPSILON
        ? value * rsqrt(lengthSquared)
        : fallback;
}

float2 HikariStylizedCloudWindDirection(float2 wind)
{
    float lengthSquared = dot(wind, wind);
    return lengthSquared > HIKARI_CLOUD_EPSILON
        ? wind * rsqrt(lengthSquared)
        : float2(1.0f, 0.0f);
}

float HikariStylizedCloudHeightProfile(
    float heightFraction,
    float softness,
    float cloudType)
{
    // A compact LUT-style profile: all clouds share a clean, comparatively
    // flat base, while the weather type controls how tall and rounded the
    // upper body becomes.  This follows the mature density-profile/shaping/
    // edge-erosion split without requiring another authored texture.
    float height = saturate(heightFraction);
    float safeSoftness = saturate(softness);
    float type = saturate(cloudType);
    float baseWidth = lerp(0.025f, 0.085f, safeSoftness);
    float base = smoothstep(0.0f, baseWidth, height);
    float topStart = lerp(0.48f, 0.70f, type);
    float topWidth = lerp(0.20f, 0.32f, safeSoftness);
    float top = 1.0f - smoothstep(
        topStart,
        min(topStart + topWidth, 0.995f),
        height);
    float roundedShoulder = smoothstep(0.08f, 0.32f, height) *
        (1.0f - smoothstep(0.68f, 0.94f, height));
    return saturate(base * top * lerp(0.88f, 1.0f, roundedShoulder * type));
}

HikariStylizedCloudShellInterval HikariStylizedCloudEmptyShellInterval()
{
    HikariStylizedCloudShellInterval result;
    result.nearDistance = 0.0f;
    result.farDistance = 0.0f;
    result.valid = 0.0f;
    return result;
}

bool HikariStylizedCloudRaySphereRoots(
    float3 rayOrigin,
    float3 rayDirection,
    float radius,
    out float nearDistance,
    out float farDistance)
{
    nearDistance = 0.0f;
    farDistance = 0.0f;
    float safeRadius = max(radius, 0.001f);
    float b = dot(rayOrigin, rayDirection);
    // 大半径の差の二乗を直接引くと、薄い雲層の距離精度を失う。
    float originRadius = length(rayOrigin);
    float c = (originRadius-safeRadius)*(originRadius+safeRadius);
    float discriminant = b * b - c;
    if (discriminant < 0.0f)
    {
        return false;
    }

    float root = sqrt(max(discriminant, 0.0f));
    float q = -b-(b >= 0.0f ? root : -root);
    float other = abs(q) > HIKARI_CLOUD_EPSILON ? c/q : 0.0f;
    nearDistance = min(q,other);
    farDistance = max(q,other);
    return true;
}

bool HikariStylizedCloudRaySphere(float3 origin, float3 direction, float radius, out float nearDistance, out float farDistance)
{
    return HikariStylizedCloudRaySphereRoots(origin,direction,radius,nearDistance,farDistance) && farDistance>0;
}

HikariStylizedCloudShellInterval HikariStylizedCloudResolveShell(
    float3 rayOrigin,
    float3 rayDirection,
    float planetRadius,
    float cloudAltitude,
    float cloudThickness,
    float maxDistance)
{
    HikariStylizedCloudShellInterval result =
        HikariStylizedCloudEmptyShellInterval();

    float innerRadius = max(planetRadius + cloudAltitude, 0.01f);
    float outerRadius = max(innerRadius + cloudThickness, innerRadius + 0.01f);
    float outerNear;
    float outerFar;
    if (!HikariStylizedCloudRaySphere(
            rayOrigin,
            rayDirection,
            outerRadius,
            outerNear,
            outerFar))
    {
        return result;
    }

    float innerNear;
    float innerFar;
    bool innerHit = HikariStylizedCloudRaySphere(
        rayOrigin,
        rayDirection,
        innerRadius,
        innerNear,
        innerFar);
    float cameraRadius = length(rayOrigin);

    float shellNear = max(outerNear, 0.0f);
    float shellFar = outerFar;
    if (cameraRadius < innerRadius)
    {
        shellNear = max(innerFar, 0.0f);
    }
    else if (cameraRadius <= outerRadius)
    {
        shellNear = 0.0f;
        if (innerHit && innerNear > 0.0f)
        {
            shellFar = min(shellFar, innerNear);
        }
    }
    else if (innerHit && innerNear > shellNear)
    {
        shellFar = min(shellFar, innerNear);
    }

    shellNear = max(shellNear, 0.0f);
    shellFar = min(shellFar, max(maxDistance, 1.0f));
    if (shellFar <= shellNear + 0.001f)
    {
        return result;
    }

    result.nearDistance = shellNear;
    result.farDistance = shellFar;
    result.valid = 1.0f;
    return result;
}

HikariStylizedCloudDensityTerms HikariStylizedCloudBuildDensityTerms(
    float coverage,
    float densityScale,
    float shapeDetail,
    float softness,
    float heightFraction,
    float4 weatherSample,
    float4 shapeSample,
    float4 detailSample,
    float macroMultiplier,
    float detailMultiplier)
{
    HikariStylizedCloudDensityTerms result;
    result.heightFraction = saturate(heightFraction);

    float weatherCoverage = saturate(
        weatherSample.r * 0.82f + weatherSample.g * 0.18f);
    // The authored coverage is a soft weather threshold, not a binary
    // occupancy gate.  This keeps the TestWG .342 preset in the medium-block
    // range while still making zero/low coverage visibly sparse.
    float coverageThreshold = lerp(0.64f, 0.22f, saturate(coverage));
    float weatherWidth = lerp(0.10f, 0.23f, saturate(softness));
    float weatherMask = smoothstep(
        coverageThreshold - weatherWidth,
        coverageThreshold + weatherWidth,
        weatherCoverage);

    float cloudType = saturate(weatherSample.g);
    float heightMask = HikariStylizedCloudHeightProfile(
        result.heightFraction,
        softness,
        cloudType);
    float shapeValue = saturate(
        shapeSample.r * 0.52f +
        shapeSample.g * 0.34f +
        shapeSample.b * 0.14f);
    // Cellular centers contribute positively so the macro field forms
    // rounded cauliflower lobes instead of carving boxy inverse cells.
    shapeValue = saturate(shapeValue +
        smoothstep(0.12f, 0.62f, result.heightFraction) *
        cloudType * 0.055f);
    float shapeWidth = lerp(0.065f, 0.16f, saturate(softness));
    float macroMask = smoothstep(
        0.39f - shapeWidth,
        0.61f + shapeWidth,
        saturate(shapeValue * max(macroMultiplier, 0.25f)));

    float detailValue = saturate(
        detailSample.r * 0.52f +
        detailSample.g * 0.32f +
        detailSample.b * 0.16f);
    float erosionWeight = saturate(shapeDetail * max(detailMultiplier, 0.0f));
    float edgeSusceptibility = 1.0f - smoothstep(
        0.42f,
        0.90f,
        macroMask);
    float erosionSignal = smoothstep(0.18f, 0.78f, detailValue);
    float erosion = lerp(
        1.0f,
        erosionSignal,
        erosionWeight * edgeSusceptibility * 0.72f);

    float rainSoftening = lerp(1.0f, 0.86f, saturate(weatherSample.b));
    result.weatherCoverage = weatherCoverage;
    result.shapeValue = shapeValue;
    result.detailValue = detailValue;
    result.baseDensity = saturate(
        weatherMask * heightMask * macroMask * rainSoftening);
    result.density = saturate(
        result.baseDensity * erosion *
        lerp(0.94f, 1.08f, cloudType) *
        max(densityScale, 0.0f));
    return result;
}

float HikariStylizedCloudValueBand(float lightAccess)
{
    float lower = smoothstep(0.16f, 0.44f, lightAccess);
    float upper = smoothstep(0.56f, 0.86f, lightAccess);
    return saturate(0.15f + lower * 0.45f + upper * 0.34f);
}

#endif // HIKARI_STYLIZED_CLOUD_VOLUME_INCLUDED
