#ifndef HIKARI_ATMOSPHERE_SCATTERING_HLSLI
#define HIKARI_ATMOSPHERE_SCATTERING_HLSLI

#include "../Lighting/HIKARI_VolumetricScattering.hlsli"

#ifdef HIKARI_ATMOSPHERE_DECLARE_LUT_CB
cbuffer AtmosphereLutCB : register(b0)
{
    float4 gAtmospherePlanetParams;
    float4 gAtmosphereRayleigh;
    float4 gAtmosphereMie;
    float4 gAtmosphereOzone;
    float4 gAtmosphereOzoneParams;
    float4 gAtmosphereSunDirection;
    float4 gAtmosphereSunColorRadiance;
    float4 gAtmosphereGroundColor;
};
#endif

// Shared atmosphere model used by the transmittance, multi-scattering and
// sky-view LUTs as well as the forward sky fallback.  The cbuffer containing
// the gAtmosphere* values is intentionally owned by the including shader so
// each consumer can retain its own binding and constant-buffer layout.

static const float HIKARI_ATMOSPHERE_PI = 3.14159265359f;
static const uint HIKARI_ATMOSPHERE_MAX_OPTICAL_STEPS = 40u;
static const uint HIKARI_ATMOSPHERE_MAX_SKY_STEPS = 24u;
static const uint HIKARI_ATMOSPHERE_MAX_SUN_STEPS = 12u;

struct HikariAtmosphereViewIntegration
{
    float3 scattering;
    float3 transmittance;
    float3 isotropicTransfer;
    float distance;
    float groundDistance;
};

float HikariAtmosphereFiniteOr(float value, float fallback)
{
    return isfinite(value) ? value : fallback;
}

float3 HikariAtmosphereSafeNormalize(float3 value)
{
    const float lengthSquared = dot(value, value);
    return all(isfinite(value)) && lengthSquared > 1.0e-12f
        ? value * rsqrt(lengthSquared)
        : float3(0.0f, 1.0f, 0.0f);
}

float HikariAtmospherePlanetRadius()
{
    return max(1.0f, HikariAtmosphereFiniteOr(gAtmospherePlanetParams.x, 6360000.0f));
}

float HikariAtmosphereTopRadius()
{
    return max(
        HikariAtmospherePlanetRadius() + 1.0f,
        HikariAtmosphereFiniteOr(gAtmospherePlanetParams.y, 6440000.0f));
}

float HikariAtmosphereTopHeight()
{
    return max(1.0f, HikariAtmosphereTopRadius() - HikariAtmospherePlanetRadius());
}

bool HikariAtmosphereIntersectSphere(
    float3 origin,
    float3 direction,
    float radius,
    out float nearDistance,
    out float farDistance)
{
    direction = HikariAtmosphereSafeNormalize(direction);
    const float safeRadius = max(1.0f, HikariAtmosphereFiniteOr(radius, 1.0f));
    const float b = dot(origin, direction);
    const float originRadius = length(origin);
    const float c = (originRadius - safeRadius) * (originRadius + safeRadius);
    const float discriminant = b * b - c;
    if (!isfinite(discriminant) || discriminant <= 0.0f)
    {
        nearDistance = 0.0f;
        farDistance = 0.0f;
        return false;
    }

    const float root = sqrt(max(0.0f, discriminant));
    // 地表付近の短い交点を巨大な半径同士の差から求めない。
    const float q = -b - (b < 0.0f ? -root : root);
    const float other = abs(q) > 1e-12f ? c / q : 0.0f;
    nearDistance = min(q, other);
    farDistance = max(q, other);
    return isfinite(nearDistance) && isfinite(farDistance) &&
        farDistance > 0.0f;
}

float HikariAtmosphereDistanceToTop(float3 origin, float3 direction)
{
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
    if (!HikariAtmosphereIntersectSphere(
            origin,
            direction,
            HikariAtmosphereTopRadius(),
            nearDistance,
            farDistance))
    {
        return 0.0f;
    }

    if (nearDistance > 0.0f)
    {
        return nearDistance;
    }
    return max(0.0f, farDistance);
}

float HikariAtmosphereDistanceToGround(float3 origin, float3 direction)
{
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
    if (!HikariAtmosphereIntersectSphere(
            origin,
            direction,
            HikariAtmospherePlanetRadius(),
            nearDistance,
            farDistance))
    {
        return -1.0f;
    }

    if (nearDistance > 1.0e-3f)
    {
        return nearDistance;
    }
    return farDistance > 1.0e-3f ? farDistance : -1.0f;
}

float HikariAtmosphereHeight(float3 position)
{
    const float radius = length(position);
    return max(0.0f, radius - HikariAtmospherePlanetRadius());
}

void HikariAtmosphereDensities(
    float3 position,
    out float rayleighDensity,
    out float mieDensity,
    out float ozoneDensity)
{
    const float height = HikariAtmosphereHeight(position);
    const float topHeight = HikariAtmosphereTopHeight();
    if (!isfinite(height) || height > topHeight + 1.0f)
    {
        rayleighDensity = 0.0f;
        mieDensity = 0.0f;
        ozoneDensity = 0.0f;
        return;
    }

    const float rayleighScaleHeight = max(
        1.0f,
        HikariAtmosphereFiniteOr(gAtmospherePlanetParams.z, 8000.0f));
    const float mieScaleHeight = max(
        1.0f,
        HikariAtmosphereFiniteOr(gAtmospherePlanetParams.w, 1200.0f));
    const float ozoneWidth = max(
        1.0f,
        HikariAtmosphereFiniteOr(gAtmosphereOzoneParams.x, 15000.0f));
    const float ozoneCenter = max(
        0.0f,
        HikariAtmosphereFiniteOr(gAtmosphereOzone.a, 25000.0f));

    rayleighDensity = exp(-height / rayleighScaleHeight);
    mieDensity = exp(-height / mieScaleHeight);
    ozoneDensity = exp(-abs(height - ozoneCenter) / ozoneWidth);
}

float3 HikariAtmosphereExtinction(
    float3 position,
    out float3 scatteringCoefficient)
{
    float rayleighDensity = 0.0f;
    float mieDensity = 0.0f;
    float ozoneDensity = 0.0f;
    HikariAtmosphereDensities(
        position,
        rayleighDensity,
        mieDensity,
        ozoneDensity);

    const float3 rayleigh = max(0.0f.xxx, gAtmosphereRayleigh.rgb);
    const float3 mie = max(0.0f.xxx, gAtmosphereMie.rgb);
    const float3 ozone = max(0.0f.xxx, gAtmosphereOzone.rgb);
    scatteringCoefficient = rayleigh * rayleighDensity +
        mie * mieDensity;
    return max(
        0.0f.xxx,
        scatteringCoefficient + ozone * ozoneDensity);
}

float HikariAtmosphereRayleighPhase(float cosine)
{
    const float c = clamp(
        HikariAtmosphereFiniteOr(cosine, 0.0f),
        -1.0f,
        1.0f);
    return 3.0f / (16.0f * HIKARI_ATMOSPHERE_PI) * (1.0f + c * c);
}

float HikariAtmosphereHenyeyGreensteinPhase(float cosine, float anisotropy)
{
    const float g = clamp(
        HikariAtmosphereFiniteOr(anisotropy, 0.0f),
        -0.95f,
        0.95f);
    const float c = clamp(
        HikariAtmosphereFiniteOr(cosine, 0.0f),
        -1.0f,
        1.0f);
    const float g2 = g * g;
    const float denominator = max(
        1.0e-4f,
        pow(max(1.0e-4f, 1.0f + g2 - 2.0f * g * c), 1.5f));
    return (1.0f - g2) /
        (4.0f * HIKARI_ATMOSPHERE_PI * denominator);
}

float HikariAtmosphereCornetteShanksPhase(float cosine, float anisotropy)
{
    const float g = clamp(
        HikariAtmosphereFiniteOr(anisotropy, 0.0f),
        -0.95f,
        0.95f);
    const float c = clamp(
        HikariAtmosphereFiniteOr(cosine, 0.0f),
        -1.0f,
        1.0f);
    const float g2 = g * g;
    const float denominator = max(
        1.0e-4f,
        pow(max(1.0e-4f, 1.0f + g2 - 2.0f * g * c), 1.5f));
    return 3.0f * (1.0f - g2) * (1.0f + c * c) /
        (8.0f * HIKARI_ATMOSPHERE_PI * (2.0f + g2) * denominator);
}

float HikariAtmosphereMiePhase(float cosine, float anisotropy)
{
    // Cornette-Shanks carries the forward lobe's angular shape while a small
    // HG contribution keeps the fallback stable for stylized anisotropy.
    return lerp(
        HikariAtmosphereHenyeyGreensteinPhase(cosine, anisotropy),
        HikariAtmosphereCornetteShanksPhase(cosine, anisotropy),
        0.78f);
}

float3 HikariAtmosphereIntegrateOpticalDepth(
    float3 origin,
    float3 direction,
    float distance,
    uint requestedSteps)
{
    const float safeDistance = max(
        0.0f,
        HikariAtmosphereFiniteOr(distance, 0.0f));
    const uint steps = min(
        requestedSteps,
        HIKARI_ATMOSPHERE_MAX_OPTICAL_STEPS);
    if (safeDistance <= 1.0e-3f || steps == 0u)
    {
        return 0.0f.xxx;
    }

    direction = HikariAtmosphereSafeNormalize(direction);
    const float stepLength = safeDistance / max(1.0f, (float)steps);
    float3 opticalDepth = 0.0f.xxx;
    [loop]
    for (uint index = 0u;
         index < HIKARI_ATMOSPHERE_MAX_OPTICAL_STEPS;
         ++index)
    {
        if (index >= steps)
        {
            break;
        }
        const float sampleDistance = ((float)index + 0.5f) * stepLength;
        float3 scatteringCoefficient = 0.0f.xxx;
        const float3 extinction = HikariAtmosphereExtinction(
            origin + direction * sampleDistance,
            scatteringCoefficient);
        opticalDepth += extinction * stepLength;
    }
    return max(0.0f.xxx, opticalDepth);
}

float3 HikariAtmosphereTransmittanceToSpaceWithSteps(
    float3 position,
    float3 direction,
    uint requestedSteps)
{
    direction = HikariAtmosphereSafeNormalize(direction);
    const float distanceToTop = HikariAtmosphereDistanceToTop(
        position,
        direction);
    if (distanceToTop <= 0.0f)
    {
        return 0.0f.xxx;
    }

    const float distanceToGround = HikariAtmosphereDistanceToGround(
        position + direction * 1.0e-2f,
        direction);
    if (distanceToGround > 0.0f && distanceToGround < distanceToTop)
    {
        return 0.0f.xxx;
    }

    return exp(-HikariAtmosphereIntegrateOpticalDepth(
        position,
        direction,
        distanceToTop,
        min(requestedSteps, HIKARI_ATMOSPHERE_MAX_SUN_STEPS)));
}

float3 HikariAtmosphereTransmittanceToSpace(
    float3 position,
    float3 direction)
{
    return HikariAtmosphereTransmittanceToSpaceWithSteps(
        position,
        direction,
        HIKARI_ATMOSPHERE_MAX_SUN_STEPS);
}

// LUT は低高度へ解像度を寄せる。producer と sampler は texel 中心を共有する。
float HikariAtmosphereLutRadius(float heightUnit)
{
    return lerp(HikariAtmospherePlanetRadius() + 0.5f,
        HikariAtmosphereTopRadius() - 0.5f, saturate(heightUnit) * saturate(heightUnit));
}

float2 HikariAtmosphereTransmittanceUvFromRadiusMu(float radius, float cosineToRadialUp)
{
    const float height = saturate((radius - HikariAtmospherePlanetRadius() - 0.5f) /
        max(HikariAtmosphereTopHeight() - 1.0f, 1.0f));
    return float2(saturate(cosineToRadialUp * 0.5f + 0.5f), sqrt(height));
}

float3 HikariAtmosphereSampleLut(Texture2D<float4> lut, SamplerState samplerState,
    float3 position, float3 sunDirection)
{
    uint width, height;
    lut.GetDimensions(width, height);
    const float radius = length(position);
    const float2 unitUv = HikariAtmosphereTransmittanceUvFromRadiusMu(
        radius, dot(position / max(radius, 1.0f), sunDirection));
    const float2 size = float2(width, height);
    return lut.SampleLevel(samplerState, (unitUv * max(size - 1.0f, 0.0f) + 0.5f) / size, 0).rgb;
}

float3 HikariAtmosphereIntegrationSunTransmittance(float3 position, float3 direction, uint steps)
{
#ifdef HIKARI_ATMOSPHERE_USE_TRANSMITTANCE_LUT
    // 地平線の両側を補間して、惑星を透過した太陽光を作らない。
    if (HikariAtmosphereDistanceToGround(position, direction) > 0.0f) return 0.0f.xxx;
    return HikariAtmosphereSampleLut(gAtmosphereTransmittance, gLinearClamp, position, direction);
#else
    return HikariAtmosphereTransmittanceToSpaceWithSteps(position, direction, steps);
#endif
}

// 空と有限距離の aerial perspective が同じ輸送を利用する。
// multiple-scattering producer だけ isotropic=true、太陽色/強度は consumer で一度適用。
HikariAtmosphereViewIntegration HikariAtmosphereIntegrateSegment(
    float3 origin, float3 direction, float3 sunDirection,
    uint requestedViewSteps, uint requestedSunSteps, float maxDistance, bool isotropic)
{
    HikariAtmosphereViewIntegration result = (HikariAtmosphereViewIntegration)0;
    result.transmittance = 1.0f.xxx;
    result.groundDistance = -1.0f;
    direction = HikariAtmosphereSafeNormalize(direction);
    sunDirection = HikariAtmosphereSafeNormalize(sunDirection);
    const float distanceToTop = HikariAtmosphereDistanceToTop(origin, direction);
    if (distanceToTop <= 0.0f) return result;
    const float ground = HikariAtmosphereDistanceToGround(origin, direction);
    const bool hitsGround = ground > 0.0f && ground < distanceToTop && ground <= maxDistance;
    const float distance = min(max(maxDistance, 0.0f), hitsGround ? ground : distanceToTop);
    const uint steps = min(requestedViewSteps, HIKARI_ATMOSPHERE_MAX_SKY_STEPS);
    if (distance <= 0.0f || steps == 0u) return result;

    const float cosine = dot(direction, sunDirection);
    const float rayleighPhase = isotropic ? 1.0f / (4.0f * HIKARI_ATMOSPHERE_PI) :
        HikariAtmosphereRayleighPhase(cosine);
    const float miePhase = isotropic ? rayleighPhase : HikariAtmosphereMiePhase(cosine, gAtmosphereMie.a);
    [loop]
    for (uint index = 0u; index < steps; ++index)
    {
        // 地表の密度勾配を少数サンプルで保持する。区間積分は Beer-Lambert。
        const float a = float(index) / float(steps);
        const float b = float(index + 1u) / float(steps);
        const float stepLength = distance * (b*b - a*a);
        const float3 position = origin + direction * (distance * 0.5f * (a*a + b*b));
        float rayleighDensity, mieDensity, ozoneDensity;
        HikariAtmosphereDensities(position, rayleighDensity, mieDensity, ozoneDensity);
        const float3 rayleigh = max(gAtmosphereRayleigh.rgb, 0.0f.xxx) * rayleighDensity;
        const float3 mie = max(gAtmosphereMie.rgb, 0.0f.xxx) * mieDensity;
        const float3 scattering = rayleigh + mie;
        const float3 extinction = scattering + max(gAtmosphereOzone.rgb, 0.0f.xxx) * ozoneDensity;
        const float3 source = (rayleigh * rayleighPhase + mie * miePhase) *
            HikariAtmosphereIntegrationSunTransmittance(position, sunDirection, requestedSunSteps);
        const float3 weight = result.transmittance * HikariExtinctionIntegral(extinction, stepLength);
        result.scattering += weight * source;
        result.isotropicTransfer += weight * scattering;
#ifdef HIKARI_ATMOSPHERE_USE_MULTISCATTERING_LUT
        result.scattering += weight * scattering *
            HikariAtmosphereSampleLut(gAtmosphereMultiScattering, gLinearClamp, position, sunDirection);
#endif
        result.transmittance *= exp(-extinction * stepLength);
    }
    result.transmittance = saturate(result.transmittance);
    result.distance = distance;
    result.groundDistance = hitsGround ? ground : -1.0f;
    return result;
}

HikariAtmosphereViewIntegration HikariAtmosphereIntegrateViewWithSteps(
    float3 origin, float3 direction, float3 sunDirection,
    uint requestedViewSteps, uint requestedSunSteps)
{
    return HikariAtmosphereIntegrateSegment(origin, direction, sunDirection,
        requestedViewSteps, requestedSunSteps, 1e30f, false);
}

HikariAtmosphereViewIntegration HikariAtmosphereIntegrateView(
    float3 origin,
    float3 direction,
    float3 sunDirection)
{
    return HikariAtmosphereIntegrateViewWithSteps(
        origin,
        direction,
        sunDirection,
        HIKARI_ATMOSPHERE_MAX_SKY_STEPS,
        HIKARI_ATMOSPHERE_MAX_SUN_STEPS);
}

float3 HikariAtmosphereEvaluateSkyRadianceWithSteps(
    float3 origin,
    float3 direction,
    float3 groundColor,
    float groundAlbedo,
    float aerialPerspectiveStrength,
    uint requestedViewSteps,
    uint requestedSunSteps)
{
    const float3 sunDirection = HikariAtmosphereSafeNormalize(
        gAtmosphereSunDirection.xyz);
    const HikariAtmosphereViewIntegration integration =
        HikariAtmosphereIntegrateViewWithSteps(
            origin,
            direction,
            sunDirection,
            requestedViewSteps,
            requestedSunSteps);
    const float sourceRadiance = max(
        0.0f,
        HikariAtmosphereFiniteOr(gAtmosphereSunColorRadiance.a, 0.0f));
    const float aerialStrength = max(
        0.0f,
        HikariAtmosphereFiniteOr(aerialPerspectiveStrength, 1.0f));
    float3 radiance = integration.scattering * max(gAtmosphereSunColorRadiance.rgb, 0.0f.xxx) * sourceRadiance * aerialStrength;

    if (integration.groundDistance > 0.0f)
    {
        const float3 hitPosition = origin +
            HikariAtmosphereSafeNormalize(direction) *
            integration.groundDistance;
        const float3 groundNormal = HikariAtmosphereSafeNormalize(hitPosition);
        const float sunCosine = saturate(dot(groundNormal, sunDirection));
        const float3 groundSunTransmittance =
            HikariAtmosphereIntegrationSunTransmittance(
                hitPosition + groundNormal * 0.5f,
                sunDirection, requestedSunSteps);
        const float3 bounce = saturate(groundColor) *
            saturate(HikariAtmosphereFiniteOr(groundAlbedo, 0.1f)) *
            sunCosine * groundSunTransmittance *
            integration.transmittance / HIKARI_ATMOSPHERE_PI;
        radiance += bounce * max(0.0f.xxx, gAtmosphereSunColorRadiance.rgb) * sourceRadiance * aerialStrength;
    }

    if (!all(isfinite(radiance)))
    {
        return 0.0f.xxx;
    }
    return max(0.0f.xxx, radiance);
}

float3 HikariAtmosphereEvaluateSkyRadiance(
    float3 origin,
    float3 direction,
    float3 groundColor,
    float groundAlbedo,
    float aerialPerspectiveStrength)
{
    return HikariAtmosphereEvaluateSkyRadianceWithSteps(
        origin,
        direction,
        groundColor,
        groundAlbedo,
        aerialPerspectiveStrength,
        HIKARI_ATMOSPHERE_MAX_SKY_STEPS,
        HIKARI_ATMOSPHERE_MAX_SUN_STEPS);
}

float HikariAtmosphereObserverSunTransmittance()
{
    // OzoneParams.z is the observer-side solar transmittance supplied by the
    // renderer. It is intentionally not folded into sky single scattering.
    return saturate(HikariAtmosphereFiniteOr(
        gAtmosphereOzoneParams.z,
        1.0f));
}

float3 HikariAtmosphereEvaluateSunDisk(
    float3 direction,
    float3 observerRadiance)
{
    const float3 sunDirection = HikariAtmosphereSafeNormalize(
        gAtmosphereSunDirection.xyz);
    const float radius = clamp(
        HikariAtmosphereFiniteOr(gAtmosphereRayleigh.a, 0.004675f),
        1.0e-5f,
        0.5f);
    const float cosine = dot(
        HikariAtmosphereSafeNormalize(direction),
        sunDirection);
    const float innerCosine = cos(radius);
    const float outerCosine = cos(radius * 1.45f);
    const float disk = smoothstep(outerCosine, innerCosine, cosine);
    const float3 radiance = max(0.0f.xxx, observerRadiance) *
        HikariAtmosphereObserverSunTransmittance();
    return disk * radiance;
}

float3 HikariAtmospherePositionFromRadius(float radius)
{
    return float3(0.0f, max(HikariAtmospherePlanetRadius(), radius), 0.0f);
}

float3 HikariAtmosphereDirectionFromMu(float cosineToRadialUp)
{
    const float mu = clamp(cosineToRadialUp, -1.0f, 1.0f);
    const float horizontal = sqrt(max(0.0f, 1.0f - mu * mu));
    return HikariAtmosphereSafeNormalize(float3(horizontal, mu, 0.0f));
}

#endif
