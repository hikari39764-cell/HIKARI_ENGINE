#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"

// GTAO（Jimenez 2016）。画素を通る画面上の直線（スライス）ごとに両側の水平線を探し、スライス面へ投影した法線を
// 基準に可視の円弧を積分する。半球サンプリングより少ない読み出しで、角や接地の遮蔽が距離に応じて正しく出る。
// 時間蓄積は持たないので乱数は画素位置だけで決める。白色雑音ではなく交互勾配雑音（近傍の画素が互いに異なる
// 角を取る格子状の並び）にして、後段の 5 tap の両側フィルタで一画素ごとの斑が消えるようにする。

cbuffer SsaoPassCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gScreenParams;
    float4 gAoParams0;
    float4 gAoParams1;
    float4 gBlurParams;
    float4 gDepthScreenParams;
};

#define gAoRadius gAoParams0.x
#define gAoBias gAoParams0.y
#define gAoStrength gAoParams0.z
#define gAoPower gAoParams0.w
#define gAoSliceCount gAoParams1.x
#define gAoStepCount gAoParams1.y

static const float kPi = 3.14159265f;
static const float kHalfPi = 1.57079633f;

Texture2D gSceneDepthTex : register(t0);
Texture2D gNormalRoughnessTex : register(t1);
SamplerState gPointClamp : register(s0);
SamplerState gLinearClamp : register(s1);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut o;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    o.uv = pos;
    o.pos = float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float3 DecodeNormal(float4 packed)
{
    return HikariDecodeOctahedralDirection(packed.rg);
}

float3 ReconstructWorld(float2 uv, float depth)
{
    float3 world;
    HikariReconstructWorldPosition(uv, depth, gInvViewProj, world);
    return world;
}

float2 ProjectWorld(float3 world)
{
    float2 uv;
    float projectedDepth;
    float viewDepth;
    HikariProjectWorldPosition(world, gViewProj, uv, projectedDepth, viewDepth);
    return uv;
}

float3 ReconstructNormalFromDepth(float2 uv, float centerDepth)
{
    float2 texel = gDepthScreenParams.zw;
    float2 uvRight = saturate(uv + float2(texel.x, 0.0f));
    float2 uvDown = saturate(uv + float2(0.0f, texel.y));
    float depthRight = gSceneDepthTex.SampleLevel(gPointClamp, uvRight, 0).r;
    float depthDown = gSceneDepthTex.SampleLevel(gPointClamp, uvDown, 0).r;
    depthRight = depthRight >= 0.99999f ? centerDepth : depthRight;
    depthDown = depthDown >= 0.99999f ? centerDepth : depthDown;

    float3 p = ReconstructWorld(uv, centerDepth);
    float3 px = ReconstructWorld(uvRight, depthRight);
    float3 py = ReconstructWorld(uvDown, depthDown);
    float3 n = normalize(cross(py - p, px - p));
    float3 viewDir = normalize(gCameraPos.xyz - p);
    return dot(n, viewDir) < 0.0f ? -n : n;
}

// 世界半径を画面上の画素数へ。面の接線方向へ半径だけ動いた点を射影して測る。
float ScreenRadiusPixels(float2 centerUv, float3 worldPos, float3 normal)
{
    float3 up = abs(normal.y) < 0.98f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float2 radiusUv = ProjectWorld(worldPos + tangent * gAoRadius) - centerUv;
    float radius = length(radiusUv * gScreenParams.xy);
    return clamp(radius, 2.0f, 0.12f * gScreenParams.x);
}

// 片側の水平線。cos は視線との角の余弦で、半径の外側ほど -1（遮蔽なし）へ戻す。
float HorizonCosine(float2 uv, float2 stepUv, float noise, uint stepCount, float3 position, float3 view)
{
    float horizon = -1.0f;
    [loop] for (uint step = 0u; step < stepCount; ++step)
    {
        float2 sampleUv = uv + stepUv * (float(step) + noise);
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
            break;
        sampleUv = HikariSnapUvToTexelCenter(sampleUv, gDepthScreenParams);
        const float sampleDepth = gSceneDepthTex.SampleLevel(gPointClamp, sampleUv, 0).r;
        if (sampleDepth >= 0.99999f)
            continue;
        const float3 delta = ReconstructWorld(sampleUv, sampleDepth) - position;
        const float distance = length(delta);
        if (distance <= gAoBias)
            continue;
        const float cosine = dot(delta / distance, view);
        const float falloff = saturate((distance / gAoRadius - 0.7f) / 0.3f);
        horizon = max(horizon, lerp(cosine, -1.0f, falloff));
    }
    return horizon;
}

float GroundTruthAo(float2 uv, float depth, float3 n)
{
    const float3 position = ReconstructWorld(uv, depth);
    const float3 view = normalize(gCameraPos.xyz - position);
    const float radiusPixels = ScreenRadiusPixels(uv, position, n);
    const float2 texel = gScreenParams.zw;
    const float2 pixel = floor(uv * gScreenParams.xy);
    const float noiseSlice = HikariInterleavedGradientNoise(pixel, 0u);
    const float noiseStep = HikariInterleavedGradientNoise((pixel + 5.0f).yx, 0u);
    const uint sliceCount = max((uint)gAoSliceCount, 1u);
    const uint stepCount = max((uint)gAoStepCount, 1u);

    float visibility = 0.0f;
    [loop] for (uint slice = 0u; slice < sliceCount; ++slice)
    {
        const float phi = (float(slice) + noiseSlice) * (kPi / float(sliceCount));
        const float2 direction = float2(cos(phi), sin(phi));
        const float2 stepUv = direction * texel * (radiusPixels / float(stepCount));
        // スライス面：視線と、画面上の方向に対応する世界方向が張る平面。
        const float3 sliceDir = ReconstructWorld(uv + direction * texel, depth) - position;
        const float3 axis = normalize(sliceDir - view * dot(sliceDir, view));
        const float3 sliceNormal = normalize(cross(axis, view));
        const float3 projected = n - sliceNormal * dot(n, sliceNormal);
        const float projectedLength = length(projected);
        if (projectedLength < 1.0e-4f)
            continue;
        const float3 projectedDir = projected / projectedLength;
        const float gamma = sign(dot(projectedDir, axis)) * acos(saturate(dot(projectedDir, view)));
        float h1 = -acos(HorizonCosine(uv, -stepUv, noiseStep, stepCount, position, view));
        float h2 = acos(HorizonCosine(uv, stepUv, noiseStep, stepCount, position, view));
        h1 = gamma + max(h1 - gamma, -kHalfPi);
        h2 = gamma + min(h2 - gamma, kHalfPi);
        const float sinGamma = sin(gamma);
        const float cosGamma = cos(gamma);
        visibility += projectedLength * 0.25f *
            ((-cos(2.0f * h1 - gamma) + cosGamma + 2.0f * h1 * sinGamma) +
             (-cos(2.0f * h2 - gamma) + cosGamma + 2.0f * h2 * sinGamma));
    }
    visibility /= float(sliceCount);
    const float ao = 1.0f - saturate((1.0f - saturate(visibility)) * gAoStrength);
    return pow(saturate(ao), gAoPower);
}

float PSMain(VSOut input) : SV_TARGET
{
    const float2 uv = HikariSnapUvToTexelCenter(input.uv, gDepthScreenParams);
    const float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
    if (depth >= 0.99999f)
        return 1.0f;
    return GroundTruthAo(uv, depth, DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, uv, 0)));
}

float PSMainDepthOnly(VSOut input) : SV_TARGET
{
    const float2 uv = HikariSnapUvToTexelCenter(input.uv, gDepthScreenParams);
    const float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
    if (depth >= 0.99999f)
        return 1.0f;
    return GroundTruthAo(uv, depth, ReconstructNormalFromDepth(uv, depth));
}
