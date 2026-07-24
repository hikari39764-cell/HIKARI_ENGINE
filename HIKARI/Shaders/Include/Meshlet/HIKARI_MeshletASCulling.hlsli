#ifndef HIKARI_MESHLET_AS_CULLING_INCLUDED
#define HIKARI_MESHLET_AS_CULLING_INCLUDED

float4 HikariMeshletAsMatrixRow0(float4x4 matrix)
{
    return float4(matrix._11, matrix._12, matrix._13, matrix._14);
}

float4 HikariMeshletAsMatrixRow1(float4x4 matrix)
{
    return float4(matrix._21, matrix._22, matrix._23, matrix._24);
}

float4 HikariMeshletAsMatrixRow2(float4x4 matrix)
{
    return float4(matrix._31, matrix._32, matrix._33, matrix._34);
}

float4 HikariMeshletAsMatrixRow3(float4x4 matrix)
{
    return float4(matrix._41, matrix._42, matrix._43, matrix._44);
}

bool HikariMeshletAsPlaneVisible(float4 plane, float3 center, float radius)
{
    float planeLength = length(plane.xyz);
    if (planeLength <= 0.000001f)
    {
        return true;
    }

    return dot(plane.xyz, center) + plane.w >= -radius * planeLength;
}

float4 HikariMeshletAsBuildWorldSphere(
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
    float worldScale = max(length(axisX), max(length(axisY), length(axisZ)));
    return float4(worldCenter, max(localRadius * worldScale, 0.0f));
}

bool HikariMeshletAsSphereVisible(float4x4 viewProj, float4 worldSphere)
{
    float3 center = worldSphere.xyz;
    float radius = max(worldSphere.w, 0.0f);
    float4 row0 = HikariMeshletAsMatrixRow0(viewProj);
    float4 row1 = HikariMeshletAsMatrixRow1(viewProj);
    float4 row2 = HikariMeshletAsMatrixRow2(viewProj);
    float4 row3 = HikariMeshletAsMatrixRow3(viewProj);

    return
        HikariMeshletAsPlaneVisible(row3 + row0, center, radius) &&
        HikariMeshletAsPlaneVisible(row3 - row0, center, radius) &&
        HikariMeshletAsPlaneVisible(row3 + row1, center, radius) &&
        HikariMeshletAsPlaneVisible(row3 - row1, center, radius) &&
        HikariMeshletAsPlaneVisible(row2, center, radius) &&
        HikariMeshletAsPlaneVisible(row3 - row2, center, radius);
}

float3 HikariMeshletAsTransformNormalAxis(float4x4 world, float3 localAxis)
{
    float3 axisX = float3(world._11, world._21, world._31);
    float3 axisY = float3(world._12, world._22, world._32);
    float3 axisZ = float3(world._13, world._23, world._33);

    float3 normalAxis =
        localAxis.x * cross(axisY, axisZ) +
        localAxis.y * cross(axisZ, axisX) +
        localAxis.z * cross(axisX, axisY);
    if (length(normalAxis) <= 0.000001f)
    {
        normalAxis = mul(world, float4(localAxis, 0.0f)).xyz;
    }
    return length(normalAxis) > 0.000001f
        ? normalize(normalAxis)
        : float3(0.0f, 0.0f, 1.0f);
}

bool HikariMeshletAsConeBackfacing(
    float4x4 world,
    HikariMeshCluster cluster,
    float4 worldSphere,
    float3 cameraPosition)
{
    float cutoff = cluster.coneAxisCutoff.w;
    if (cutoff <= 0.0f || cutoff >= 1.0f)
    {
        return false;
    }

    float3 toCamera = cameraPosition - worldSphere.xyz;
    float distanceToCamera = length(toCamera);
    if (distanceToCamera <= 0.0001f)
    {
        return false;
    }

    float3 axis = HikariMeshletAsTransformNormalAxis(
        world,
        cluster.coneAxisCutoff.xyz);
    float3 view = toCamera / distanceToCamera;
    float radiusBias = max(worldSphere.w, 0.0f) / distanceToCamera;
    return dot(axis, view) <= -cutoff - radiusBias - 0.001f;
}

#endif
