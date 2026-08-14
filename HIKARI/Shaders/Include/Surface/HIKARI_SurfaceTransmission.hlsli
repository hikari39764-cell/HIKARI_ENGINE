#ifndef HIKARI_SURFACE_TRANSMISSION_INCLUDED
#define HIKARI_SURFACE_TRANSMISSION_INCLUDED

#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"

float3 HikariReferenceTransmittance(
    float3 transmittanceAtReferenceDistance,
    float referenceDistance,
    float distance)
{
    const float exponent =
        max(distance, 0.0f) / max(referenceDistance, 1.0e-4f);
    return pow(
        clamp(transmittanceAtReferenceDistance, 1.0e-4f, 1.0f),
        exponent.xxx);
}

float HikariScreenEdgeConfidence(
    float2 uv,
    float2 inverseScreenSize,
    float fadePixels)
{
    const float2 edgeDistance = min(uv, 1.0f.xx - uv);
    const float distancePixels = min(
        edgeDistance.x / max(inverseScreenSize.x, 1.0e-6f),
        edgeDistance.y / max(inverseScreenSize.y, 1.0e-6f));
    return saturate(distancePixels / max(fadePixels, 1.0f));
}

bool HikariProjectWorldDirectionToUvOffset(
    float3 worldPosition,
    float3 worldDirection,
    float worldDistance,
    float4x4 viewProjection,
    out float2 uvOffset)
{
    float2 originUv;
    float originDepth;
    float originViewDepth;
    float2 targetUv;
    float targetDepth;
    float targetViewDepth;
    const bool originValid = HikariProjectWorldPosition(
        worldPosition,
        viewProjection,
        originUv,
        originDepth,
        originViewDepth);
    const bool targetValid = HikariProjectWorldPosition(
        worldPosition + worldDirection * worldDistance,
        viewProjection,
        targetUv,
        targetDepth,
        targetViewDepth);
    uvOffset = targetValid && originValid
        ? targetUv - originUv
        : 0.0f.xx;
    return originValid && targetValid;
}

bool HikariValidateSceneColorDistortionCandidate(
    float3 surfaceWorldPosition,
    float3 cameraWorldPosition,
    float2 candidateUv,
    float candidateDepth,
    float4x4 inverseViewProjection,
    float2 inverseScreenSize,
    out float3 receiverWorldPosition,
    out float opticalDistance,
    out float confidence)
{
    receiverWorldPosition = surfaceWorldPosition;
    opticalDistance = 0.0f;
    confidence = 0.0f;
    if (!HikariIsScreenUvInsideTexelGuard(
            candidateUv,
            inverseScreenSize) ||
        !HikariReconstructWorldPosition(
            candidateUv,
            candidateDepth,
            inverseViewProjection,
            receiverWorldPosition))
    {
        return false;
    }

    const float surfaceDistance =
        length(surfaceWorldPosition - cameraWorldPosition);
    const float receiverDistance =
        length(receiverWorldPosition - cameraWorldPosition);
    const float receiverBehindSurface =
        receiverDistance - surfaceDistance;
    // A small bias tolerates depth precision at the water intersection while
    // rejecting above-water foreground silhouettes pulled by distortion.
    if (receiverBehindSurface < -0.025f)
    {
        return false;
    }

    const float3 viewDirection = normalize(
        surfaceWorldPosition - cameraWorldPosition);
    opticalDistance = max(
        dot(receiverWorldPosition - surfaceWorldPosition, viewDirection),
        0.0f);
    const float depthConfidence = saturate(
        (receiverBehindSurface + 0.025f) / 0.10f);
    confidence = depthConfidence * HikariScreenEdgeConfidence(
        candidateUv,
        inverseScreenSize,
        3.0f);
    return confidence > 0.0f;
}

#endif
