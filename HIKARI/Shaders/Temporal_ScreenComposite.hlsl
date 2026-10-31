#include "Include/Temporal/HIKARI_TemporalShaderInterface.hlsli"

// 場景キャプチャを映す画面を、時間方向の拡大（DLSS・TAA）の後に出力の解像度で貼り直す。画面の中身は面と別に
// 動くので、拡大に任せると残像になるか、低い描画解像度のまま揺れる。ここで画素ごとに面の (s, t) を射影で求め、
// キャプチャを直接引いて、Unlit の材質と同じ道（表示参照 → 絶対量 → 霧 → 前露出）で色を出す。手前に物がある
// 画素（時間方向に均した深度が面より手前）は拡大の結果を残す。縁は面の辺までの距離で 1 画素に均す。
// 出力は前乗算の色と覆い（ONE / INV_SRC_ALPHA で重ねる）。

#define SCREEN_COMPOSITE_REGIONS 16

struct ScreenCompositeRegion
{
    float4 surfaceFromScreen[3];
    float4 uv01;
    float4 uv23;
    float4 depthPlane;   // 深度 = x·u + y·v + z、w = 貼図の番号
    float4 worldOrigin;
    float4 worldS;
    float4 worldT;
    float4 rect;
    float4 baseColor;
};

cbuffer TemporalScreenCompositeCB : register(b0)
{
    float4 gOutputSize;       // xy: 出力の大きさ、zw: その逆数
    float4 gViewExposure;     // {P, 1/P, P/E, P/P_prev}
    float4 gFogParams;        // 前向きの解析霧（HikariForwardFogFactor と同じ）
    float4 gFogColorDensity;
    float4 gCameraPosCount;   // xyz: カメラの位置、w: 画面の数
    ScreenCompositeRegion gRegions[SCREEN_COMPOSITE_REGIONS];
};

Texture2D<float> gCompositeDepth : register(t0);
Texture2D<float4> gCapture0 : register(t1);
Texture2D<float4> gCapture1 : register(t2);
Texture2D<float4> gCapture2 : register(t3);
Texture2D<float4> gCapture3 : register(t4);
Texture2D<float4> gCapture4 : register(t5);
Texture2D<float4> gCapture5 : register(t6);
Texture2D<float4> gCapture6 : register(t7);
Texture2D<float4> gCapture7 : register(t8);
SamplerState gCompositeSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = pos;
    output.position =
        float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float2 SurfaceAt(ScreenCompositeRegion region, float2 uv)
{
    float3 p = float3(uv, 1.0f);
    float3 q = float3(dot(region.surfaceFromScreen[0].xyz, p), dot(region.surfaceFromScreen[1].xyz, p),
                      dot(region.surfaceFromScreen[2].xyz, p));
    return q.xy / (abs(q.z) > 1e-12f ? q.z : 1e-12f);
}

float2 CaptureUvAt(ScreenCompositeRegion region, float2 st)
{
    float2 top = lerp(region.uv01.xy, region.uv01.zw, st.x);
    float2 bottom = lerp(region.uv23.zw, region.uv23.xy, st.x);
    return lerp(top, bottom, st.y);
}

float4 SampleCapture(uint index, float2 uv)
{
    [branch] switch (index)
    {
    case 0u: return gCapture0.SampleLevel(gCompositeSampler, uv, 0.0f);
    case 1u: return gCapture1.SampleLevel(gCompositeSampler, uv, 0.0f);
    case 2u: return gCapture2.SampleLevel(gCompositeSampler, uv, 0.0f);
    case 3u: return gCapture3.SampleLevel(gCompositeSampler, uv, 0.0f);
    case 4u: return gCapture4.SampleLevel(gCompositeSampler, uv, 0.0f);
    case 5u: return gCapture5.SampleLevel(gCompositeSampler, uv, 0.0f);
    case 6u: return gCapture6.SampleLevel(gCompositeSampler, uv, 0.0f);
    default: return gCapture7.SampleLevel(gCompositeSampler, uv, 0.0f);
    }
}

float2 CaptureSize(uint index)
{
    uint width = 1u, height = 1u;
    [branch] switch (index)
    {
    case 0u: gCapture0.GetDimensions(width, height); break;
    case 1u: gCapture1.GetDimensions(width, height); break;
    case 2u: gCapture2.GetDimensions(width, height); break;
    case 3u: gCapture3.GetDimensions(width, height); break;
    case 4u: gCapture4.GetDimensions(width, height); break;
    case 5u: gCapture5.GetDimensions(width, height); break;
    case 6u: gCapture6.GetDimensions(width, height); break;
    default: gCapture7.GetDimensions(width, height); break;
    }
    return float2(max(width, 1u), max(height, 1u));
}

// 1 画素に複数の texel が入る（遠い画面）時は、画素の範囲に 4 点を置いて平均する（細い線が画素の間で点滅しない）。
float4 SampleCaptureFootprint(uint index, float2 uv, float2 uvDx, float2 uvDy)
{
    float2 size = CaptureSize(index);
    float footprint = max(length(uvDx * size), length(uvDy * size));
    if (footprint <= 1.25f)
    {
        return SampleCapture(index, uv);
    }
    float4 sum = SampleCapture(index, uv + uvDx * 0.125f + uvDy * 0.375f);
    sum += SampleCapture(index, uv - uvDx * 0.375f + uvDy * 0.125f);
    sum += SampleCapture(index, uv - uvDx * 0.125f - uvDy * 0.375f);
    sum += SampleCapture(index, uv + uvDx * 0.375f - uvDy * 0.125f);
    return sum * 0.25f;
}

// 前向きの材質の解析霧（HikariForwardFogFactor と同じ式）。
float ForwardFogFactor(float3 worldPosition)
{
    if (gFogParams.x < 0.5f)
    {
        return 0.0f;
    }
    const float distance = max(length(gCameraPosCount.xyz - worldPosition) - gFogParams.y, 0.0f);
    const float falloff = max(gFogParams.w, 0.0f);
    const float h0 = max(gCameraPosCount.y - gFogParams.z, 0.0f);
    const float h1 = max(worldPosition.y - gFogParams.z, 0.0f);
    float heightScale = exp(-falloff * h0);
    if (falloff > 1.0e-5f && abs(h1 - h0) > 1.0e-3f)
    {
        heightScale = (exp(-falloff * h0) - exp(-falloff * h1)) / (falloff * (h1 - h0));
    }
    return 1.0f - exp(-max(gFogColorDensity.a, 0.0f) * heightScale * distance);
}

float4 PSMain(VSOut input) : SV_TARGET0
{
    const float2 uv = input.uv;
    const uint2 pixel = uint2(input.position.xy);
    // 深度は出力の解像度で時間方向に均したもの（0 = 近い面）。面との比べは 1 - 深度（視距離の逆数に比例）で行う。
    const float sceneInverseDepth = 1.0f - gCompositeDepth.Load(int3(pixel, 0));
    const uint count = min((uint)gCameraPosCount.w, (uint)SCREEN_COMPOSITE_REGIONS);
    float4 result = 0.0f;

    [loop]
    for (uint index = 0u; index < count; ++index)
    {
        const ScreenCompositeRegion region = gRegions[index];
        const float2 margin = gOutputSize.zw;
        if (any(uv < region.rect.xy - margin) || any(uv > region.rect.zw + margin))
        {
            continue;
        }
        const float2 st = SurfaceAt(region, uv);
        const float2 stDx = SurfaceAt(region, uv + float2(gOutputSize.z, 0.0f)) - st;
        const float2 stDy = SurfaceAt(region, uv + float2(0.0f, gOutputSize.w)) - st;
        // 面の辺までの距離（画素）。s と t それぞれ、1 画素あたりの変化で割る。
        const float2 perPixel = float2(length(float2(stDx.x, stDy.x)), length(float2(stDx.y, stDy.y)));
        const float2 edge = min(st, 1.0f - st) / max(perPixel, 1e-7f);
        float coverage = saturate(min(edge.x, edge.y) + 0.5f);
        if (coverage <= 0.0f)
        {
            continue;
        }
        // 手前の物：均した深度が面より少しでも手前なら、その分だけ拡大の結果を残す。
        const float planeInverseDepth = max(1.0f - dot(region.depthPlane.xyz, float3(uv, 1.0f)), 1e-6f);
        coverage *= 1.0f - smoothstep(1.004f, 1.02f, sceneInverseDepth / planeInverseDepth);
        if (coverage <= 0.0f)
        {
            continue;
        }
        const float2 captureUv = CaptureUvAt(region, st);
        const float2 captureDx = CaptureUvAt(region, st + stDx) - captureUv;
        const float2 captureDy = CaptureUvAt(region, st + stDy) - captureUv;
        const uint textureIndex = (uint)region.depthPlane.w;
        const float3 albedo =
            max(SampleCaptureFootprint(textureIndex, captureUv, captureDx, captureDy).rgb * region.baseColor.rgb, 0.0f);
        // Unlit の材質と同じ：表示参照の色を絶対量へ（× P/E × 1/P）、霧を通し、前露出（× P）。
        float3 radiance = albedo * (gViewExposure.z * gViewExposure.y);
        const float3 world = region.worldOrigin.xyz + st.x * region.worldS.xyz + st.y * region.worldT.xyz;
        radiance = lerp(radiance, gFogColorDensity.rgb, ForwardFogFactor(world));
        const float3 value = radiance * gViewExposure.x;
        // 後の画面ほど上に重ねる（前乗算の over）。
        result = float4(value * coverage + result.rgb * (1.0f - coverage), coverage + result.a * (1.0f - coverage));
    }
    return result;
}
