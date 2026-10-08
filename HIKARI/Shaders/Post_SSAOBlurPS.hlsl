#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"

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

Texture2D gAoTex : register(t0);
Texture2D gSceneDepthTex : register(t1);
Texture2D gNormalRoughnessTex : register(t2);
SamplerState gPointClamp : register(s0);
SamplerState gLinearClamp : register(s1);

#define gAoRadius gAoParams0.x

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

float DepthAwareWeight(float3 centerWorld, float3 sampleWorld)
{
    // AO 半解像度復元では非線形 depth 差ではなく、実空間距離でエッジを守る。
    float depthScale = max(gAoRadius * 0.35f, 0.035f);
    return exp(-length(sampleWorld - centerWorld) / depthScale);
}

// 半解像度の AO を一方向に 5 tap で均す。深度と法線が近いサンプルだけを強く採用する。
float BlurAo(float2 uv, bool depthOnlyNormals)
{
    float2 texel = gScreenParams.zw * gBlurParams.xy;
    float centerDepth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
    if (centerDepth >= 0.99999f)
    {
        return gAoTex.SampleLevel(gLinearClamp, uv, 0).r;
    }

    float3 centerNormal = depthOnlyNormals
        ? ReconstructNormalFromDepth(uv, centerDepth)
        : DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, uv, 0));
    float3 centerWorld = ReconstructWorld(uv, centerDepth);

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int i = -2; i <= 2; ++i)
    {
        float2 sampleUv = uv + texel * float(i);
        float ao = gAoTex.SampleLevel(gLinearClamp, sampleUv, 0).r;
        float depth = gSceneDepthTex.SampleLevel(gPointClamp, sampleUv, 0).r;
        if (depth >= 0.99999f)
        {
            continue;
        }

        float3 normal = depthOnlyNormals
            ? ReconstructNormalFromDepth(sampleUv, depth)
            : DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, sampleUv, 0));
        float3 sampleWorld = ReconstructWorld(sampleUv, depth);
        float normalWeight = saturate(dot(centerNormal, normal));
        normalWeight *= normalWeight;
        float depthWeight = DepthAwareWeight(centerWorld, sampleWorld);
        float kernelWeight = (i == 0) ? 0.34f : ((abs(i) == 1) ? 0.22f : 0.10f);
        float weight = kernelWeight * normalWeight * depthWeight;
        sum += ao * weight;
        weightSum += weight;
    }

    return sum / max(weightSum, 1e-4f);
}

float PSMain(VSOut input) : SV_TARGET
{
    return BlurAo(input.uv, false);
}

float PSMainDepthOnly(VSOut input) : SV_TARGET
{
    return BlurAo(input.uv, true);
}

// 半解像度 AO を全解像度へ戻す（joint bilateral upsampling）。画素を囲む 2x2 の AO texel を双線形の重みで混ぜ、
// 深度と法線が近い texel だけを強く採る。半解像度の blur が既に広く均しているので、3x3 の texel を読む必要はない。
float UpsampleAo(float2 uv, bool depthOnlyNormals)
{
    float centerDepth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
    if (centerDepth >= 0.99999f)
    {
        return 1.0f;
    }

    float3 centerNormal = depthOnlyNormals
        ? ReconstructNormalFromDepth(uv, centerDepth)
        : DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, uv, 0));
    float3 centerWorld = ReconstructWorld(uv, centerDepth);
    float2 sourceTexel = max(gBlurParams.zw, gScreenParams.zw);
    float2 position = uv / sourceTexel - 0.5f;
    float2 base = floor(position);
    float2 fraction = position - base;

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (uint i = 0u; i < 4u; ++i)
    {
        float2 offset = float2(i & 1u, i >> 1u);
        float2 sampleUv = saturate((base + offset + 0.5f) * sourceTexel);
        float depth = gSceneDepthTex.SampleLevel(gPointClamp, sampleUv, 0).r;
        if (depth >= 0.99999f)
        {
            continue;
        }

        float3 normal = depthOnlyNormals
            ? ReconstructNormalFromDepth(sampleUv, depth)
            : DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, sampleUv, 0));
        float normalWeight = saturate(dot(centerNormal, normal));
        normalWeight *= normalWeight;
        float depthWeight = DepthAwareWeight(centerWorld, ReconstructWorld(sampleUv, depth));
        // 双線形の重みが 0 の texel も、深度と法線が一致するなら縁で拾えるよう下限を持たせる。
        float2 bilinear = lerp(1.0f - fraction, fraction, offset);
        float weight = (bilinear.x * bilinear.y + 0.01f) * normalWeight * depthWeight;
        sum += gAoTex.SampleLevel(gPointClamp, sampleUv, 0).r * weight;
        weightSum += weight;
    }

    return weightSum > 1e-4f ? sum / weightSum : gAoTex.SampleLevel(gLinearClamp, uv, 0).r;
}

float PSMainUpsample(VSOut input) : SV_TARGET
{
    return UpsampleAo(input.uv, false);
}

float PSMainUpsampleDepthOnly(VSOut input) : SV_TARGET
{
    return UpsampleAo(input.uv, true);
}
