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
    return normalize(packed.xyz * 2.0f - 1.0f);
}

float3 ReconstructWorld(float2 uv, float depth)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 world = mul(gInvViewProj, float4(ndc, depth, 1.0f));
    return world.xyz / max(abs(world.w), 1e-5f);
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

float PSMain(VSOut input) : SV_TARGET
{
    float2 texel = gScreenParams.zw * gBlurParams.xy;
    float centerDepth = gSceneDepthTex.SampleLevel(gPointClamp, input.uv, 0).r;
    if (centerDepth >= 0.99999f)
    {
        return gAoTex.SampleLevel(gLinearClamp, input.uv, 0).r;
    }

    float3 centerNormal = DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, input.uv, 0));
    float3 centerWorld = ReconstructWorld(input.uv, centerDepth);

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int i = -2; i <= 2; ++i)
    {
        float2 uv = input.uv + texel * float(i);
        float ao = gAoTex.SampleLevel(gLinearClamp, uv, 0).r;
        float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
        if (depth >= 0.99999f)
        {
            continue;
        }

        float3 normal = DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, uv, 0));
        float3 sampleWorld = ReconstructWorld(uv, depth);
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

// 半解像度 AO を全解像度へ戻す。深度と法線が近いサンプルだけを強く採用する。
float PSMainUpsample(VSOut input) : SV_TARGET
{
    float centerDepth = gSceneDepthTex.SampleLevel(gPointClamp, input.uv, 0).r;
    if (centerDepth >= 0.99999f)
    {
        return 1.0f;
    }

    float3 centerNormal = DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, input.uv, 0));
    float3 centerWorld = ReconstructWorld(input.uv, centerDepth);
    float2 sourceTexel = max(gBlurParams.zw, gScreenParams.zw);
    float centerAo = gAoTex.SampleLevel(gLinearClamp, input.uv, 0).r;

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 uv = saturate(input.uv + float2(x, y) * sourceTexel);
            float ao = gAoTex.SampleLevel(gLinearClamp, uv, 0).r;
            float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
            if (depth >= 0.99999f)
            {
                continue;
            }

            float3 normal = DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, uv, 0));
            float3 sampleWorld = ReconstructWorld(uv, depth);
            float normalWeight = saturate(dot(centerNormal, normal));
            normalWeight *= normalWeight;
            float depthWeight = DepthAwareWeight(centerWorld, sampleWorld);
            float kernelWeight = (x == 0 && y == 0) ? 0.34f : ((abs(x) + abs(y)) == 1 ? 0.14f : 0.08f);
            float weight = kernelWeight * normalWeight * depthWeight;
            sum += ao * weight;
            weightSum += weight;
        }
    }

    return weightSum > 1e-4f ? sum / weightSum : centerAo;
}

float PSMainDepthOnly(VSOut input) : SV_TARGET
{
    float2 texel = gScreenParams.zw * gBlurParams.xy;
    float centerDepth = gSceneDepthTex.SampleLevel(gPointClamp, input.uv, 0).r;
    if (centerDepth >= 0.99999f)
    {
        return gAoTex.SampleLevel(gLinearClamp, input.uv, 0).r;
    }

    float3 centerNormal = ReconstructNormalFromDepth(input.uv, centerDepth);
    float3 centerWorld = ReconstructWorld(input.uv, centerDepth);

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int i = -2; i <= 2; ++i)
    {
        float2 uv = input.uv + texel * float(i);
        float ao = gAoTex.SampleLevel(gLinearClamp, uv, 0).r;
        float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
        if (depth >= 0.99999f)
        {
            continue;
        }

        float3 normal = ReconstructNormalFromDepth(uv, depth);
        float3 sampleWorld = ReconstructWorld(uv, depth);
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

float PSMainUpsampleDepthOnly(VSOut input) : SV_TARGET
{
    float centerDepth = gSceneDepthTex.SampleLevel(gPointClamp, input.uv, 0).r;
    if (centerDepth >= 0.99999f)
    {
        return 1.0f;
    }

    float3 centerNormal = ReconstructNormalFromDepth(input.uv, centerDepth);
    float3 centerWorld = ReconstructWorld(input.uv, centerDepth);
    float2 sourceTexel = max(gBlurParams.zw, gScreenParams.zw);
    float centerAo = gAoTex.SampleLevel(gLinearClamp, input.uv, 0).r;

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 uv = saturate(input.uv + float2(x, y) * sourceTexel);
            float ao = gAoTex.SampleLevel(gLinearClamp, uv, 0).r;
            float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
            if (depth >= 0.99999f)
            {
                continue;
            }

            float3 normal = ReconstructNormalFromDepth(uv, depth);
            float3 sampleWorld = ReconstructWorld(uv, depth);
            float normalWeight = saturate(dot(centerNormal, normal));
            normalWeight *= normalWeight;
            float depthWeight = DepthAwareWeight(centerWorld, sampleWorld);
            float kernelWeight = (x == 0 && y == 0) ? 0.34f : ((abs(x) + abs(y)) == 1 ? 0.14f : 0.08f);
            float weight = kernelWeight * normalWeight * depthWeight;
            sum += ao * weight;
            weightSum += weight;
        }
    }

    return weightSum > 1e-4f ? sum / weightSum : centerAo;
}
