#ifndef HIKARI_OCEAN_GEOMETRY_INCLUDED
#define HIKARI_OCEAN_GEOMETRY_INCLUDED

#include "Include/Math/HIKARI_FiniteMath.hlsli"

// Camera-centered ocean geometry is an explicit water-record route.  This
// include only supplies the shared graded-grid math; it never inspects mesh
// size, object name, preset identity, or camera height to promote finite
// authored water.
static const float HIKARI_OCEAN_EPSILON = 1.0e-4f;
static const float HIKARI_OCEAN_MIN_SNAP_CELL = 1.0e-3f;
static const float HIKARI_OCEAN_FOCUS_NDC_Y = -0.35f;

float2 HikariOceanSafeHalfExtent(float2 extent, float2 fallbackExtent)
{
    const float2 safeFallback = max(
        HikariFiniteOr(abs(fallbackExtent), 1.0f.xx),
        HIKARI_OCEAN_EPSILON.xx);
    return max(
        HikariFiniteOr(abs(extent), safeFallback),
        HIKARI_OCEAN_EPSILON.xx);
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

// HikariOceanWorldDeltaToLocalXZ が world 変位を local へ写す時の倍率上界。
// 2x2 逆行列のスペクトルノルムを Frobenius ノルムで抑える。
float HikariOceanWorldDeltaToLocalScaleBound(float4x4 world)
{
    const float2 basisX = float2(world._11, world._31);
    const float2 basisZ = float2(world._13, world._33);
    const float determinant = basisX.x * basisZ.y - basisX.y * basisZ.x;
    if (abs(determinant) <= HIKARI_OCEAN_EPSILON)
    {
        return 1.0f;
    }
    return sqrt(dot(basisX, basisX) + dot(basisZ, basisZ)) / abs(determinant);
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

float2 HikariOceanResolveGridCenterWorldXZ(
    float4x4 world,
    float4x4 invViewProj,
    float4 cameraPosition)
{
    const float2 fallbackCenter = HikariFiniteOr(
        cameraPosition.xz,
        0.0f.xx);
    const float planeWorldY =
        mul(world, float4(0.0f, 0.0f, 0.0f, 1.0f)).y;
    const float3 farWorld = HikariOceanUnprojectFarPoint(
        invViewProj,
        float2(0.0f, HIKARI_OCEAN_FOCUS_NDC_Y));
    const float3 ray = farWorld - cameraPosition.xyz;
    const float rayLengthSquared = dot(ray, ray);
    if (!HikariIsFiniteFloat(planeWorldY) ||
        !HikariIsFiniteFloat3(ray) ||
        !HikariIsFiniteFloat(rayLengthSquared) ||
        rayLengthSquared <= HIKARI_OCEAN_EPSILON ||
        abs(ray.y) <= HIKARI_OCEAN_EPSILON)
    {
        return fallbackCenter;
    }
    const float planeDistance =
        (planeWorldY - cameraPosition.y) / ray.y;
    if (!HikariIsFiniteFloat(planeDistance) ||
        planeDistance <= HIKARI_OCEAN_EPSILON)
    {
        return fallbackCenter;
    }
    const float3 hit = cameraPosition.xyz + ray * planeDistance;
    return HikariIsFiniteFloat3(hit)
        ? hit.xz
        : fallbackCenter;
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
