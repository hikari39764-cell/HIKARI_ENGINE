#ifndef HIKARI_ATMOSPHERE_SCATTERING_HLSLI
#define HIKARI_ATMOSPHERE_SCATTERING_HLSLI

// Shared atmosphere model used by the transmittance, multi-scattering and
// sky-view LUTs as well as the forward sky fallback.  The cbuffer containing
// the gAtmosphere* values is intentionally owned by the including shader so
// the existing seven-float4 ABI and descriptor layout remain unchanged.

static const float HIKARI_ATMOSPHERE_PI = 3.14159265359f;
static const uint HIKARI_ATMOSPHERE_MAX_OPTICAL_STEPS = 40u;
static const uint HIKARI_ATMOSPHERE_MAX_SKY_STEPS = 24u;
static const uint HIKARI_ATMOSPHERE_MAX_SUN_STEPS = 12u;

struct HikariAtmosphereViewIntegration
{
    float3 scattering;
    float3 transmittance;
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
    const float c = dot(origin, origin) - safeRadius * safeRadius;
    const float discriminant = b * b - c;
    if (!isfinite(discriminant) || discriminant <= 0.0f)
    {
        nearDistance = 0.0f;
        farDistance = 0.0f;
        return false;
    }

    const float root = sqrt(max(0.0f, discriminant));
    nearDistance = -b - root;
    farDistance = -b + root;
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

float3 HikariAtmosphereIntegrateLowOrderScattering(
    float3 origin,
    float3 direction,
    float3 sunDirection,
    uint requestedSteps)
{
    direction = HikariAtmosphereSafeNormalize(direction);
    sunDirection = HikariAtmosphereSafeNormalize(sunDirection);
    const float distanceToTop = HikariAtmosphereDistanceToTop(
        origin,
        direction);
    if (distanceToTop <= 0.0f)
    {
        return 0.0f.xxx;
    }

    const float distanceToGround = HikariAtmosphereDistanceToGround(
        origin + direction * 1.0e-2f,
        direction);
    const float viewDistance = distanceToGround > 0.0f &&
        distanceToGround < distanceToTop
        ? distanceToGround
        : distanceToTop;
    const uint steps = min(requestedSteps, HIKARI_ATMOSPHERE_MAX_SKY_STEPS);
    if (viewDistance <= 1.0e-3f || steps == 0u)
    {
        return 0.0f.xxx;
    }

    const float stepLength = viewDistance / max(1.0f, (float)steps);
    const float cosine = dot(direction, sunDirection);
    const float rayleighPhase = HikariAtmosphereRayleighPhase(cosine);
    const float miePhase = HikariAtmosphereMiePhase(
        cosine,
        gAtmosphereMie.a);
    float3 result = 0.0f.xxx;
    float viewTransmittance = 1.0f;
    [loop]
    for (uint index = 0u;
         index < HIKARI_ATMOSPHERE_MAX_SKY_STEPS;
         ++index)
    {
        if (index >= steps)
        {
            break;
        }
        const float sampleDistance = ((float)index + 0.5f) * stepLength;
        const float3 position = origin + direction * sampleDistance;
        float rayleighDensity = 0.0f;
        float mieDensity = 0.0f;
        float ozoneDensity = 0.0f;
        HikariAtmosphereDensities(
            position,
            rayleighDensity,
            mieDensity,
            ozoneDensity);
        const float3 sunTransmittance = HikariAtmosphereTransmittanceToSpaceWithSteps(
            position,
            sunDirection,
            min(requestedSteps, HIKARI_ATMOSPHERE_MAX_SUN_STEPS));
        const float3 scattering = (
            max(0.0f.xxx, gAtmosphereRayleigh.rgb) *
                rayleighDensity * rayleighPhase +
            max(0.0f.xxx, gAtmosphereMie.rgb) *
                mieDensity * miePhase) * sunTransmittance;
        result += scattering * viewTransmittance * stepLength;
        float3 scatteringCoefficient = 0.0f.xxx;
        const float3 extinction = HikariAtmosphereExtinction(
            position,
            scatteringCoefficient);
        viewTransmittance *= exp(-max(0.0f, dot(extinction, 0.3333333f.xxx) * stepLength));
    }
    return max(0.0f.xxx, result);
}

HikariAtmosphereViewIntegration HikariAtmosphereIntegrateViewWithSteps(
    float3 origin,
    float3 direction,
    float3 sunDirection,
    uint requestedViewSteps,
    uint requestedSunSteps)
{
    HikariAtmosphereViewIntegration result;
    result.scattering = 0.0f.xxx;
    result.transmittance = 1.0f.xxx;
    result.distance = 0.0f;
    result.groundDistance = -1.0f;

    direction = HikariAtmosphereSafeNormalize(direction);
    sunDirection = HikariAtmosphereSafeNormalize(sunDirection);
    const float distanceToTop = HikariAtmosphereDistanceToTop(
        origin,
        direction);
    if (distanceToTop <= 0.0f)
    {
        return result;
    }

    const float distanceToGround = HikariAtmosphereDistanceToGround(
        origin + direction * 1.0e-2f,
        direction);
    const bool hitsGround = distanceToGround > 0.0f &&
        distanceToGround < distanceToTop;
    const float viewDistance = hitsGround
        ? distanceToGround
        : distanceToTop;
    if (viewDistance <= 1.0e-3f)
    {
        result.groundDistance = hitsGround ? distanceToGround : -1.0f;
        return result;
    }

    const uint steps = min(
        requestedViewSteps,
        HIKARI_ATMOSPHERE_MAX_SKY_STEPS);
    if (steps == 0u)
    {
        return result;
    }
    const float stepLength = viewDistance / (float)steps;
    const float cosine = dot(direction, sunDirection);
    const float rayleighPhase = HikariAtmosphereRayleighPhase(cosine);
    const float miePhase = HikariAtmosphereMiePhase(
        cosine,
        gAtmosphereMie.a);
    float3 throughput = 1.0f.xxx;
    [loop]
    for (uint index = 0u;
         index < HIKARI_ATMOSPHERE_MAX_SKY_STEPS;
         ++index)
    {
        if (index >= steps)
        {
            break;
        }
        const float sampleDistance = ((float)index + 0.5f) * stepLength;
        const float3 position = origin + direction * sampleDistance;
        float3 scatteringCoefficient = 0.0f.xxx;
        const float3 extinction = HikariAtmosphereExtinction(
            position,
            scatteringCoefficient);
        float rayleighDensity = 0.0f;
        float mieDensity = 0.0f;
        float ozoneDensity = 0.0f;
        HikariAtmosphereDensities(
            position,
            rayleighDensity,
            mieDensity,
            ozoneDensity);
        const float3 sunTransmittance = HikariAtmosphereTransmittanceToSpaceWithSteps(
            position,
            sunDirection,
            min(requestedSunSteps, HIKARI_ATMOSPHERE_MAX_SUN_STEPS));
        const float3 inScatter = (
            max(0.0f.xxx, gAtmosphereRayleigh.rgb) *
                rayleighDensity * rayleighPhase +
            max(0.0f.xxx, gAtmosphereMie.rgb) *
                mieDensity * miePhase) * sunTransmittance;
        result.scattering += throughput * inScatter * stepLength;
        throughput *= exp(-max(0.0f.xxx, extinction * stepLength));
    }

    result.transmittance = saturate(throughput);
    result.distance = viewDistance;
    result.groundDistance = hitsGround ? distanceToGround : -1.0f;
    return result;
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
    float3 radiance = integration.scattering * sourceRadiance * aerialStrength;

    if (integration.groundDistance > 0.0f)
    {
        const float3 hitPosition = origin +
            HikariAtmosphereSafeNormalize(direction) *
            integration.groundDistance;
        const float3 groundNormal = HikariAtmosphereSafeNormalize(hitPosition);
        const float sunCosine = saturate(dot(groundNormal, sunDirection));
        const float3 groundSunTransmittance =
            HikariAtmosphereTransmittanceToSpace(
                hitPosition + groundNormal * 0.5f,
                sunDirection);
        const float3 bounce = max(0.0f.xxx, groundColor) *
            saturate(HikariAtmosphereFiniteOr(groundAlbedo, 0.1f)) *
            sunCosine * groundSunTransmittance *
            integration.transmittance * 0.18f;
        radiance += bounce * aerialStrength;
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

float2 HikariAtmosphereTransmittanceUvFromRadiusMu(
    float radius,
    float cosineToRadialUp)
{
    // The transmittance LUT stores the physical pair (r, mu): X is mu for
    // the high-resolution angular axis, Y is normalized radius.
    const float radiusUv = saturate((radius - HikariAtmospherePlanetRadius()) /
        HikariAtmosphereTopHeight());
    const float muUv = saturate(cosineToRadialUp * 0.5f + 0.5f);
    return float2(muUv, radiusUv);
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
