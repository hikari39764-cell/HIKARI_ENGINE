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
    float4 gAtmosphereOzoneParams;        // width, sun scale, exposure, albedo
    float4 gAtmosphereSunDirection;       // authoritative direction, enabled/LUT flag
    float4 gAtmosphereSunColorRadiance;   // authoritative color, radiance
};

Texture2D gSkyTex2D : register(t0);
TextureCube gSkyCube : register(t1);
Texture2D gAtmosphereSkyView : register(t2);
SamplerState gLinearClamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

float3 RotateYaw(float3 dir, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);
    return float3(dir.x * c - dir.z * s, dir.y, dir.x * s + dir.z * c);
}

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

float2 DirectionToLatLongUv(float3 dir)
{
    const float invPi = 0.31830988618f;
    const float invTwoPi = 0.15915494309f;
    float u = atan2(dir.z, dir.x) * invTwoPi + 0.5f;
    float v = asin(clamp(dir.y, -1.0f, 1.0f)) * -invPi + 0.5f;
    return float2(u, v);
}

float3 SampleGradient(float3 dir)
{
    // Both hemispheres start from the exact same authored horizon color.
    // The previous 0..1 remap evaluated each branch at its midpoint when
    // dir.y == 0, producing a visible discontinuity around an open ocean.
    float elevation = saturate(abs(dir.y));
    float gradientPosition = smoothstep(0.0f, 1.0f, elevation);
    float3 hemisphereTarget = dir.y >= 0.0f
        ? gSkyZenithExposure.rgb
        : gSkyGroundYaw.rgb;
    float3 color = lerp(
        gSkyHorizonPower.rgb,
        hemisphereTarget,
        gradientPosition);
    float horizonBand = pow(
        saturate(1.0f - elevation),
        max(0.01f, gSkyHorizonPower.a));
    return lerp(color, gSkyHorizonPower.rgb, horizonBand * 0.12f);
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

float3 AtmosphereSafeNormalize(float3 value)
{
    const float lengthSquared = dot(value, value);
    return all(isfinite(value)) && lengthSquared > 1.0e-8f
        ? value * rsqrt(lengthSquared)
        : float3(0.0f, 1.0f, 0.0f);
}

float3 EvaluateAtmosphereSky(float3 dir)
{
    dir = AtmosphereSafeNormalize(dir);
    const float3 sunDirection = AtmosphereSafeNormalize(
        gAtmosphereSunDirection.xyz);
    const float altitude = max(0.0f, dir.y) *
        max(1.0f, gAtmospherePlanetParams.y -
            gAtmospherePlanetParams.x);
    const float rayleighDensity = exp(
        -altitude / max(1.0f, gAtmospherePlanetParams.z));
    const float mieDensity = exp(
        -altitude / max(1.0f, gAtmospherePlanetParams.w));
    const float ozoneBand = exp(
        -abs(altitude - gAtmosphereOzone.a) /
        max(1.0f, gAtmosphereOzoneParams.x));
    const float sunCosine = dot(dir, sunDirection);
    const float rayleighPhase = 0.05968310366f *
        (1.0f + sunCosine * sunCosine);
    const float mieG = clamp(gAtmosphereMie.a, -0.95f, 0.95f);
    const float miePhase = 0.07957747155f *
        (1.0f - mieG * mieG) /
        pow(max(1.0e-3f, 1.0f + mieG * mieG -
            2.0f * mieG * sunCosine), 1.5f);
    const float3 extinction =
        gAtmosphereRayleigh.rgb * rayleighDensity +
        gAtmosphereMie.rgb * mieDensity +
        gAtmosphereOzone.rgb * ozoneBand;
    const float pathLength = lerp(0.12f, 1.0f, saturate(1.0f - dir.y)) *
        max(1.0f, gAtmospherePlanetParams.y -
            gAtmospherePlanetParams.x);
    const float3 transmittance = exp(-max(0.0f.xxx, extinction * pathLength));
    const float3 integratedScattering =
        1.0f.xxx - exp(-max(0.0f.xxx, extinction * pathLength));
    const float aerialPerspectiveStrength =
        max(0.0f, gAtmosphereOzoneParams.y);
    const float3 scatteringShare =
        (gAtmosphereRayleigh.rgb * rayleighDensity * rayleighPhase +
         gAtmosphereMie.rgb * mieDensity * miePhase) /
        max(extinction, 1.0e-6f.xxx);
    const float3 singleScattering =
        gAtmosphereSunColorRadiance.rgb *
        max(0.0f, gAtmosphereSunColorRadiance.a) *
        integratedScattering * scatteringShare * transmittance *
        aerialPerspectiveStrength;
    const float horizon = pow(saturate(1.0f - abs(dir.y)), 0.35f);
    const float3 groundBounce = gSkyGroundYaw.rgb *
        max(0.0f, gAtmosphereOzoneParams.a) *
        (0.12f + 0.28f * horizon);
    return max(0.0f.xxx, singleScattering + groundBounce);
}

float3 AddAtmosphereSunDisk(float3 color, float3 dir)
{
    const float3 sunDirection = AtmosphereSafeNormalize(
        gAtmosphereSunDirection.xyz);
    const float radius = max(0.0001f, gAtmosphereRayleigh.a);
    const float cosine = dot(AtmosphereSafeNormalize(dir), sunDirection);
    const float disk = smoothstep(
        cos(radius * 1.5f),
        cos(radius),
        cosine);
    return color + disk *
        gAtmosphereSunColorRadiance.rgb *
        max(0.0f, gAtmosphereSunColorRadiance.a);
}

float3 SampleAtmosphereSkyView(float3 dir)
{
    const float2 uv = float2(
        atan2(dir.z, dir.x) * 0.15915494309f + 0.5f,
        saturate(0.5f * dir.y + 0.5f));
    return gAtmosphereSkyView.SampleLevel(gLinearClamp, uv, 0.0f).rgb;
}

float4 main(PSInput input) : SV_TARGET
{
    float3 worldRayDir = ReconstructWorldRay(input.ndc);
    float3 skySampleDir = normalize(RotateYaw(worldRayDir, gSkyGroundYaw.a));
    uint mode = (uint)(gSkyTintMode.a + 0.5f);

    float3 sky = SampleGradient(skySampleDir);
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
        sky = gSkyTex2D.Sample(
            gLinearClamp,
            DirectionToLatLongUv(skySampleDir)).rgb;
    }

    if (mode != 4u)
    {
        sky = AddSunDisk(sky, skySampleDir);
    }
    sky *= gSkyTintMode.rgb;
    sky *= gSkyZenithExposure.a;
    if (!all(isfinite(sky)))
    {
        sky = 0.0f.xxx;
    }
    sky = max(sky, 0.0f.xxx);
    return float4(sky, 1.0f);
}
