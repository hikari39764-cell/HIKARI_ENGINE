#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"

cbuffer ScreenSpaceReflectionCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPosition;
    float4 gScreenParams;
    float4 gTraceParams;
    float4 gResolveParams;
    float4 gFrameParams;
};

#define gMaxDistance gTraceParams.x
#define gThickness gTraceParams.y
#define gIntensity gTraceParams.z
#define gMaxRoughness gTraceParams.w
#define gStride gResolveParams.x
#define gMaxSteps ((uint)gResolveParams.y)
#define gHzbMipCount gResolveParams.z
#define gEdgeFade gResolveParams.w
#define gDebugMode ((uint)gFrameParams.x)

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gSceneSurface : register(t2);
Texture2D<float> gCurrentHzb : register(t3);
SamplerState gLinearClamp : register(s0);
SamplerState gPointClamp : register(s1);

struct FullscreenVertex
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

FullscreenVertex VSMain(uint vertexId : SV_VertexID)
{
    FullscreenVertex output;
    const float2 position =
        float2((vertexId << 1u) & 2u, vertexId & 2u);
    output.uv = position;
    output.position =
        float4(
            position * float2(2.0f, -2.0f) +
                float2(-1.0f, 1.0f),
            0.0f,
            1.0f);
    return output;
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    const float4 world =
        mul(gInvViewProj, float4(ndc, depth, 1.0f));
    return world.xyz / max(abs(world.w), 1e-6f);
}

bool ProjectWorldPosition(
    float3 worldPosition,
    out float2 uv,
    out float projectedDepth)
{
    const float4 clip =
        mul(gViewProj, float4(worldPosition, 1.0f));
    if (clip.w <= 1e-5f)
    {
        uv = 0.0f;
        projectedDepth = 1.0f;
        return false;
    }
    const float3 ndc = clip.xyz / clip.w;
    uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
    projectedDepth = ndc.z;
    return all(uv > 0.0f) && all(uv < 1.0f) &&
        projectedDepth > 0.0f && projectedDepth < 1.0f;
}

float ScreenEdgeConfidence(float2 uv)
{
    const float edgeDistance =
        min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    return smoothstep(0.0f, max(gEdgeFade, 1e-4f), edgeDistance);
}

float4 PSMain(FullscreenVertex input) : SV_TARGET
{
    const float4 baseSceneColor =
        gSceneColor.SampleLevel(gLinearClamp, input.uv, 0.0f);
    const float depth =
        gSceneDepth.SampleLevel(gPointClamp, input.uv, 0.0f);
    if (depth >= 0.99999f)
    {
        return baseSceneColor;
    }

    float3 normal;
    float roughness;
    float metallic;
    HikariUnpackSceneSurface(
        gSceneSurface.SampleLevel(gPointClamp, input.uv, 0.0f),
        normal,
        roughness,
        metallic);
    if (roughness >= gMaxRoughness)
    {
        return gDebugMode == 1u
            ? float4(0.0f, 0.0f, 0.0f, 1.0f)
            : baseSceneColor;
    }

    const float3 worldPosition =
        ReconstructWorldPosition(input.uv, depth);
    const float3 incident =
        normalize(worldPosition - gCameraPosition.xyz);
    const float3 reflectionDirection =
        normalize(reflect(incident, normal));
    if (dot(reflectionDirection, normal) <= 0.0001f)
    {
        return baseSceneColor;
    }

    bool hit = false;
    float2 hitUv = input.uv;
    float hitTravel = 0.0f;
    float travel = max(gStride, 0.01f);
    [loop]
    for (uint stepIndex = 0u; stepIndex < 96u; ++stepIndex)
    {
        if (stepIndex >= gMaxSteps || travel > gMaxDistance)
        {
            break;
        }

        const float3 candidatePosition =
            worldPosition + normal * max(gStride, 0.01f) +
            reflectionDirection * travel;
        float2 candidateUv;
        float candidateDepth;
        if (!ProjectWorldPosition(
                candidatePosition,
                candidateUv,
                candidateDepth))
        {
            break;
        }

        const float adaptiveMip =
            min(
                max(
                    floor(log2(1.0f + travel * 0.5f)) +
                        roughness * 2.0f,
                    0.0f),
                max(gHzbMipCount - 1.0f, 0.0f));
        const float hierarchyDepth =
            gCurrentHzb.SampleLevel(
                gPointClamp,
                candidateUv,
                adaptiveMip);

        // A max-reduction HZB can reject candidates still in front of every
        // surface in the tile. Full-resolution depth confirms the crossing.
        if (candidateDepth + gThickness >= hierarchyDepth)
        {
            const float surfaceDepth =
                gSceneDepth.SampleLevel(
                    gPointClamp,
                    candidateUv,
                    0.0f);
            const float depthDelta = candidateDepth - surfaceDepth;
            if (surfaceDepth < 0.99999f &&
                depthDelta >= -gThickness &&
                depthDelta <= gThickness * 4.0f)
            {
                hit = true;
                hitUv = candidateUv;
                hitTravel = travel;
                break;
            }
        }
        travel += max(gStride, travel * 0.035f);
    }

    const float edgeConfidence =
        hit ? ScreenEdgeConfidence(hitUv) : 0.0f;
    const float distanceConfidence =
        hit ? saturate(1.0f - hitTravel / max(gMaxDistance, 1e-3f)) : 0.0f;
    const float roughnessConfidence =
        1.0f - smoothstep(
            gMaxRoughness * 0.55f,
            gMaxRoughness,
            roughness);
    const float facingConfidence =
        saturate(1.0f - abs(dot(-incident, normal)));
    const float confidence =
        edgeConfidence *
        distanceConfidence *
        roughnessConfidence *
        lerp(0.35f, 1.0f, facingConfidence);

    if (gDebugMode == 1u)
    {
        return float4(confidence.xxx, 1.0f);
    }
    if (gDebugMode == 2u)
    {
        return float4(hit ? hitUv : 0.0f, hit ? 1.0f : 0.0f, 1.0f);
    }
    if (!hit || confidence <= 0.0001f)
    {
        return baseSceneColor;
    }

    const float3 reflectedRadiance =
        gSceneColor.SampleLevel(gLinearClamp, hitUv, 0.0f).rgb;
    const float viewNormal = saturate(dot(-incident, normal));
    const float dielectricFresnel =
        0.04f + 0.96f * pow(1.0f - viewNormal, 5.0f);
    const float reflectivity =
        saturate(lerp(dielectricFresnel, 0.85f, metallic));
    const float replacementWeight =
        saturate(confidence * reflectivity * gIntensity);

    // The forward path already contains probe/sky specular. Remove the
    // estimated fallback lobe before applying the higher-confidence screen hit.
    const float3 estimatedFallback =
        baseSceneColor.rgb * reflectivity * 0.25f;
    const float3 resolved =
        max(baseSceneColor.rgb - estimatedFallback * replacementWeight, 0.0f) +
        reflectedRadiance * replacementWeight;
    return float4(resolved, baseSceneColor.a);
}
