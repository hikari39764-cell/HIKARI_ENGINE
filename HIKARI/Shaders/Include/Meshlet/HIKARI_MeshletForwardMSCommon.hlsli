#include "Include/Meshlet/HIKARI_MeshletMSCommon.hlsli"

#ifndef HIKARI_MESHLET_ENABLE_WATER_DEFORM
#define HIKARI_MESHLET_ENABLE_WATER_DEFORM 0
#endif

#define HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM HIKARI_MESHLET_ENABLE_WATER_DEFORM
#include "Include/Meshlet/HIKARI_GpuDrivenWaterDeform.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"

struct HikariMeshletVertexOut
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD10;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    nointerpolation uint forwardMeta : TEXCOORD3;
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    nointerpolation uint debugSurfaceId : TEXCOORD6;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD7;
};

HikariMeshletVertexOut HikariBuildEmptyMeshletVertex()
{
    HikariMeshletVertexOut output = (HikariMeshletVertexOut)0;
    output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    output.normalWS = float3(0.0f, 1.0f, 0.0f);
    output.tangentWS = float4(1.0f, 0.0f, 0.0f, 1.0f);
    return output;
}

HikariMeshletVertexOut HikariBuildMeshletVertex(
    HikariSurfaceGpuSceneInstance instance,
    HikariMeshletResolvedCluster resolved,
    HikariClusterVertex vertex,
    uint surfaceGpuSceneIndex)
{
    HikariMeshletVertexOut output = HikariBuildEmptyMeshletVertex();
    float3 localPosition = vertex.position.xyz;
    float3 localNormal = vertex.normal.xyz;
    HikariApplyGpuDrivenWaterDeform(
        instance,
        gTimeParams.x,
        localPosition,
        localNormal);

    // Depth prepass (MeshletDepthMS) と深度を bit 一致させるため、
    // clip 位置の計算は各 MS で同一式 + precise に固定する。
    float4 worldPos = mul(instance.clusterWorld, float4(localPosition, 1.0f));
    precise float4 clipPosition = mul(gViewProj, worldPos);
    output.position = clipPosition;
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(mul((float3x3)instance.clusterNormalMatrix, localNormal));
    output.tangentWS = float4(
        normalize(mul((float3x3)instance.clusterNormalMatrix, vertex.tangent.xyz)),
        vertex.tangent.w);
    output.uv = vertex.uv01.xy;
    output.uv1 = vertex.uv01.zw;
    output.materialDataIndex = instance.materialDataIndex;
    output.forwardMeta = HikariPackForwardVertexMeta(
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_RECEIVE_SHADOW) != 0u ? 1u : 0u,
        resolved.visible.drawBucket,
        resolved.visible.lodIndex,
        resolved.clusterIndex);
    output.objectDataIndex = surfaceGpuSceneIndex;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.debugSurfaceId =
        resolved.visible.clusterSurfaceIndex * 4099u + resolved.visible.sectionIndex;
    output.surfaceFeatureFlags = instance.flags;
    return output;
}

[outputtopology("triangle")]
[numthreads(HIKARI_MESHLET_MS_THREAD_COUNT, 1, 1)]
void main(
    uint groupIndex : SV_GroupIndex,
    uint3 groupId : SV_GroupID,
    in payload HikariMeshletPayload payload,
    out vertices HikariMeshletVertexOut vertices[HIKARI_MESHLET_MAX_VERTICES],
    out indices uint3 triangles[HIKARI_MESHLET_MAX_PRIMITIVES])
{
    HikariMeshletResolvedCluster resolved =
        HikariResolveMeshletCluster(payload, groupId);
    SetMeshOutputCounts(resolved.vertexCount, resolved.primitiveCount);
    if (!resolved.valid)
    {
        return;
    }

    ByteAddressBuffer geometry =
        gClusterGeometryPool[NonUniformResourceIndex(resolved.clusterGeometryPoolIndex)];
    HikariSurfaceGpuSceneInstance instance =
        gSurfaceGpuSceneBuffer[resolved.visible.gpuSceneInstanceIndex];

    for (uint vertexSlot = groupIndex;
         vertexSlot < resolved.vertexCount;
         vertexSlot += HIKARI_MESHLET_MS_THREAD_COUNT)
    {
        uint vertexIndex = 0u;
        if (HikariResolveMeshletVertexIndex(resolved, vertexSlot, vertexIndex))
        {
            HikariClusterVertex vertex =
                HikariLoadClusterVertexShading(geometry, resolved.header, vertexIndex);
            HikariApplyGpuDrivenSkinning(
                geometry,
                instance,
                vertexIndex,
                vertex.position.xyz,
                vertex.normal.xyz,
                vertex.tangent.xyz);
            vertices[vertexSlot] =
                HikariBuildMeshletVertex(
                    instance,
                    resolved,
                    vertex,
                    resolved.visible.gpuSceneInstanceIndex);
        }
        else
        {
            vertices[vertexSlot] = HikariBuildEmptyMeshletVertex();
        }
    }

    for (uint primitiveSlot = groupIndex;
         primitiveSlot < resolved.primitiveCount;
         primitiveSlot += HIKARI_MESHLET_MS_THREAD_COUNT)
    {
        uint3 primitive =
            HikariLoadResolvedMeshletPrimitive(geometry, resolved, primitiveSlot);
        if (primitive.x < resolved.vertexCount &&
            primitive.y < resolved.vertexCount &&
            primitive.z < resolved.vertexCount)
        {
            triangles[primitiveSlot] = primitive;
        }
        else
        {
            triangles[primitiveSlot] = uint3(0u, 0u, 0u);
        }
    }
}
