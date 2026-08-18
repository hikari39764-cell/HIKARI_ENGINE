#ifndef HIKARI_OCEAN_GEOMETRY_INCLUDED
#define HIKARI_OCEAN_GEOMETRY_INCLUDED

#include "Include/Math/HIKARI_FiniteMath.hlsli"

// Camera-centered ocean geometry is an explicit water-record route.  This
// include only supplies the shared graded-grid math; it never inspects mesh
// size, object name, preset identity, or camera height to promote finite
// authored water.
static const float HIKARI_OCEAN_EPSILON = 1.0e-4f;
static const float HIKARI_OCEAN_EDGE_MARGIN = 1.05f;
static const float HIKARI_OCEAN_MIN_SNAP_CELL = 1.0e-3f;
// A malformed/degenerate inverse projection must not turn one water vertex
// into NaN/Inf and poison the whole draw.  The fallback remains deliberately
// conservative (8 authored half-extents) while the valid path is bounded to
// a finite 256x expansion and an absolute world-scale ceiling.
static const float HIKARI_OCEAN_FALLBACK_FAR_EXTENT_RATIO = 8.0f;
static const float HIKARI_OCEAN_MAX_FAR_EXTENT_RATIO = 256.0f;
static const float HIKARI_OCEAN_MAX_FAR_HALF_EXTENT = 1.0e6f;

float2 HikariOceanSafeHalfExtent(float2 extent, float2 fallbackExtent)
{
    const float2 safeFallback = max(
        HikariFiniteOr(abs(fallbackExtent), 1.0f.xx),
        HIKARI_OCEAN_EPSILON.xx);
    return max(
        HikariFiniteOr(abs(extent), safeFallback),
        HIKARI_OCEAN_EPSILON.xx);
}

float2 HikariOceanFallbackFarHalfExtent(float2 authoredHalfExtent)
{
    const float2 safeAuthored =
        HikariOceanSafeHalfExtent(authoredHalfExtent, 1.0f.xx);
    const float2 ratioLimit =
        safeAuthored * HIKARI_OCEAN_FALLBACK_FAR_EXTENT_RATIO;
    const float2 absoluteLimit = HIKARI_OCEAN_MAX_FAR_HALF_EXTENT.xx;
    return max(
        safeAuthored,
        min(ratioLimit, absoluteLimit));
}

float2 HikariOceanSanitizeFarHalfExtent(
    float2 candidate,
    float2 authoredHalfExtent)
{
    const float2 safeAuthored =
        HikariOceanSafeHalfExtent(authoredHalfExtent, 1.0f.xx);
    const float2 ratioLimit =
        safeAuthored * HIKARI_OCEAN_MAX_FAR_EXTENT_RATIO;
    const float2 absoluteLimit = HIKARI_OCEAN_MAX_FAR_HALF_EXTENT.xx;
    const float2 boundedLimit = max(
        safeAuthored,
        min(ratioLimit, absoluteLimit));
    return max(
        safeAuthored,
        min(
            HikariFiniteOr(abs(candidate), HikariOceanFallbackFarHalfExtent(safeAuthored)),
            boundedLimit));
}

float2 HikariOceanSnapCameraXZ(float2 cameraLocalXZ, float2 snapCellSize)
{
    const float2 safeCamera = HikariFiniteOr(cameraLocalXZ, 0.0f.xx);
    const float2 safeCellSize = max(
        HikariFiniteOr(abs(snapCellSize), HIKARI_OCEAN_MIN_SNAP_CELL.xx),
        HIKARI_OCEAN_MIN_SNAP_CELL.xx);
    return HikariFiniteOr(
        floor(safeCamera / safeCellSize) * safeCellSize,
        safeCamera);
}

float2 HikariOceanWorldToLocalXZ(float4x4 world, float2 worldXZ)
{
    const float3 worldOrigin = mul(world, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
    const float2 basisX = float2(world._11, world._31);
    const float2 basisZ = float2(world._13, world._33);
    const float2 delta = worldXZ - worldOrigin.xz;
    const float determinant = basisX.x * basisZ.y - basisX.y * basisZ.x;
    if (abs(determinant) <= HIKARI_OCEAN_EPSILON)
    {
        return delta;
    }
    const float inverseDeterminant = rcp(determinant);
    return float2(
        (delta.x * basisZ.y - delta.y * basisZ.x) * inverseDeterminant,
        (basisX.x * delta.y - basisX.y * delta.x) * inverseDeterminant);
}

float2 HikariOceanLocalDeltaToWorldXZ(float4x4 world, float2 localDelta)
{
    const float2 basisX = float2(world._11, world._31);
    const float2 basisZ = float2(world._13, world._33);
    return basisX * localDelta.x + basisZ * localDelta.y;
}

float2 HikariOceanWorldDeltaToLocalXZ(float4x4 world, float2 worldDelta)
{
    const float2 basisX = float2(world._11, world._31);
    const float2 basisZ = float2(world._13, world._33);
    const float determinant = basisX.x * basisZ.y - basisX.y * basisZ.x;
    if (abs(determinant) <= HIKARI_OCEAN_EPSILON)
    {
        return worldDelta;
    }
    const float inverseDeterminant = rcp(determinant);
    return float2(
        (worldDelta.x * basisZ.y - worldDelta.y * basisZ.x) * inverseDeterminant,
        (basisX.x * worldDelta.y - basisX.y * worldDelta.x) * inverseDeterminant);
}

float2 HikariOceanResolveAuthoredHalfExtent(
    float2 authoritativeHalfExtent,
    float localBoundsRadius,
    float4x4 world)
{
    const float2 candidate = HikariFiniteOr(
        abs(authoritativeHalfExtent),
        0.0f.xx);
    if (candidate.x > HIKARI_OCEAN_EPSILON &&
        candidate.y > HIKARI_OCEAN_EPSILON)
    {
        return candidate;
    }
    // Schema-invalid fallback remains instance-constant. Never infer extent
    // from packed per-vertex UVs; that creates a non-continuous lattice.
    const float2 basisX = float2(world._11, world._31);
    const float2 basisZ = float2(world._13, world._33);
    const float basisDiagonalScale = sqrt(
        max(
            dot(basisX, basisX) + dot(basisZ, basisZ),
            HIKARI_OCEAN_EPSILON * HIKARI_OCEAN_EPSILON));
    const float safeBasisDiagonalScale = max(
        HikariFiniteOr(basisDiagonalScale, 1.0f),
        HIKARI_OCEAN_EPSILON);
    const float squareHalfExtent = max(
        HikariFiniteOr(abs(localBoundsRadius), 1.0f) /
            safeBasisDiagonalScale,
        HIKARI_OCEAN_EPSILON);
    return HikariOceanSafeHalfExtent(
        squareHalfExtent.xx,
        squareHalfExtent.xx);
}

float2 HikariOceanResolveAuthoredCenteredXZ(
    float2 authoredLocalXZ,
    float2 uv1,
    float2 authoredCenterXZ,
    float2 authoredHalfExtent)
{
    const float2 safeHalfExtent = HikariOceanSafeHalfExtent(
        authoredHalfExtent,
        1.0f.xx);
    const float2 safeAuthoredLocalXZ =
        HikariFiniteOr(authoredLocalXZ, 0.0f.xx);
    const float2 safeUv1 = HikariFiniteOr(uv1, 0.5f.xx);
    const float2 uvCentered = safeUv1 * 2.0f - 1.0f;
    // GridPlane local XZ is centered at the authored origin.  Use that
    // position as the graded coordinate so packed UV1 precision cannot make
    // neighboring vertices choose different extents.  UV1 remains an input
    // for the shared caller contract and is only the malformed-position
    // fallback.
    const bool authoredPositionFinite =
        HikariIsFiniteFloat2(authoredLocalXZ);
    const float2 positionCentered =
        (safeAuthoredLocalXZ -
            HikariFiniteOr(authoredCenterXZ, 0.0f.xx)) /
        safeHalfExtent;
    return clamp(
        authoredPositionFinite
            ? HikariFiniteOr(positionCentered, uvCentered)
            : uvCentered,
        -1.0f.xx,
        1.0f.xx);
}

float HikariOceanResolveGridSegments(uint indexCount)
{
    // Explicit ocean mode is authored on the existing square GridPlane.  Its
    // index count is a topology contract, not a name/size heuristic.  LOD
    // ranges still yield a bounded conservative lattice when present.
    const float cellCount = max((float)indexCount / 6.0f, 1.0f);
    return max(round(sqrt(cellCount)), 1.0f);
}

float2 HikariOceanResolveSnapCell(
    float2 authoredHalfExtent,
    uint indexCount)
{
    const float segments = HikariOceanResolveGridSegments(indexCount);
    return max(
        authoredHalfExtent * (2.0f / segments),
        HIKARI_OCEAN_MIN_SNAP_CELL.xx);
}

float3 HikariOceanUnprojectFarPoint(
    float4x4 invViewProj,
    float2 ndc)
{
    float4 world = mul(invViewProj, float4(ndc, 1.0f, 1.0f));
    if (!HikariIsFiniteFloat4(world) ||
        abs(world.w) <= HIKARI_OCEAN_EPSILON)
    {
        return 0.0f.xxx;
    }
    // Preserve the projective sign.  Using abs(world.w) here mirrors points
    // behind the camera and can generate an invalid ray/plane intersection.
    world.xyz /= world.w;
    return HikariFiniteOr(world.xyz, 0.0f.xxx);
}

float3 HikariOceanWorldNormalToLocal(
    float4x4 world,
    float3 worldNormal)
{
    const float3 basisX = float3(world._11, world._21, world._31);
    const float3 basisY = float3(world._12, world._22, world._32);
    const float3 basisZ = float3(world._13, world._23, world._33);
    const float3 localNormal = float3(
        dot(worldNormal, basisX),
        dot(worldNormal, basisY),
        dot(worldNormal, basisZ));
    return length(localNormal) > HIKARI_OCEAN_EPSILON
        ? normalize(localNormal)
        : float3(0.0f, 1.0f, 0.0f);
}

float2 HikariOceanResolveFarHalfExtent(
    float4x4 world,
    float4x4 invViewProj,
    float4 cameraPosition,
    float2 cameraWorldXZ,
    float2 authoredHalfExtent)
{
    const float planeWorldY =
        mul(world, float4(0.0f, 0.0f, 0.0f, 1.0f)).y;
    const float2 safeAuthoredHalfExtent =
        HikariOceanSafeHalfExtent(authoredHalfExtent, 1.0f.xx);
    if (!HikariIsFiniteFloat(planeWorldY))
    {
        return HikariOceanFallbackFarHalfExtent(safeAuthoredHalfExtent);
    }
    float2 farHalfExtent = safeAuthoredHalfExtent;
    const float2 cameraLocalXZ = HikariOceanWorldToLocalXZ(
        world,
        cameraWorldXZ);
    if (!HikariIsFiniteFloat2(cameraLocalXZ) ||
        !HikariIsFiniteFloat3(cameraPosition.xyz))
    {
        return HikariOceanFallbackFarHalfExtent(safeAuthoredHalfExtent);
    }
    [unroll]
    for (uint corner = 0u; corner < 4u; ++corner)
    {
        const float2 ndc = float2(
            (corner & 1u) != 0u ? 1.0f : -1.0f,
            (corner & 2u) != 0u ? 1.0f : -1.0f);
        const float3 farWorld = HikariOceanUnprojectFarPoint(
            invViewProj,
            ndc);
        if (!HikariIsFiniteFloat3(farWorld))
        {
            continue;
        }
        const float3 ray = farWorld - cameraPosition.xyz;
        const float rayY = ray.y;
        float3 hit = farWorld;
        if (HikariIsFiniteFloat3(ray) &&
            HikariIsFiniteFloat(rayY) &&
            abs(rayY) > HIKARI_OCEAN_EPSILON)
        {
            const float planeDistance =
                (planeWorldY - cameraPosition.y) / rayY;
            if (HikariIsFiniteFloat(planeDistance) &&
                planeDistance > HIKARI_OCEAN_EPSILON)
            {
                hit = cameraPosition.xyz + ray * planeDistance;
            }
        }
        if (!HikariIsFiniteFloat3(hit))
        {
            continue;
        }
        const float2 localHit = HikariOceanWorldToLocalXZ(world, hit.xz);
        if (!HikariIsFiniteFloat2(localHit))
        {
            continue;
        }
        farHalfExtent = max(
            farHalfExtent,
            abs(localHit - cameraLocalXZ));
    }
    return HikariOceanSanitizeFarHalfExtent(
        farHalfExtent * HIKARI_OCEAN_EDGE_MARGIN,
        safeAuthoredHalfExtent);
}

float HikariOceanResolveTanHalfFovY(
    float4x4 invViewProj,
    float4 cameraPosition)
{
    const float3 safeCamera = HikariFiniteOr(cameraPosition.xyz, 0.0f.xxx);
    const float3 centerDelta =
        HikariOceanUnprojectFarPoint(invViewProj, float2(0.0f, 0.0f)) -
        safeCamera;
    const float3 topDelta =
        HikariOceanUnprojectFarPoint(invViewProj, float2(0.0f, 1.0f)) -
        safeCamera;
    const float centerLengthSquared = dot(centerDelta, centerDelta);
    const float topLengthSquared = dot(topDelta, topDelta);
    if (!HikariIsFiniteFloat3(centerDelta) ||
        !HikariIsFiniteFloat3(topDelta) ||
        !HikariIsFiniteFloat(centerLengthSquared) ||
        !HikariIsFiniteFloat(topLengthSquared) ||
        centerLengthSquared <= HIKARI_OCEAN_EPSILON ||
        topLengthSquared <= HIKARI_OCEAN_EPSILON)
    {
        return 1.0f;
    }
    const float3 centerRay = centerDelta * rsqrt(centerLengthSquared);
    const float3 topRay = topDelta * rsqrt(topLengthSquared);
    const float sine = length(cross(centerRay, topRay));
    const float cosine = max(dot(centerRay, topRay), HIKARI_OCEAN_EPSILON);
    const float tanHalfFovY = sine / cosine;
    return HikariIsFiniteFloat(tanHalfFovY)
        ? clamp(tanHalfFovY, HIKARI_OCEAN_EPSILON, 100.0f)
        : 1.0f;
}

float HikariOceanProjectedCellPixels(
    float worldCellSize,
    float viewDistance,
    float tanHalfFovY,
    float screenHeight)
{
    if (!HikariIsFiniteFloat(worldCellSize) ||
        !HikariIsFiniteFloat(viewDistance) ||
        !HikariIsFiniteFloat(tanHalfFovY) ||
        !HikariIsFiniteFloat(screenHeight))
    {
        return 0.0f;
    }
    const float safeDistance = max(abs(viewDistance), HIKARI_OCEAN_EPSILON);
    const float safeTanHalfFovY = max(abs(tanHalfFovY), HIKARI_OCEAN_EPSILON);
    const float safeScreenHeight = max(abs(screenHeight), 1.0f);
    const float projectedPixels = safeScreenHeight * abs(worldCellSize) /
        (2.0f * safeTanHalfFovY * safeDistance);
    return HikariIsFiniteFloat(projectedPixels)
        ? max(projectedPixels, 0.0f)
        : 0.0f;
}

float HikariOceanHighFrequencyFade(
    float projectedCellPixels,
    float radialNormalized)
{
    // 0.75..1.5 px is the continuous footprint transition.  The normalized
    // radial term keeps the extended edge from carrying high-frequency
    // geometric choppiness after the authored grid has been stretched.
    const float footprint = saturate(
        (max(projectedCellPixels, 0.0f) - 0.75f) / 0.75f);
    const float radial = 1.0f - smoothstep(0.55f, 1.0f, saturate(radialNormalized));
    return saturate(min(footprint, radial));
}

float2 HikariOceanWorldAnchoredXZ(
    float4x4 world,
    float3 localPosition)
{
    // Wave phase is evaluated from the final world anchor, never from a
    // normalized patch UV.  Adjacent camera-snapped positions share phase.
    const float2 localXZ = HikariFiniteOr(localPosition.xz, 0.0f.xx);
    return HikariFiniteOr(
        mul(world, float4(localPosition, 1.0f)).xz,
        localXZ);
}

#endif
