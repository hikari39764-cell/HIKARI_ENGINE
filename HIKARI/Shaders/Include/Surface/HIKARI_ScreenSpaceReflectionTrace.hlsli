#ifndef HIKARI_SCREEN_SPACE_REFLECTION_TRACE_INCLUDED
#define HIKARI_SCREEN_SPACE_REFLECTION_TRACE_INCLUDED

#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"

// 任意の world-space surface origin から current SceneDepth を追跡する共通契約。
// Full-resolution depth が hit correctness を所有し、max-HZB は歩幅の hint に限る。

struct HikariScreenSpaceReflectionTraceConfig
{
    uint valid;
    uint maxSteps;
    uint hzbMipCount;
    float maxDistance;
    float thickness;
    float maxRoughness;
    float stride;
    float edgeFade;
};

struct HikariScreenSpaceReflectionTraceResult
{
    float3 radiance;
    float confidence;
    float2 hitUv;
    uint hit;
};

bool HikariIsValidScreenSpaceReflectionRadiance(float3 radiance)
{
    return all(radiance == radiance) &&
        all(radiance >= 0.0f.xxx) &&
        all(radiance < 65504.0f.xxx);
}

bool HikariIsFiniteScreenSpaceReflectionVector(float3 value)
{
    return all(value == value) && all(abs(value) < 1.0e30f.xxx);
}

HikariScreenSpaceReflectionTraceResult
HikariTraceScreenSpaceReflectionFromSurface(
    Texture2D<float4> sceneColor,
    Texture2D<float> fullResolutionSceneDepth,
    Texture2D<float> currentDepthPyramid,
    Texture2D<float4> packedSceneSurface,
    SamplerState linearClamp,
    SamplerState pointClamp,
    float3 surfaceWorldPosition,
    float3 surfaceNormal,
    float surfaceRoughness,
    float3 cameraWorldPosition,
    float4x4 viewProjection,
    float4x4 inverseViewProjection,
    HikariScreenSpaceReflectionTraceConfig config)
{
    HikariScreenSpaceReflectionTraceResult result;
    result.radiance = 0.0f.xxx;
    result.confidence = 0.0f;
    result.hitUv = 0.0f.xx;
    result.hit = 0u;

    if (config.valid == 0u ||
        config.maxSteps == 0u ||
        config.hzbMipCount == 0u ||
        config.maxDistance <= 0.0f ||
        config.maxDistance != config.maxDistance ||
        config.thickness != config.thickness ||
        config.maxRoughness != config.maxRoughness ||
        config.stride != config.stride ||
        config.edgeFade != config.edgeFade ||
        surfaceRoughness != surfaceRoughness ||
        !HikariIsFiniteScreenSpaceReflectionVector(
            surfaceWorldPosition) ||
        !HikariIsFiniteScreenSpaceReflectionVector(surfaceNormal) ||
        !HikariIsFiniteScreenSpaceReflectionVector(
            cameraWorldPosition) ||
        surfaceRoughness >= config.maxRoughness)
    {
        return result;
    }

    const float normalLengthSquared = dot(surfaceNormal, surfaceNormal);
    const float3 incidentVector =
        surfaceWorldPosition - cameraWorldPosition;
    const float incidentLengthSquared = dot(
        incidentVector,
        incidentVector);
    if (normalLengthSquared <= 1.0e-12f ||
        incidentLengthSquared <= 1.0e-12f)
    {
        return result;
    }
    const float3 normal =
        surfaceNormal * rsqrt(normalLengthSquared);
    const float3 incident =
        incidentVector * rsqrt(incidentLengthSquared);
    const float3 reflected = reflect(incident, normal);
    const float reflectedLengthSquared = dot(reflected, reflected);
    if (reflectedLengthSquared <= 1.0e-12f)
    {
        return result;
    }
    const float3 reflectionDirection =
        reflected * rsqrt(reflectedLengthSquared);
    if (dot(reflectionDirection, normal) <= 0.0001f)
    {
        return result;
    }

    const float stride = max(config.stride, 0.01f);
    const float originBias = clamp(stride * 0.10f, 0.005f, 0.025f);
    const float3 rayOrigin =
        surfaceWorldPosition + normal * originBias;

    bool hit = false;
    float2 hitUv = 0.0f.xx;
    float hitTravel = 0.0f;
    float hitDepthDelta = 0.0f;
    float hitFrontFacing = 0.0f;
    float hitRefinementWindow = 1.0f;
    float previousTravel = 0.0f;
    float previousDepthDelta = -1.0f;
    bool previousDepthValid = true;
    float travel = max(stride * 0.25f, 0.01f);

    [loop]
    for (uint stepIndex = 0u; stepIndex < 96u; ++stepIndex)
    {
        if (stepIndex >= config.maxSteps ||
            travel > config.maxDistance)
        {
            break;
        }

        const float3 candidatePosition =
            rayOrigin + reflectionDirection * travel;
        float2 candidateUv;
        float candidateDepth;
        float candidateViewDepth;
        if (!HikariProjectWorldPosition(
                candidatePosition,
                viewProjection,
                candidateUv,
                candidateDepth,
                candidateViewDepth))
        {
            previousDepthValid = false;
            travel += max(stride, travel * 0.035f);
            continue;
        }

        const float surfaceDepth = fullResolutionSceneDepth.SampleLevel(
            pointClamp,
            candidateUv,
            0.0f);
        float receiverViewDepth;
        if (!HikariReconstructViewDepth(
                candidateUv,
                surfaceDepth,
                inverseViewProjection,
                receiverViewDepth))
        {
            previousDepthValid = false;
            travel += max(stride, travel * 0.035f);
            continue;
        }
        const float depthDelta =
            candidateViewDepth - receiverViewDepth;
        const float adaptiveMip = min(
            max(
                floor(log2(1.0f + travel * 0.5f)) +
                    surfaceRoughness * 2.0f,
                0.0f),
            max(float(config.hzbMipCount) - 1.0f, 0.0f));
        const bool hierarchySampled = (stepIndex & 3u) == 0u;
        float hierarchyDepth = 1.0f;
        if (hierarchySampled)
        {
            hierarchyDepth = currentDepthPyramid.SampleLevel(
                pointClamp,
                candidateUv,
                adaptiveMip);
        }

        const float viewThickness = max(
            config.thickness * max(candidateViewDepth, 1.0f),
            0.0025f);
        const bool crossedSurface =
            previousDepthValid &&
            previousDepthDelta < 0.0f &&
            depthDelta >= 0.0f;
        const bool closeToSurface =
            depthDelta >= -viewThickness &&
            depthDelta <= viewThickness * 2.0f;
        const bool hierarchyCrossing =
            hierarchySampled &&
            candidateDepth >= hierarchyDepth;

        if (closeToSurface || crossedSurface)
        {
            float refinedTravel = travel;
            float2 refinedUv = candidateUv;
            float refinedDepthDelta = depthDelta;
            float3 refinedReceiverWorldPosition;
            bool refinedValid = HikariReconstructWorldPosition(
                candidateUv,
                surfaceDepth,
                inverseViewProjection,
                refinedReceiverWorldPosition);
            if (crossedSurface)
            {
                float lowTravel = previousTravel;
                float highTravel = travel;
                [unroll]
                for (uint refinementStep = 0u;
                     refinementStep < 5u;
                     ++refinementStep)
                {
                    const float middleTravel =
                        (lowTravel + highTravel) * 0.5f;
                    const float3 middlePosition =
                        rayOrigin + reflectionDirection * middleTravel;
                    float2 middleUv;
                    float middleDepth;
                    float middleViewDepth;
                    if (!HikariProjectWorldPosition(
                            middlePosition,
                            viewProjection,
                            middleUv,
                            middleDepth,
                            middleViewDepth))
                    {
                        lowTravel = middleTravel;
                        continue;
                    }
                    const float middleSurfaceDepth =
                        fullResolutionSceneDepth.SampleLevel(
                            pointClamp,
                            middleUv,
                            0.0f);
                    float middleReceiverViewDepth;
                    if (!HikariReconstructViewDepth(
                            middleUv,
                            middleSurfaceDepth,
                            inverseViewProjection,
                            middleReceiverViewDepth))
                    {
                        lowTravel = middleTravel;
                        continue;
                    }
                    const float middleDepthDelta =
                        middleViewDepth - middleReceiverViewDepth;
                    if (middleDepthDelta < 0.0f)
                    {
                        lowTravel = middleTravel;
                    }
                    else
                    {
                        highTravel = middleTravel;
                    }
                }

                refinedTravel = highTravel;
                const float3 refinedPosition =
                    rayOrigin + reflectionDirection * refinedTravel;
                float refinedProjectedDepth;
                float refinedViewDepth;
                refinedValid = HikariProjectWorldPosition(
                    refinedPosition,
                    viewProjection,
                    refinedUv,
                    refinedProjectedDepth,
                    refinedViewDepth);
                const float refinedSurfaceDepth = refinedValid
                    ? fullResolutionSceneDepth.SampleLevel(
                        pointClamp,
                        refinedUv,
                        0.0f)
                    : 1.0f;
                refinedValid = refinedValid &&
                    HikariReconstructWorldPosition(
                        refinedUv,
                        refinedSurfaceDepth,
                        inverseViewProjection,
                        refinedReceiverWorldPosition);
                if (refinedValid)
                {
                    float refinedReceiverViewDepth;
                    refinedValid = HikariReconstructViewDepth(
                        refinedUv,
                        refinedSurfaceDepth,
                        inverseViewProjection,
                        refinedReceiverViewDepth);
                    if (refinedValid)
                    {
                        refinedDepthDelta =
                            refinedViewDepth - refinedReceiverViewDepth;
                    }
                }
            }

            if (refinedValid)
            {
                float3 receiverNormal;
                float receiverRoughness;
                float receiverMetallic;
                HikariUnpackSceneSurface(
                    packedSceneSurface.SampleLevel(
                        pointClamp,
                        refinedUv,
                        0.0f),
                    receiverNormal,
                    receiverRoughness,
                    receiverMetallic);
                const float frontFacing =
                    dot(receiverNormal, -reflectionDirection);
                const float planeSeparation = abs(dot(
                    refinedReceiverWorldPosition - surfaceWorldPosition,
                    normal));
                const bool sameReceiverPlane =
                    dot(receiverNormal, normal) > 0.96f &&
                    planeSeparation < max(originBias * 2.0f, 0.01f);
                const float refinementWindow = max(
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
        }

        previousDepthValid = true;
        previousTravel = travel;
        previousDepthDelta = depthDelta;
        const float hierarchyAdvance = hierarchyCrossing
            ? stride * 0.5f
            : stride;
        travel += max(hierarchyAdvance, travel * 0.035f);
    }

    if (!hit)
    {
        return result;
    }

    const float edgeDistance = min(
        min(hitUv.x, 1.0f - hitUv.x),
        min(hitUv.y, 1.0f - hitUv.y));
    const float edgeConfidence = smoothstep(
        0.0f,
        max(config.edgeFade, 1.0e-4f),
        edgeDistance);
    const float distanceConfidence = saturate(
        1.0f - hitTravel / max(config.maxDistance, 1.0e-3f));
    const float roughnessConfidence =
        1.0f - smoothstep(
            config.maxRoughness * 0.55f,
            config.maxRoughness,
            surfaceRoughness);
    const float facingConfidence = saturate(
        1.0f - abs(dot(-incident, normal)));
    const float intersectionConfidence = saturate(
        hitFrontFacing *
        (1.0f - abs(hitDepthDelta) /
            max(hitRefinementWindow, 0.01f)));
    const float confidence =
        edgeConfidence *
        distanceConfidence *
        roughnessConfidence *
        lerp(0.35f, 1.0f, facingConfidence) *
        lerp(0.35f, 1.0f, intersectionConfidence);

    const float3 radiance = sceneColor.SampleLevel(
        linearClamp,
        hitUv,
        0.0f).rgb;
    if (confidence <= 0.0001f ||
        !HikariIsValidScreenSpaceReflectionRadiance(radiance))
    {
        return result;
    }

    result.radiance = radiance;
    result.confidence = saturate(confidence);
    result.hitUv = hitUv;
    result.hit = 1u;
    return result;
}

#endif
