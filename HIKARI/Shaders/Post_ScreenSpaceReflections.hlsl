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

float ReconstructViewDepth(float2 uv, float depth)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    const float4 world =
        mul(gInvViewProj, float4(ndc, depth, 1.0f));
    return rcp(max(abs(world.w), 1e-6f));
}

bool ProjectWorldPosition(
    float3 worldPosition,
    out float2 uv,
    out float projectedDepth,
    out float viewDepth)
{
    const float4 clip =
        mul(gViewProj, float4(worldPosition, 1.0f));
    if (clip.w <= 1e-5f)
    {
        uv = 0.0f;
        projectedDepth = 1.0f;
        viewDepth = 0.0f;
        return false;
    }
    const float3 ndc = clip.xyz / clip.w;
    uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
    projectedDepth = ndc.z;
    viewDepth = clip.w;
    return all(uv > 0.0f) && all(uv < 1.0f) &&
        projectedDepth > 0.0f && projectedDepth < 1.0f;
}

bool EvaluateRayDepth(
    float3 candidatePosition,
    out float2 candidateUv,
    out float candidateDepth,
    out float depthDelta,
    out float3 surfaceWorldPosition)
{
    float candidateViewDepth;
    if (!ProjectWorldPosition(
            candidatePosition,
            candidateUv,
            candidateDepth,
            candidateViewDepth))
    {
        depthDelta = 0.0f;
        surfaceWorldPosition = 0.0f.xxx;
        return false;
    }

    const float surfaceDepth =
        gCurrentHzb.SampleLevel(
            gPointClamp,
            candidateUv,
            0.0f);
    if (surfaceDepth >= 0.99999f)
    {
        depthDelta = 0.0f;
        surfaceWorldPosition = 0.0f.xxx;
        return false;
    }

    surfaceWorldPosition =
        ReconstructWorldPosition(candidateUv, surfaceDepth);
    const float surfaceViewDepth =
        mul(
            gViewProj,
            float4(surfaceWorldPosition, 1.0f)).w;
    depthDelta = candidateViewDepth - surfaceViewDepth;
    return surfaceViewDepth > 0.0f;
}

bool EvaluateProjectedDepth(
    float3 candidatePosition,
    out float2 candidateUv,
    out float candidateDepth,
    out float candidateViewDepth,
    out float surfaceDepth,
    out float surfaceViewDepth,
    out float depthDelta)
{
    if (!ProjectWorldPosition(
            candidatePosition,
            candidateUv,
            candidateDepth,
            candidateViewDepth))
    {
        surfaceDepth = 1.0f;
        surfaceViewDepth = 0.0f;
        depthDelta = 0.0f;
        return false;
    }

    // HZB mip zero is the current full-resolution SceneDepth. Recover only
    // homogeneous view depth while marching; full receiver reconstruction and
    // reprojection remain deferred until a hit candidate.
    surfaceDepth =
        gCurrentHzb.SampleLevel(
            gPointClamp,
            candidateUv,
            0.0f);
    if (surfaceDepth >= 0.99999f)
    {
        surfaceViewDepth = 0.0f;
        depthDelta = 0.0f;
        return false;
    }
    surfaceViewDepth =
        ReconstructViewDepth(candidateUv, surfaceDepth);
    depthDelta = candidateViewDepth - surfaceViewDepth;
    return surfaceViewDepth > 0.0f;
}

bool RefineRayIntersection(
    float3 rayOrigin,
    float3 rayDirection,
    float frontTravel,
    float backTravel,
    out float refinedTravel,
    out float2 refinedUv,
    out float refinedDepthDelta,
    out float3 refinedSurfaceWorldPosition)
{
    float lowTravel = frontTravel;
    float highTravel = backTravel;

    [unroll]
    for (uint refinementStep = 0u; refinementStep < 5u; ++refinementStep)
    {
        const float middleTravel =
            (lowTravel + highTravel) * 0.5f;
        float2 middleUv;
        float middleDepth;
        float middleDelta;
        float3 middleSurfaceWorldPosition;
        const bool middleValid =
            EvaluateRayDepth(
                rayOrigin + rayDirection * middleTravel,
                middleUv,
                middleDepth,
                middleDelta,
                middleSurfaceWorldPosition);
        if (!middleValid || middleDelta < 0.0f)
        {
            lowTravel = middleTravel;
        }
        else
        {
            highTravel = middleTravel;
        }
    }

    float refinedDepth;
    refinedTravel = highTravel;
    return EvaluateRayDepth(
        rayOrigin + rayDirection * refinedTravel,
        refinedUv,
        refinedDepth,
        refinedDepthDelta,
        refinedSurfaceWorldPosition);
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

    // March spacing and receiver separation are distinct controls. Keeping the
    // origin bias small avoids skipping nearby feet and thin props.
    const float originBias =
        clamp(gStride * 0.10f, 0.005f, 0.025f);
    const float3 rayOrigin =
        worldPosition + normal * originBias;

    bool hit = false;
    float2 hitUv = input.uv;
    float hitTravel = 0.0f;
    float hitDepthDelta = 0.0f;
    float hitFrontFacing = 0.0f;
    float hitRefinementWindow = 1.0f;
    float previousTravel = 0.0f;
    float previousDepthDelta = -1.0f;
    bool previousDepthValid = true;
    float travel = max(gStride * 0.25f, 0.01f);
    [loop]
    for (uint stepIndex = 0u; stepIndex < 96u; ++stepIndex)
    {
        if (stepIndex >= gMaxSteps || travel > gMaxDistance)
        {
            break;
        }

        const float3 candidatePosition =
            rayOrigin +
            reflectionDirection * travel;
        float2 candidateUv;
        float candidateDepth;
        float candidateViewDepth;
        float surfaceDepth;
        float surfaceViewDepth;
        float depthDelta;
        if (!EvaluateProjectedDepth(
                candidatePosition,
                candidateUv,
                candidateDepth,
                candidateViewDepth,
                surfaceDepth,
                surfaceViewDepth,
                depthDelta))
        {
            previousDepthValid = false;
            travel += max(gStride, travel * 0.035f);
            continue;
        }

        const float adaptiveMip =
            min(
                max(
                    floor(log2(1.0f + travel * 0.5f)) +
                        roughness * 2.0f,
                    0.0f),
                max(gHzbMipCount - 1.0f, 0.0f));
        float hierarchyDepth = surfaceDepth;
        if (adaptiveMip >= 1.0f &&
            (stepIndex & 3u) == 0u)
        {
            hierarchyDepth =
                gCurrentHzb.SampleLevel(
                    gPointClamp,
                    candidateUv,
                    adaptiveMip);
        }

        // The shared HZB is a standard-Z max hierarchy. It can identify a
        // coarse crossing, but it cannot prove the first visible intersection
        // when a tile contains both foreground geometry and background.
        // Full-resolution linear view depth therefore owns hit correctness.
        const bool hierarchyCrossing =
            adaptiveMip >= 1.0f &&
            (stepIndex & 3u) == 0u &&
            candidateDepth + gThickness >= hierarchyDepth;
        const float viewThickness =
            max(
                gThickness *
                    max(candidateViewDepth, 1.0f),
                0.0025f);
        const bool crossedSurface =
            previousDepthValid &&
            previousDepthDelta < 0.0f &&
            depthDelta >= 0.0f;
        const bool closeToSurface =
            depthDelta >= -viewThickness &&
            depthDelta <= viewThickness * 2.0f;
        if (closeToSurface ||
            crossedSurface ||
            hierarchyCrossing)
        {
            const float3 surfaceWorldPosition =
                ReconstructWorldPosition(
                    candidateUv,
                    surfaceDepth);
            float refinedTravel = travel;
            float2 refinedUv = candidateUv;
            float refinedDepthDelta = depthDelta;
            float3 refinedSurfaceWorldPosition =
                surfaceWorldPosition;
            bool refinedValid = true;
            if (crossedSurface)
            {
                refinedValid = RefineRayIntersection(
                    rayOrigin,
                    reflectionDirection,
                    previousTravel,
                    travel,
                    refinedTravel,
                    refinedUv,
                    refinedDepthDelta,
                    refinedSurfaceWorldPosition);
            }
            if (!refinedValid)
            {
                previousDepthValid = false;
                travel += max(gStride, travel * 0.035f);
                continue;
            }

            float3 hitNormal;
            float hitRoughness;
            float hitMetallic;
            HikariUnpackSceneSurface(
                gSceneSurface.SampleLevel(
                    gPointClamp,
                    refinedUv,
                    0.0f),
                hitNormal,
                hitRoughness,
                hitMetallic);
            const float frontFacing =
                dot(hitNormal, -reflectionDirection);
            const float planeSeparation =
                abs(
                    dot(
                        refinedSurfaceWorldPosition - worldPosition,
                        normal));
            const bool sameReceiverPlane =
                dot(hitNormal, normal) > 0.96f &&
                planeSeparation < max(originBias * 2.0f, 0.01f);
            const float refinementWindow =
                max(
                    viewThickness * 4.0f,
                    (travel - previousTravel) * 0.75f);
            if (frontFacing > 0.05f &&
                !sameReceiverPlane &&
                refinedDepthDelta >= -viewThickness &&
                refinedDepthDelta <= refinementWindow)
            {
                hit = true;
                hitUv = refinedUv;
                hitTravel = refinedTravel;
                hitDepthDelta = refinedDepthDelta;
                hitFrontFacing = frontFacing;
                hitRefinementWindow = refinementWindow;
                break;
            }
        }

        previousDepthValid = true;
        previousTravel = travel;
        previousDepthDelta = depthDelta;
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
    const float intersectionConfidence =
        hit
            ? saturate(
                hitFrontFacing *
                (1.0f - abs(hitDepthDelta) /
                    max(hitRefinementWindow, 0.01f)))
            : 0.0f;
    const float confidence =
        edgeConfidence *
        distanceConfidence *
        roughnessConfidence *
        lerp(0.35f, 1.0f, facingConfidence) *
        lerp(0.35f, 1.0f, intersectionConfidence);

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
