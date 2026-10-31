// 幾何の線だけを jitter のある HDR に描く。DLSS/TAA が色と線を一緒に解決し、網点と画素格子は表示段階に残す。
#include "Include/Post/HIKARI_PostStylize.hlsli"

float3 InkSceneColor(float3 displayColor)
{
    return pow(saturate(displayColor), max(gDisplayGamma, 0.01f)) * max(gDisplayToScene, 1e-6f);
}

float4 GeometryInk(HikariPostEffectInput input, float strength, float radius1080, float depthSensitivity,
                   float3 displayColor, float surfaceColor, float darken, bool pixelLine, bool protectScreen)
{
    const float4 source = gSceneTex.SampleLevel(gSamp, input.uv, 0.0f);
    float weight = saturate(strength * gIntensity);
    const float radius = max(radius1080 * HikariStylizePixelScale(), 0.0f);
    if (weight <= 0.0001f || radius <= 0.0f)
        return source;
    const float2 resolution = max(float2(gResolutionX, gResolutionY), 1.0f.xx);
    if (protectScreen)
        weight *= 1.0f - HikariScreenReadabilityAt(input.uv - gRasterJitterPixels / resolution, resolution,
                                                 1.0f - HikariPostReadInverseDepth(input.uv));
    // 線幅は 1080p の画素で指定する。内部の 1 画素未満は面積の被覆率にし、整数幅へ切り上げない。
    const float2 geometry = HikariStylizeGeometryEdges(input.uv, max(radius, 1.0f));
    const float threshold = lerp(0.25f, 0.01f, saturate(depthSensitivity));
    const float lineCoverage = pixelLine ? smoothstep(0.1f, 0.25f, geometry.x)
        : max(smoothstep(threshold, threshold * 2.0f, geometry.x),
              smoothstep(threshold * 0.5f, threshold, geometry.y));
    const float3 traced = source.rgb * pow(saturate(darken), max(gDisplayGamma, 0.01f));
    const float3 ink = lerp(InkSceneColor(displayColor), traced, saturate(surfaceColor));
    return float4(lerp(source.rgb, ink, lineCoverage * weight * saturate(radius)), source.a);
}

float4 Pixel(HikariPostEffectInput input) : SV_TARGET
{
    return GeometryInk(input, gUser[0].x * gUser[1].x, gUser[0].y, 0.0f, gUser[2].rgb, 0.0f, 0.0f, true, false);
}

float4 Manga(HikariPostEffectInput input) : SV_TARGET
{
    return GeometryInk(input, gUser[0].x * gUser[1].y, gUser[1].z, gUser[1].w, gUser[4].rgb,
                       0.0f, 0.0f, false, true);
}

float4 Outline(HikariPostEffectInput input) : SV_TARGET
{
    return GeometryInk(input, gUser[0].x, gUser[0].y, gUser[0].z, gUser[2].rgb, gUser[1].x, gUser[1].y, false, false);
}

float4 Sketch(HikariPostEffectInput input) : SV_TARGET
{
    return GeometryInk(input, gUser[0].x, gUser[0].y, gUser[0].z, gUser[2].rgb, 0.0f, 0.0f, false, false);
}
