cbuffer CommonParams : register(b0)
{
    float gTime;
    float gDeltaTime;
    float gCombo;
    float gIntensity;
    float gResolutionX;
    float gResolutionY;
    float2 gCommonPadding;
    float4 gUser[16];
};

Texture2D<float> gSceneDepth : register(t0);
SamplerState gLinearClamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float HikariDepthIsSky(float depth)
{
    return step(0.999995f, depth);
}

float HikariSampleSkyVisibility(float2 uv)
{
    if (any(uv < 0.0f) || any(uv > 1.0f))
    {
        return 0.0f;
    }
    return HikariDepthIsSky(
        gSceneDepth.SampleLevel(gLinearClamp, uv, 0.0f));
}

float HikariResolveSunVisibility(float2 sunUv)
{
    const float2 depthSize = max(gUser[3].zw, float2(1.0f, 1.0f));
    const float2 texel = 1.0f / depthSize;
    const float radius = max(gUser[3].x, max(texel.x, texel.y) * 2.0f);
    const float2 axisX = float2(radius, 0.0f);
    const float2 axisY = float2(0.0f, radius);
    const float2 diagonal = float2(radius, radius) * 0.70710678f;

    float visibility = 0.0f;
    visibility += HikariDepthIsSky(
        gSceneDepth.SampleLevel(gLinearClamp, sunUv, 0.0f));
    visibility += HikariDepthIsSky(
        gSceneDepth.SampleLevel(gLinearClamp, sunUv + axisX, 0.0f));
    visibility += HikariDepthIsSky(
        gSceneDepth.SampleLevel(gLinearClamp, sunUv - axisX, 0.0f));
    visibility += HikariDepthIsSky(
        gSceneDepth.SampleLevel(gLinearClamp, sunUv + axisY, 0.0f));
    visibility += HikariDepthIsSky(
        gSceneDepth.SampleLevel(gLinearClamp, sunUv - axisY, 0.0f));
    visibility += HikariDepthIsSky(
        gSceneDepth.SampleLevel(gLinearClamp, sunUv + diagonal, 0.0f));
    visibility += HikariDepthIsSky(
        gSceneDepth.SampleLevel(gLinearClamp, sunUv - diagonal, 0.0f));
    visibility += HikariDepthIsSky(
        gSceneDepth.SampleLevel(
            gLinearClamp,
            sunUv + float2(diagonal.x, -diagonal.y),
            0.0f));
    visibility += HikariDepthIsSky(
        gSceneDepth.SampleLevel(
            gLinearClamp,
            sunUv + float2(-diagonal.x, diagonal.y),
            0.0f));
    return visibility / 9.0f;
}

float HikariResolveRadialShaft(
    float2 pixelUv,
    float2 sunUv,
    float shaftLength,
    float shaftDecay,
    float sourceRadius)
{
    static const uint kShaftSampleCount = 24u;
    const float aspect = max(
        gResolutionX / max(gResolutionY, 1.0f),
        0.1f);
    const float2 toSun = sunUv - pixelUv;
    const float radialDistance = length(float2(toSun.x * aspect, toSun.y));
    const float reach = saturate(
        shaftLength / max(radialDistance, 1.0e-4f));
    const float envelope = 1.0f - smoothstep(
        shaftLength * 0.12f,
        shaftLength,
        radialDistance);
    if (envelope <= 0.0f)
    {
        return 0.0f;
    }

    const float2 stepUv = toSun * reach /
        (float)kShaftSampleCount;
    float2 sampleUv = pixelUv;
    float decayWeight = 1.0f;
    float weightSum = 0.0f;
    float accumulated = 0.0f;
    float pathVisibility = HikariSampleSkyVisibility(pixelUv);
    const float radiusSquared = max(
        sourceRadius * sourceRadius,
        1.0e-6f);
    [unroll]
    for (uint sampleIndex = 0u;
         sampleIndex < kShaftSampleCount;
         ++sampleIndex)
    {
        sampleUv += stepUv;
        const float sky = HikariSampleSkyVisibility(sampleUv);
        // Keep some path memory so a silhouette near the solar disk creates a
        // readable ray instead of turning the entire pass off abruptly.
        pathVisibility = lerp(pathVisibility, sky, 0.42f);
        const float2 sourceDelta = float2(
            (sampleUv.x - sunUv.x) * aspect,
            sampleUv.y - sunUv.y);
        const float source = exp(
            -dot(sourceDelta, sourceDelta) / radiusSquared);
        accumulated += source * pathVisibility * decayWeight;
        weightSum += source * decayWeight;
        decayWeight *= shaftDecay;
    }
    return saturate(accumulated / max(weightSum, 1.0e-4f)) * envelope;
}

float4 main(PSInput input) : SV_TARGET
{
    const float2 sunUv = gUser[0].xy;
    const float coreRadius = max(gUser[0].z, 0.0005f);
    const float intensity = max(gUser[0].w, 0.0f);
    const float3 sunColor = max(gUser[1].rgb, 0.0f.xxx);
    const float maxRadiance = max(gUser[1].w, 0.1f);
    const float haloRadius = max(gUser[2].x, coreRadius);
    const float haloFalloff = max(gUser[2].y, 0.25f);
    const float streakIntensity = max(gUser[2].z, 0.0f);
    const float streakLength = max(gUser[2].w, 0.001f);
    const float edgeFadeDistance = max(gUser[3].y, 0.001f);
    const float shaftIntensity = max(gUser[4].x, 0.0f);
    const float shaftLength = max(gUser[4].y, 0.01f);
    const float shaftDecay = clamp(gUser[4].z, 0.5f, 0.999f);

    const float screenEdgeDistance = min(
        min(sunUv.x, 1.0f - sunUv.x),
        min(sunUv.y, 1.0f - sunUv.y));
    const float edgeVisibility = smoothstep(
        0.0f,
        edgeFadeDistance,
        screenEdgeDistance);
    if (intensity <= 0.0f || edgeVisibility <= 0.0f)
    {
        return 0.0f.xxxx;
    }

    const float visibility =
        HikariResolveSunVisibility(sunUv) * edgeVisibility;

    float2 delta = input.uv - sunUv;
    delta.x *= max(gResolutionX / max(gResolutionY, 1.0f), 0.1f);
    const float radialDistance = length(delta);
    const float core = exp(
        -radialDistance * radialDistance /
        max(coreRadius * coreRadius * 0.65f, 1.0e-7f));
    const float halo = pow(
        saturate(1.0f - radialDistance / haloRadius),
        haloFalloff);
    const float horizontalStreak = exp(
        -abs(delta.y) / max(coreRadius * 1.75f, 0.0005f)) *
        exp(-abs(delta.x) / streakLength) *
        streakIntensity;

    const float coreAndHalo =
        (core + halo * 0.38f + horizontalStreak) * visibility;
    const float shaft = HikariResolveRadialShaft(
        input.uv,
        sunUv,
        shaftLength,
        shaftDecay,
        max(haloRadius * 0.42f, coreRadius * 5.0f)) *
        shaftIntensity * edgeVisibility;
    float3 glare = sunColor *
        ((coreAndHalo + shaft) * intensity);
    glare = min(max(glare, 0.0f.xxx), maxRadiance.xxx);
    if (!all(isfinite(glare)))
    {
        glare = 0.0f.xxx;
    }
    return float4(glare, 0.0f);
}
