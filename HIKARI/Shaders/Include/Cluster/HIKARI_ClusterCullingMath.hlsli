#ifndef HIKARI_CLUSTER_CULLING_MATH_INCLUDED
#define HIKARI_CLUSTER_CULLING_MATH_INCLUDED

#include "Include/Surface/HIKARI_FoliageWind.hlsli"

bool HikariClusterHasConservativeBounds(HikariSurfaceGpuSceneInstance instance)
{
    const uint unsupported = HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED |
        HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_SURFACE |
        HIKARI_SURFACE_GPU_SCENE_FLAG_SAND_SURFACE |
        HIKARI_SURFACE_GPU_SCENE_FLAG_CAMERA_CENTERED_OCEAN;
    return (instance.flags & unsupported) == 0u &&
        ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_FOLIAGE_WIND) == 0u ||
            all(isfinite(instance.fxUser[0].xyz)));
}

float4 HikariClusterMatrixRow0(float4x4 matrix)
{
    return float4(matrix._11, matrix._12, matrix._13, matrix._14);
}

float4 HikariClusterMatrixRow1(float4x4 matrix)
{
    return float4(matrix._21, matrix._22, matrix._23, matrix._24);
}

float4 HikariClusterMatrixRow2(float4x4 matrix)
{
    return float4(matrix._31, matrix._32, matrix._33, matrix._34);
}

float4 HikariClusterMatrixRow3(float4x4 matrix)
{
    return float4(matrix._41, matrix._42, matrix._43, matrix._44);
}

bool HikariClusterPlaneVisible(float4 plane, float3 center, float radius)
{
    float planeLength = length(plane.xyz);
    if (planeLength <= 0.000001f)
    {
        return true;
    }

    return dot(plane.xyz, center) + plane.w >= -radius * planeLength;
}

float4 HikariClusterBuildWorldSphere(
    float4x4 world,
    float4 localSphere,
    float4 boundsMin,
    float4 boundsMax)
{
    float3 localCenter = localSphere.xyz;
    float localRadius = localSphere.w;
    if (localRadius <= 0.000001f)
    {
        localCenter = (boundsMin.xyz + boundsMax.xyz) * 0.5f;
        localRadius = length(max(
            boundsMax.xyz - localCenter,
            float3(0.0f, 0.0f, 0.0f)));
    }

    float3 worldCenter = mul(world, float4(localCenter, 1.0f)).xyz;
    float3 axisX = float3(world._11, world._21, world._31);
    float3 axisY = float3(world._12, world._22, world._32);
    float3 axisZ = float3(world._13, world._23, world._33);
    // A^T A の絶対行和による最大特異値の上界。shear も球の中に収める。
    float xy = abs(dot(axisX, axisY)), xz = abs(dot(axisX, axisZ)), yz = abs(dot(axisY, axisZ));
    float worldScale = sqrt(max(dot(axisX, axisX) + xy + xz,
        max(dot(axisY, axisY) + xy + yz, dot(axisZ, axisZ) + xz + yz)));
    return float4(worldCenter, max(localRadius * worldScale, 0.0f));
}

bool HikariClusterSphereVisible(float4x4 viewProj, float4 worldSphere)
{
    float3 center = worldSphere.xyz;
    float radius = max(worldSphere.w, 0.0f);
    float4 row0 = HikariClusterMatrixRow0(viewProj);
    float4 row1 = HikariClusterMatrixRow1(viewProj);
    float4 row2 = HikariClusterMatrixRow2(viewProj);
    float4 row3 = HikariClusterMatrixRow3(viewProj);

    return
        HikariClusterPlaneVisible(row3 + row0, center, radius) &&
        HikariClusterPlaneVisible(row3 - row0, center, radius) &&
        HikariClusterPlaneVisible(row3 + row1, center, radius) &&
        HikariClusterPlaneVisible(row3 - row1, center, radius) &&
        HikariClusterPlaneVisible(row2, center, radius) &&
        HikariClusterPlaneVisible(row3 - row2, center, radius);
}

float4 HikariClusterBuildCullSphere(HikariSurfaceGpuSceneInstance instance,
    float4 localSphere, float4 boundsMin, float4 boundsMax)
{
    float4 sphere = HikariClusterBuildWorldSphere(instance.clusterWorld,
        localSphere, boundsMin, boundsMax);
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_FOLIAGE_WIND) != 0u)
        sphere.w += length(HikariFoliageWindBoundsExtent(instance));
    return sphere;
}

// 静的形状は tight な local box、風変形は world 包絡を同じ HZB query に渡す。
void HikariClusterBuildCullBox(HikariSurfaceGpuSceneInstance instance,
    inout float4x4 world, inout float4 boundsMin, inout float4 boundsMax)
{
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_FOLIAGE_WIND) == 0u) return;
    float3 center = mul(world, float4((boundsMin.xyz + boundsMax.xyz) * 0.5f, 1.0f)).xyz;
    float3 extent = mul(abs((float3x3)world), (boundsMax.xyz - boundsMin.xyz) * 0.5f) +
        HikariFoliageWindBoundsExtent(instance);
    boundsMin = float4(center - extent, 0.0f);
    boundsMax = float4(center + extent, 0.0f);
    world = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
}

// local box を clip plane の support radius で検査する。長い薄板を球だけで残さない。
// world に負の scale/shear があっても plane を local に戻すので保守性を保つ。
bool HikariClusterBoxVisible(float4x4 viewProj, float4x4 world, float3 boundsMin, float3 boundsMax)
{
    if (!all(isfinite(boundsMin)) || !all(isfinite(boundsMax)) || any(boundsMax < boundsMin)) return true;
    float3 center = (boundsMin + boundsMax) * 0.5f;
    float3 extent = (boundsMax - boundsMin) * 0.5f;
    float4x4 clip = mul(viewProj, world);
    float4 r0 = HikariClusterMatrixRow0(clip), r1 = HikariClusterMatrixRow1(clip);
    float4 r2 = HikariClusterMatrixRow2(clip), r3 = HikariClusterMatrixRow3(clip);
    float4 planes[6] = {r3+r0, r3-r0, r3+r1, r3-r1, r2, r3-r2};
    [unroll] for (uint i = 0u; i < 6u; ++i)
    {
        float4 p = planes[i];
        float radius = dot(abs(p.xyz), extent);
        float distance = dot(p.xyz, center) + p.w;
        float tolerance = 1e-6f * max(dot(abs(p.xyz), abs(center) + extent) + abs(p.w), 1.0f);
        if (isfinite(distance) && isfinite(radius) && distance + radius < -tolerance) return false;
    }
    return true;
}

// cone は cook 時の local 空間で判定する。非一様 scale/shear で
// world normal の cone 開口角をそのまま流用すると可視面まで落ちる。
bool HikariClusterConeBackfacing(float4x4 world, HikariMeshCluster cluster,
    float3 cameraPosition)
{
    float cutoff = cluster.coneAxisCutoff.w;
    if (cutoff < 0.0f || cutoff >= 1.0f || cluster.sphereCenterRadius.w <= 0.0f) return false;
    float3 x = float3(world._11, world._21, world._31);
    float3 y = float3(world._12, world._22, world._32);
    float3 z = float3(world._13, world._23, world._33);
    float3 yz = cross(y, z), zx = cross(z, x), xy = cross(x, y);
    float determinant = dot(x, yz);
    float scale = length(x) * length(y) * length(z);
    if (!isfinite(determinant) || abs(determinant) <= max(scale * 0.000001f, 1e-30f)) return false;
    float3 delta = cameraPosition - float3(world._14, world._24, world._34);
    float3 localCamera = float3(dot(delta, yz), dot(delta, zx), dot(delta, xy)) / determinant;
    // 正の winding では cook 済み apex を使い、球半径による過剰な保守性を避ける。
    // 反転 transform は元の apex を再利用できないため、従来の球式へ落とす。
    if (determinant > 0.0f && all(isfinite(cluster.coneApex.xyz)))
    {
        float3 fromCamera = cluster.coneApex.xyz - localCamera;
        float distanceToApex = length(fromCamera);
        return distanceToApex > 0.0001f &&
            dot(cluster.coneAxisCutoff.xyz, fromCamera) > (cutoff + 0.001f) * distanceToApex;
    }
    float3 toCamera = localCamera - cluster.sphereCenterRadius.xyz;
    float distanceToCamera = length(toCamera);
    // determinant の符号は raster の winding と一致させる。
    float facing = dot(cluster.coneAxisCutoff.xyz, toCamera) * (determinant < 0.0f ? -1.0f : 1.0f);
    return isfinite(facing) && distanceToCamera > 0.0001f &&
        facing < -(cutoff + 0.001f) * distanceToCamera - cluster.sphereCenterRadius.w;
}

#endif
