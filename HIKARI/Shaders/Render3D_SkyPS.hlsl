cbuffer SkyCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4 gSkyZenithExposure;      // rgb zenith, a exposure
    float4 gSkyHorizonPower;        // rgb horizon, a horizon power
    float4 gSkyGroundYaw;           // rgb ground, a yaw
    float4 gSkyTintMode;            // rgb tint, a SkyMode
    float4 gSkySunDirectionIntensity;
    float4 gSkySunSizeParams;       // x sun size, w directional intensity
    float4 gSkyCloudCameraTime;     // xyz camera world position, w unscaled time
    float4 gSkyCloudShape;          // coverage, density, altitude, thickness
    float4 gSkyCloudMotion;         // scale, wind x/y, wind speed
    float4 gSkyCloudDetailLighting; // shape detail, softness, absorption, silver lining
    float4 gSkyCloudHorizonQuality; // horizon fade, enabled, steps, max steps
    float4 gSkyCloudBrightColor;
    float4 gSkyCloudShadowColor;
};

Texture2D gSkyTex2D : register(t0);
TextureCube gSkyCube : register(t1);
SamplerState gLinearClamp : register(s0);

// Keep these constants synchronized with the C++ SkyCB packing and quality
// policy in HIKARI_SkyRenderer.cpp. The ray loop below is fixed at 16 and
// exits early for Low/Balanced quality (6/10/16).
static const uint HIKARI_STYLIZED_CLOUD_LOW_STEPS = 6u;
static const uint HIKARI_STYLIZED_CLOUD_BALANCED_STEPS = 10u;
static const uint HIKARI_STYLIZED_CLOUD_HIGH_STEPS = 16u;
static const uint HIKARI_STYLIZED_CLOUD_MAX_STEPS = 16u;

struct PSInput
{
    float4 position : SV_POSITION;
    float3 localDir : TEXCOORD0;
};

float3 RotateYaw(float3 dir, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);
    return float3(dir.x * c - dir.z * s, dir.y, dir.x * s + dir.z * c);
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
    float y = saturate(dir.y * 0.5f + 0.5f);
    float3 upper = lerp(gSkyHorizonPower.rgb, gSkyZenithExposure.rgb, y);
    float3 lower = lerp(gSkyGroundYaw.rgb, gSkyHorizonPower.rgb, y);
    float3 color = (dir.y >= 0.0f) ? upper : lower;
    float horizon = pow(saturate(1.0f - abs(dir.y)), max(0.01f, gSkyHorizonPower.a));
    return lerp(color, gSkyHorizonPower.rgb, horizon * 0.25f);
}

float3 AddSunDisk(float3 color, float3 dir)
{
    float intensity = max(0.0f, gSkySunDirectionIntensity.w);
    if (intensity <= 0.0f)
    {
        return color;
    }

    float3 sunDir = normalize(-gSkySunDirectionIntensity.xyz);
    float size = max(0.0001f, gSkySunSizeParams.x);
    float sun = smoothstep(1.0f - size, 1.0f, dot(normalize(dir), sunDir));
    return color + sun * intensity;
}

float Hash21(float2 p)
{
    // A deterministic, texture-free hash keeps the cloud field stable in
    // world space and avoids a new GPU resource or sampler dependency.
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 34.45f);
    return frac(p.x * p.y);
}

float Noise2D(float2 p)
{
    float2 cell = floor(p);
    float2 local = frac(p);
    local = local * local * (3.0f - 2.0f * local);

    float n00 = Hash21(cell);
    float n10 = Hash21(cell + float2(1.0f, 0.0f));
    float n01 = Hash21(cell + float2(0.0f, 1.0f));
    float n11 = Hash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(n00, n10, local.x), lerp(n01, n11, local.x), local.y);
}

float Fbm2D(float2 p, float detail)
{
    // Three fixed octaves give a bounded cost independent of cloud quality;
    // quality controls the slab march, while detail controls the high
    // frequency contribution inside each sample.
    float value = 0.0f;
    float amplitude = 0.5f;
    float normalizer = 0.0f;
    float2 shift = float2(17.13f, 9.37f);
    [unroll]
    for (uint octave = 0u; octave < 3u; ++octave)
    {
        float octaveWeight = (octave == 0u) ? 1.0f : lerp(0.72f, 1.0f, detail);
        value += Noise2D(p) * amplitude * octaveWeight;
        normalizer += amplitude * octaveWeight;
        p = p * 2.03f + shift;
        amplitude *= 0.5f;
    }
    return saturate(value / max(normalizer, 0.0001f));
}

float CloudDensityAt(float3 worldPosition, float height01)
{
    float scale = max(1.0f, gSkyCloudMotion.x);
    float2 wind = gSkyCloudMotion.yz * gSkyCloudMotion.w * gSkyCloudCameraTime.w;
    float2 coordinates = worldPosition.xz / scale - wind / scale;

    float detail = saturate(gSkyCloudDetailLighting.x);
    float broadNoise = Fbm2D(coordinates, detail * 0.55f);
    // One additional octave keeps the total at four Noise2D calls per march
    // sample (three broad + one detail) in the common quality path.
    float detailNoise = Noise2D(coordinates * 2.35f + float2(5.3f, -3.1f));
    float shape = saturate(lerp(broadNoise, detailNoise, detail * 0.82f));

    // Coverage increases the filled area. Softness gives the noise threshold
    // a broad anime-like edge instead of a hard binary cutout.
    float threshold = 1.0f - saturate(gSkyCloudShape.x);
    float edgeWidth = max(0.01f, gSkyCloudDetailLighting.y);
    float horizontalShape = smoothstep(
        threshold - edgeWidth,
        threshold + edgeWidth,
        shape);

    float verticalEdge = max(0.025f, edgeWidth * 0.65f);
    float verticalShape =
        smoothstep(0.0f, verticalEdge, height01) *
        (1.0f - smoothstep(1.0f - verticalEdge, 1.0f, height01));
    return horizontalShape * saturate(verticalShape);
}

float EvaluateStylizedCloudSlab(float3 rayOrigin, float3 rayDirection, out float3 cloudColor)
{
    cloudColor = gSkyCloudBrightColor.rgb;

    float thickness = max(0.5f, gSkyCloudShape.w);
    float bottom = gSkyCloudShape.z;
    float top = bottom + thickness;
    float directionY = rayDirection.y;
    if (abs(directionY) <= 0.0001f)
    {
        return 0.0f;
    }

    float slabT0 = (bottom - rayOrigin.y) / directionY;
    float slabT1 = (top - rayOrigin.y) / directionY;
    float nearT = max(0.0f, min(slabT0, slabT1));
    float farT = max(slabT0, slabT1);
    if (farT <= nearT)
    {
        return 0.0f;
    }

    uint steps = (uint)clamp(
        gSkyCloudHorizonQuality.z,
        1.0f,
        (float)HIKARI_STYLIZED_CLOUD_MAX_STEPS);
    float stepLength = (farT - nearT) / (float)steps;
    float transmittance = 1.0f;
    float opticalDepth = 0.0f;
    float3 accumulatedColor = 0.0f.xxx;
    float3 lightDirection = normalize(-gSkySunDirectionIntensity.xyz);
    float sunEnergy = saturate(gSkySunSizeParams.w * 0.35f);
    float absorption = max(0.0f, gSkyCloudDetailLighting.z);
    float silverLining = max(0.0f, gSkyCloudDetailLighting.w);

    [loop]
    for (uint step = 0u; step < HIKARI_STYLIZED_CLOUD_MAX_STEPS; ++step)
    {
        // This is the quality policy's bounded early exit: 6, 10, or 16
        // samples, while the compiled loop maximum remains 16.
        if (step >= steps)
        {
            break;
        }

        float distanceAlongRay = nearT + (float(step) + 0.5f) * stepLength;
        float3 samplePosition = rayOrigin + rayDirection * distanceAlongRay;
        float height01 = saturate((samplePosition.y - bottom) / thickness);
        float localDensity = CloudDensityAt(samplePosition, height01) *
            max(0.0f, gSkyCloudShape.y);
        float segmentDepth = localDensity * stepLength / thickness * 2.2f;
        float sampleAlpha = 1.0f - exp(-segmentDepth);

        // The front-to-back optical depth is also the inexpensive self-shadow
        // approximation: deeper cloud samples receive a cooler, darker lobe.
        float selfShadow = exp(-opticalDepth * absorption * 1.65f);
        float sunFacing = saturate(dot(lightDirection, float3(0.0f, 1.0f, 0.0f)) * 0.5f + 0.5f);
        // Silver lining follows the sun-view alignment; perpendicular views
        // must not become the brightest part of the cloud by accident.
        float sunRim = pow(saturate(dot(rayDirection, lightDirection)), 3.0f);
        float lighting = saturate(
            0.18f +
            sunFacing * (0.34f + 0.66f * sunEnergy) * selfShadow +
            silverLining * sunRim * sunEnergy);
        float3 sampleColor = lerp(
            gSkyCloudShadowColor.rgb,
            gSkyCloudBrightColor.rgb,
            lighting);

        accumulatedColor += transmittance * sampleAlpha * sampleColor;
        transmittance *= 1.0f - sampleAlpha;
        opticalDepth += segmentDepth;
        if (transmittance <= 0.02f)
        {
            break;
        }
    }

    float cloudAlpha = 1.0f - transmittance;
    float horizon = smoothstep(
        0.0f,
        max(0.001f, gSkyCloudHorizonQuality.x),
        abs(rayDirection.y));
    cloudAlpha *= horizon;
    cloudColor = accumulatedColor / max(1.0f - transmittance, 0.0001f);
    return saturate(cloudAlpha);
}

float3 ApplyStylizedClouds(float3 sky, float3 rayDirection)
{
    // Disabled clouds are an exact pass-through so old scenes and invalid
    // optional JSON members retain the existing sky result bit-for-bit.
    if (gSkyCloudHorizonQuality.y <= 0.5f ||
        gSkyCloudShape.x <= 0.0f ||
        gSkyCloudShape.y <= 0.0f)
    {
        return sky;
    }

    float3 cloudColor;
    float cloudAlpha = EvaluateStylizedCloudSlab(
        gSkyCloudCameraTime.xyz,
        rayDirection,
        cloudColor);
    return lerp(sky, cloudColor, cloudAlpha);
}

float4 main(PSInput input) : SV_TARGET
{
    float3 dir = normalize(RotateYaw(input.localDir, gSkyGroundYaw.a));
    uint mode = (uint)(gSkyTintMode.a + 0.5f);

    float3 sky = SampleGradient(dir);
    if (mode == 2u)
    {
        sky = gSkyCube.Sample(gLinearClamp, dir).rgb;
    }
    else if (mode == 3u)
    {
        sky = gSkyTex2D.Sample(gLinearClamp, DirectionToLatLongUv(dir)).rgb;
    }

    sky = AddSunDisk(sky, dir);
    sky = ApplyStylizedClouds(sky, dir);
    sky *= gSkyTintMode.rgb;
    sky *= gSkyZenithExposure.a;
    return float4(sky, 1.0f);
}
