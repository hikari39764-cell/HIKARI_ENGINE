#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

float3 SampleScene(float2 uv)
{
    return gSceneTex.Sample(gSamp, saturate(uv)).rgb;
}

float3 ArcadeOutline(float2 warpedUv, float2 resolution)
{
    if (gUser[2].x < 0.5f || gUser[2].z <= 0.0f)
        return 0.0f.xxx;

    const float2 uvPerPixel = max(float2(
        length(float2(ddx(warpedUv.x), ddy(warpedUv.x))),
        length(float2(ddx(warpedUv.y), ddy(warpedUv.y)))), 1.0e-6f.xx);
    const float2 edge = (abs(warpedUv - 0.5f) - 0.5f) / uvPerPixel;
    const float distance = length(max(edge, 0.0f.xx)) + min(max(edge.x, edge.y), 0.0f);
    const float pixelScale = resolution.y / 1080.0f;
    const float width = clamp(gUser[2].y, 0.5f, 12.0f) * pixelScale;
    const float glowWidth = clamp(gUser[2].w, 0.0f, 32.0f) * pixelScale;
    if (distance < -1.0f || distance > width + glowWidth * 3.0f + 2.0f)
        return 0.0f.xxx;

    const float outside = smoothstep(-0.5f, 0.5f, distance);
    const float rim = outside * (1.0f - smoothstep(width - 0.75f, width + 0.75f, distance));
    const float halo = glowWidth > 0.0f
        ? outside * exp2(-max(distance - width, 0.0f) * 4.0f / max(glowWidth, 0.5f)) * 0.18f
        : 0.0f;
    const float2 surface = saturate(warpedUv);
    const float perimeter = abs(warpedUv.y - 0.5f) >= abs(warpedUv.x - 0.5f)
        ? (warpedUv.y < 0.5f ? surface.x : 3.0f - surface.x)
        : (warpedUv.x > 0.5f ? 1.0f + surface.y : 4.0f - surface.y);
    const float motion = saturate(gUser[3].w);
    const float clock = gTime * clamp(gUser[4].w, 0.0f, 3.0f);
    const float pulse = 1.0f + motion * 0.12f * sin(clock * 1.8f);
    const float phaseDistance = abs(frac(perimeter * 0.25f - clock * 0.12f + 0.5f) - 0.5f);
    const float runner = pow(saturate(1.0f - phaseDistance / 0.065f), 2.0f) * motion;
    const float3 baseColor = saturate(gUser[3].rgb);
    const float3 accentColor = saturate(gUser[4].rgb);
    const float3 rimColor = lerp(baseColor, accentColor, runner);
    return (rimColor * rim * (1.0f + runner * 0.65f) + baseColor * halo) *
        clamp(gUser[2].z, 0.0f, 4.0f) * pulse;
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float3 unwarped = SampleScene(input.uv);
    const float strength = saturate(gUser[0].x * gIntensity);
    if (strength <= 0.0001f)
    {
        return float4(unwarped, 1.0f);
    }

    const float curvature = max(gUser[0].y, 0.0f);
    const float scanlineStrength = saturate(gUser[0].z);
    const float phosphorStrength = saturate(gUser[0].w);
    const float chromaPixels = max(gUser[1].x, 0.0f);
    const float glitchStrength = saturate(gUser[1].y);
    const float vignetteStrength = saturate(gUser[1].z);
    const float rollingStrength = saturate(gUser[1].w);

    const float2 resolution = max(
        float2(gResolutionX, gResolutionY),
        float2(1.0f, 1.0f));
    const float2 texel = rcp(resolution);
    float2 centered = input.uv * 2.0f - 1.0f;
    const float radius2 = dot(centered, centered);
    centered *= 1.0f + curvature * strength * radius2;
    const float2 uv = centered * 0.5f + 0.5f;
    const float inside = step(0.0f, uv.x) * step(uv.x, 1.0f) *
        step(0.0f, uv.y) * step(uv.y, 1.0f);
    const float3 source = SampleScene(uv) * inside;

    // CYBER と同じ走査帯グリッチを、従来の粒状ノイズの代わりに使う。
    // CRT の走査線と蛍光体マスクは画面側に固定し、映像だけを横に裂く。
    const float strip = floor(uv.y * 54.0f + gTime * 3.0f);
    const float stripNoise = HikariHash12(float2(
        strip,
        floor(gTime * 12.0f)));
    const float glitchGate = step(0.86f, stripNoise) * glitchStrength;
    const float horizontalShift = (stripNoise - 0.5f) *
        glitchGate * 18.0f * texel.x;
    const float2 sampleUv = saturate(
        uv + float2(horizontalShift, 0.0f));

    const float readability = HikariScreenReadability(sampleUv, resolution);
    const float glitchChromaPixels = lerp(chromaPixels, .25f, readability) *
        (1.0f + glitchGate * 3.0f);
    const float2 chromaOffset = float2(
        glitchChromaPixels * texel.x,
        0.0f);
    float3 color = float3(
        SampleScene(sampleUv + chromaOffset).r,
        SampleScene(sampleUv).g,
        SampleScene(sampleUv - chromaOffset).b);
    const float3 glow =
        SampleScene(sampleUv + texel * float2(1.5f, 0.0f)) +
        SampleScene(sampleUv - texel * float2(1.5f, 0.0f)) +
        SampleScene(sampleUv + texel * float2(0.0f, 1.5f)) +
        SampleScene(sampleUv - texel * float2(0.0f, 1.5f));
    color += max(glow * 0.25f - 0.62f.xxx, 0.0f.xxx) * lerp(0.18f, 0.02f, readability);

    const float pixelY = uv.y * resolution.y;
    const float scanline = lerp(
        1.0f,
        0.72f + 0.28f * sin(pixelY * 3.14159265f),
        scanlineStrength);
    color *= scanline;

    const uint triad = ((uint)floor(uv.x * resolution.x)) % 3u;
    const float3 phosphor = triad == 0u
        ? float3(1.10f, 0.82f, 0.82f)
        : (triad == 1u
            ? float3(0.82f, 1.10f, 0.82f)
            : float3(0.82f, 0.82f, 1.10f));
    color *= lerp(1.0f.xxx, phosphor, phosphorStrength * (1.0f - readability));

    const float rollingBand = 0.5f + 0.5f * sin(
        uv.y * 8.0f - gTime * 1.8f);
    color *= 1.0f + (rollingBand - 0.5f) * rollingStrength * 0.10f;

    const float vignette = saturate(
        1.0f - radius2 * (0.34f + vignetteStrength * 0.62f));
    color *= lerp(vignette, 1.0f, readability * .85f) * inside;
    const float3 outlined = lerp(source, color, strength) + ArcadeOutline(uv, resolution) * strength;
    return float4(max(outlined, 0.0f.xxx), 1.0f);
}
