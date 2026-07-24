#include "Include/HIKARI_MeshletMSCommon.hlsli"
#include "Include/HIKARI_GpuDrivenWaterDeform.hlsli"

struct HikariMeshletDepthVertexOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    float3 worldPosWS : TEXCOORD3;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD4;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD5;
};

HikariMeshletDepthVertexOut HikariBuildEmptyMeshletDepthVertex()
{
    HikariMeshletDepthVertexOut output = (HikariMeshletDepthVertexOut)0;
    output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    output.materialDataIndex = 0xffffffffu;
    return output;
}

HikariMeshletDepthVertexOut HikariBuildMeshletDepthVertex(
    HikariSurfaceGpuSceneInstance instance,
    HikariMeshletResolvedCluster resolved,
    ByteAddressBuffer geometry,
    uint vertexIndex)
{
    HikariMeshletDepthVertexOut output = HikariBuildEmptyMeshletDepthVertex();
    float3 localPosition =
        HikariLoadClusterVertexPosition(geometry, resolved.header, vertexIndex).xyz;
    float3 localNormal = float3(0.0f, 1.0f, 0.0f);
    float3 localTangent = float3(1.0f, 0.0f, 0.0f);
    HikariApplyGpuDrivenSkinning(
        geometry,
        instance,
        vertexIndex,
        localPosition,
        localNormal,
        localTangent);
    HikariApplyGpuDrivenWaterDeform(
        instance,
        gTimeParams.x,
        localPosition,
        localNormal);

    // Depth prepass と forward (LESS_EQUAL) の深度を bit 一致させるため、
    // clip 位置の計算は各 MS で同一式 + precise に固定する。
    float4 worldPos = mul(instance.clusterWorld, float4(localPosition, 1.0f));
    precise float4 clipPosition = mul(gViewProj, worldPos);
    output.position = clipPosition;

    const bool alphaMasked =
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_ALPHA_MASKED) != 0u;
    const bool materialFx =
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_MATERIAL_FX) != 0u &&
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX) == 0u;
    if (alphaMasked || materialFx)
    {
        float4 uv01 = HikariLoadClusterVertexUv01(geometry, resolved.header, vertexIndex);
        output.uv = uv01.xy;
        output.uv1 = uv01.zw;
        output.materialDataIndex = instance.materialDataIndex;
        output.worldPosWS = worldPos.xyz;
        output.surfaceGpuSceneIndex = resolved.visible.gpuSceneInstanceIndex;
        output.surfaceFeatureFlags = instance.flags;
    }
    return output;
}

[outputtopology("triangle")]
[numthreads(HIKARI_MESHLET_MS_THREAD_COUNT, 1, 1)]
void main(
    uint groupIndex : SV_GroupIndex,
    uint3 groupId : SV_GroupID,
    in payload HikariMeshletPayload payload,
    out vertices HikariMeshletDepthVertexOut vertices[HIKARI_MESHLET_MAX_VERTICES],
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
            vertices[vertexSlot] =
                HikariBuildMeshletDepthVertex(instance, resolved, geometry, vertexIndex);
        }
        else
        {
            vertices[vertexSlot] = HikariBuildEmptyMeshletDepthVertex();
        }
    }

    for (uint primitiveSlot = groupIndex;
         primitiveSlot < resolved.primitiveCount;
         primitiveSlot += HIKARI_MESHLET_MS_THREAD_COUNT)
    {
        uint3 primitive =
            HikariLoadResolvedMeshletPrimitive(geometry, resolved, primitiveSlot);
        triangles[primitiveSlot] =
            primitive.x < resolved.vertexCount &&
            primitive.y < resolved.vertexCount &&
            primitive.z < resolved.vertexCount
                ? primitive
                : uint3(0u, 0u, 0u);
    }
}
