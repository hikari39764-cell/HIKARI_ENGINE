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
#define gAoSampleCount gAoParams1.x
#define gFrameIndex gAoParams1.y
#define gDepthOnlyNormals gAoParams1.w

static const float kTwoPi = 6.28318530718f;
static const float kGoldenAngle = 2.39996322973f;

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
    return normalize(packed.xyz * 2.0f - 1.0f);
}

float3 ReconstructWorld(float2 uv, float depth)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 world = mul(gInvViewProj, float4(ndc, depth, 1.0f));
    return world.xyz / max(abs(world.w), 1e-5f);
}

float2 ProjectWorld(float3 world)
{
    float4 clip = mul(gViewProj, float4(world, 1.0f));
    float3 ndc = clip.xyz / max(abs(clip.w), 1e-5f);
    return float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
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

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float3 SampleKernel(uint index, float angle)
{
    static const float3 kSamples[32] = {
        float3( 0.5381f,  0.1856f,  0.4319f), float3( 0.1379f,  0.2486f,  0.4430f),
        float3( 0.3371f,  0.5679f,  0.0057f), float3(-0.6999f, -0.0451f,  0.0019f),
        float3( 0.0689f, -0.1598f,  0.8547f), float3( 0.0560f,  0.0069f,  0.1843f),
        float3(-0.0146f,  0.1402f,  0.0762f), float3( 0.0100f, -0.1924f,  0.0344f),
        float3(-0.3577f, -0.5301f,  0.4358f), float3(-0.3169f,  0.1063f,  0.0158f),
        float3( 0.0103f, -0.5869f,  0.0046f), float3(-0.0897f, -0.4940f,  0.3287f),
        float3( 0.7119f, -0.0154f,  0.0918f), float3(-0.0533f,  0.0596f,  0.5411f),
        float3( 0.0352f, -0.0631f,  0.5460f), float3(-0.4776f,  0.2847f,  0.0271f),
        float3(-0.0483f, -0.0833f,  0.0503f), float3(-0.1959f,  0.3624f,  0.4353f),
        float3(-0.3566f,  0.0135f,  0.4821f), float3( 0.1030f, -0.1127f,  0.7812f),
        float3( 0.1316f,  0.2841f,  0.2977f), float3(-0.1975f, -0.3839f,  0.3994f),
        float3( 0.3828f, -0.1567f,  0.4751f), float3(-0.4609f,  0.1411f,  0.2927f),
        float3( 0.3012f,  0.2178f,  0.5945f), float3(-0.2057f,  0.3228f,  0.6420f),
        float3( 0.0644f, -0.4475f,  0.5692f), float3( 0.4165f, -0.3861f,  0.3912f),
        float3(-0.5904f, -0.1559f,  0.3025f), float3( 0.5107f,  0.4422f,  0.1978f),
        float3(-0.2961f,  0.5774f,  0.3397f), float3( 0.0747f, -0.7116f,  0.2181f)
    };

    float s = sin(angle);
    float c = cos(angle);
    float3 v = kSamples[index];
    return normalize(float3(v.x * c - v.y * s, v.x * s + v.y * c, abs(v.z)));
}

float PSMain(VSOut input) : SV_TARGET
{
    float2 texel = gScreenParams.zw;
    float depth = gSceneDepthTex.SampleLevel(gPointClamp, input.uv, 0).r;
    if (depth >= 0.99999f)
    {
        return 1.0f;
    }

    float4 normalRoughness = gNormalRoughnessTex.SampleLevel(gPointClamp, input.uv, 0);
    float3 n = DecodeNormal(normalRoughness);
    float3 worldPos = ReconstructWorld(input.uv, depth);
    float randomAngle = Hash12(input.uv * gScreenParams.xy + gFrameIndex) * kTwoPi;
    float sampleCount = clamp(gAoSampleCount, 1.0f, 32.0f);

    float occlusion = 0.0f;
    [loop]
    for (uint i = 0; i < 32; ++i)
    {
        if (i >= (uint)sampleCount)
        {
            break;
        }

        float scale = (float(i) + 0.5f) / sampleCount;
        scale = lerp(0.2f, 1.0f, scale * scale);
        float3 dir = SampleKernel(i, randomAngle);
        if (dot(dir, n) < 0.0f)
        {
            dir = -dir;
        }

        float3 sampleWorld = worldPos + dir * gAoRadius * scale;
        float2 sampleUv = ProjectWorld(sampleWorld);
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
        {
            continue;
        }

        float sampleDepth = gSceneDepthTex.SampleLevel(gPointClamp, sampleUv, 0).r;
        if (sampleDepth >= 0.99999f)
        {
            continue;
        }

        float3 hitWorld = ReconstructWorld(sampleUv, sampleDepth);
        float3 delta = hitWorld - worldPos;
        float distanceToHit = length(delta);
        float facing = saturate(dot(n, normalize(delta)));
        float range = saturate(1.0f - distanceToHit / max(gAoRadius, 1e-4f));
        float hit = (distanceToHit > gAoBias && distanceToHit < gAoRadius && facing > 0.05f) ? 1.0f : 0.0f;
        occlusion += hit * range * facing;
    }

    float ao = 1.0f - saturate((occlusion / sampleCount) * gAoStrength);
    return pow(saturate(ao), gAoPower);
}

float2 VogelDisk(uint index, float sampleCount, float angle)
{
    float t = (float(index) + 0.5f) / max(sampleCount, 1.0f);
    float r = sqrt(t);
    float theta = float(index) * kGoldenAngle + angle;
    return float2(cos(theta), sin(theta)) * r;
}

float ScreenRadiusFromWorldRadius(float2 centerUv, float3 worldPos, float3 normal)
{
    float3 up = abs(normal.y) < 0.98f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float2 radiusUv = ProjectWorld(worldPos + tangent * gAoRadius);
    float radius = length(radiusUv - centerUv);
    float minRadius = max(gScreenParams.z, gScreenParams.w) * 2.0f;
    return clamp(radius, minRadius, 0.12f);
}

// Spiral sampling で Reference より軽い AO を作る。
float PSMainOptimizedHigh(VSOut input) : SV_TARGET
{
    float depth = gSceneDepthTex.SampleLevel(gPointClamp, input.uv, 0).r;
    if (depth >= 0.99999f)
    {
        return 1.0f;
    }

    float4 normalRoughness = gNormalRoughnessTex.SampleLevel(gPointClamp, input.uv, 0);
    float3 n = DecodeNormal(normalRoughness);
    float3 worldPos = ReconstructWorld(input.uv, depth);
    float randomAngle = Hash12(input.uv * gScreenParams.xy + gFrameIndex * 7.13f) * kTwoPi;
    float sampleCount = clamp(gAoSampleCount, 1.0f, 32.0f);
    float screenRadius = ScreenRadiusFromWorldRadius(input.uv, worldPos, n);

    float occlusion = 0.0f;
    float weightSum = 0.0f;

    [loop]
    for (uint i = 0; i < 32; ++i)
    {
        if (i >= (uint)sampleCount)
        {
            break;
        }

        float2 offset = VogelDisk(i, sampleCount, randomAngle);
        float2 sampleUv = input.uv + offset * screenRadius;
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
        {
            continue;
        }

        float sampleDepth = gSceneDepthTex.SampleLevel(gPointClamp, sampleUv, 0).r;
        if (sampleDepth >= 0.99999f)
        {
            continue;
        }

        float3 hitWorld = ReconstructWorld(sampleUv, sampleDepth);
        float3 delta = hitWorld - worldPos;
        float distanceToHit = length(delta);
        float3 dir = delta / max(distanceToHit, 1e-4f);
        float facing = saturate(dot(n, dir));
        float range = saturate(1.0f - distanceToHit / max(gAoRadius, 1e-4f));
        range = range * range * (3.0f - 2.0f * range);

        float radialWeight = 1.0f - saturate(length(offset));
        float hit = (distanceToHit > gAoBias && distanceToHit < gAoRadius && facing > 0.03f) ? 1.0f : 0.0f;
        float weight = max(radialWeight, 0.15f);
        occlusion += hit * range * facing * weight;
        weightSum += weight;
    }

    float ao = 1.0f - saturate((occlusion / max(weightSum, 1e-4f)) * gAoStrength);
    return pow(saturate(ao), gAoPower);
}

float PSMainDepthOnly(VSOut input) : SV_TARGET
{
    float depth = gSceneDepthTex.SampleLevel(gPointClamp, input.uv, 0).r;
    if (depth >= 0.99999f)
    {
        return 1.0f;
    }

    float3 n = ReconstructNormalFromDepth(input.uv, depth);
    float3 worldPos = ReconstructWorld(input.uv, depth);
    float randomAngle = Hash12(input.uv * gScreenParams.xy + gFrameIndex * 7.13f) * kTwoPi;
    float sampleCount = clamp(gAoSampleCount, 1.0f, 32.0f);
    float screenRadius = ScreenRadiusFromWorldRadius(input.uv, worldPos, n);

    float occlusion = 0.0f;
    float weightSum = 0.0f;

    [loop]
    for (uint i = 0; i < 32; ++i)
    {
        if (i >= (uint)sampleCount)
        {
            break;
        }

        float2 offset = VogelDisk(i, sampleCount, randomAngle);
        float2 sampleUv = input.uv + offset * screenRadius;
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
        {
            continue;
        }

        float sampleDepth = gSceneDepthTex.SampleLevel(gPointClamp, sampleUv, 0).r;
        if (sampleDepth >= 0.99999f)
        {
            continue;
        }

        float3 hitWorld = ReconstructWorld(sampleUv, sampleDepth);
        float3 delta = hitWorld - worldPos;
        float distanceToHit = length(delta);
        float3 dir = delta / max(distanceToHit, 1e-4f);
        float facing = saturate(dot(n, dir));
        float range = saturate(1.0f - distanceToHit / max(gAoRadius, 1e-4f));
        range = range * range * (3.0f - 2.0f * range);

        float radialWeight = 1.0f - saturate(length(offset));
        float hit = (distanceToHit > gAoBias && distanceToHit < gAoRadius && facing > 0.03f) ? 1.0f : 0.0f;
        float weight = max(radialWeight, 0.15f);
        occlusion += hit * range * facing * weight;
        weightSum += weight;
    }

    float ao = 1.0f - saturate((occlusion / max(weightSum, 1e-4f)) * gAoStrength);
    return pow(saturate(ao), gAoPower);
}
