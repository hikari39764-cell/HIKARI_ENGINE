cbuffer SkyCB : register(b0)
{
    float4x4 gSkyInverseViewProj;
    float4 gSkyZenithExposure;      // rgb zenith, a exposure
    float4 gSkyHorizonPower;        // rgb horizon, a horizon power
    float4 gSkyGroundYaw;           // rgb ground, a yaw
    float4 gSkyTintMode;            // rgb tint, a SkyMode
    float4 gSkySunDirectionIntensity;
    float4 gSkySunSizeParams;       // x sun size, w directional intensity
};

cbuffer AtmosphereCB : register(b1)
{
    float4 gAtmospherePlanetParams;       // radius, top radius, Rayleigh H, Mie H
    float4 gAtmosphereRayleigh;           // scattering RGB, sun angular radius
    float4 gAtmosphereMie;                // scattering RGB, anisotropy
    float4 gAtmosphereOzone;              // absorption RGB, center height
    float4 gAtmosphereOzoneParams;        // width, aerial, observer sun T, albedo
    float4 gAtmosphereSunDirection;       // authoritative direction, enabled/LUT flag
    float4 gAtmosphereSunColorRadiance;   // source color, source radiance
};

Texture2D gSkyTex2D : register(t0);
TextureCube gSkyCube : register(t1);
Texture2D gAtmosphereSkyView : register(t2);
SamplerState gLinearClamp : register(s0);

#include "Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"
#include "Include/Sky/HIKARI_EquirectangularSampling.hlsli"
#include "Include/Sky/HIKARI_SkyRadiance.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

float3 ReconstructWorldRay(float2 ndc)
{
    float4 worldNear = mul(
        gSkyInverseViewProj,
        float4(ndc, 0.0f, 1.0f));
    float4 worldFar = mul(
        gSkyInverseViewProj,
        float4(ndc, 1.0f, 1.0f));
    if (abs(worldNear.w) <= 1.0e-6f ||
        abs(worldFar.w) <= 1.0e-6f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }

    float3 direction = worldFar.xyz / worldFar.w -
        worldNear.xyz / worldNear.w;
    float lengthSquared = dot(direction, direction);
    if (!all(isfinite(direction)) || lengthSquared <= 1.0e-8f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    return direction * rsqrt(lengthSquared);
}

float3 AddSunDisk(float3 color, float3 dir)
{
    float intensity = max(0.0f, gSkySunDirectionIntensity.w);
    if (intensity <= 0.0f)
    {
        return color;
    }

    float3 sunVector = -gSkySunDirectionIntensity.xyz;
    float sunLengthSquared = dot(sunVector, sunVector);
    if (!all(isfinite(sunVector)) || sunLengthSquared <= 1.0e-8f)
    {
        return color;
    }
    float3 sunDir = sunVector * rsqrt(sunLengthSquared);
    float size = max(0.0001f, gSkySunSizeParams.x);
    float sun = smoothstep(
        1.0f - size,
        1.0f,
        dot(normalize(dir), sunDir));
    return color + sun * intensity;
}

float3 EvaluateAtmosphereSky(float3 dir)
{
    const float3 observer = float3(
        0.0f,
        HikariAtmospherePlanetRadius() + 1.0f,
        0.0f);
    return HikariAtmosphereEvaluateSkyRadiance(
        observer,
        dir,
        gSkyGroundYaw.rgb,
        gAtmosphereOzoneParams.w,
        gAtmosphereOzoneParams.y);
}

float3 AddAtmosphereSunDisk(float3 color, float3 dir)
{
    // SkyCB owns the narrow disk's observer radiance and show/hide policy;
    // the shared atmosphere helper applies the observer-side transmittance
    // from OzoneParams.z exactly once.
    const float observerDiskIntensity = max(
        0.0f,
        HikariAtmosphereFiniteOr(gSkySunDirectionIntensity.w, 0.0f));
    return color + HikariAtmosphereEvaluateSunDisk(
        dir,
        max(0.0f.xxx, gAtmosphereSunColorRadiance.rgb) *
            observerDiskIntensity);
}

float3 SampleAtmosphereSkyView(float3 dir)
{
    dir = HikariAtmosphereSafeNormalize(dir);
    const float2 uv = float2(
        atan2(dir.z, dir.x) * 0.15915494309f + 0.5f,
        saturate(0.5f * dir.y + 0.5f));
    return gAtmosphereSkyView.SampleLevel(gLinearClamp, uv, 0.0f).rgb;
}

float4 main(PSInput input) : SV_TARGET
{
    float3 worldRayDir = ReconstructWorldRay(input.ndc);
    float3 skySampleDir = normalize(HikariRotateSkyYaw(worldRayDir, gSkyGroundYaw.a));
    uint mode = (uint)(gSkyTintMode.a + 0.5f);

    float3 sky = HikariEvaluateGradientSky(skySampleDir, gSkyZenithExposure.rgb,
        gSkyHorizonPower.rgb, gSkyGroundYaw.rgb, gSkyHorizonPower.a);
    if (mode == 4u && gAtmosphereSunDirection.w > 0.5f)
    {
        sky = gAtmosphereSunDirection.w > 1.5f
            ? SampleAtmosphereSkyView(worldRayDir)
            : EvaluateAtmosphereSky(worldRayDir);
        sky = AddAtmosphereSunDisk(sky, worldRayDir);
    }
    else if (mode == 2u)
    {
        sky = gSkyCube.Sample(gLinearClamp, skySampleDir).rgb;
    }
    else if (mode == 3u)
    {
        const float2 panoramaUv =
            HikariDirectionToEquirectangularUv(skySampleDir);
        float2 panoramaDerivativeX;
        float2 panoramaDerivativeY;
        HikariResolveEquirectangularGradients(
            skySampleDir,
            panoramaUv,
            panoramaDerivativeX,
            panoramaDerivativeY);
        sky = gSkyTex2D.SampleGrad(
            gLinearClamp,
            panoramaUv,
            panoramaDerivativeX,
            panoramaDerivativeY).rgb;
    }

    if (mode != 4u)
    {
        sky = AddSunDisk(sky, skySampleDir);
    }
    sky = HikariApplySkyRadianceScale(sky, gSkyTintMode.rgb, gSkyZenithExposure.a);
    if (!all(isfinite(sky)))
    {
        sky = 0.0f.xxx;
    }
    sky = max(sky, 0.0f.xxx);
    return float4(sky, 1.0f);
}
