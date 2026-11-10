#include "Include/Surface/HIKARI_SurfacePartition.hlsli"
#if HIKARI_SURFACE_LIGHTING == HIKARI_SURFACE_LIGHTING_MASS
#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#endif
#include "Include/Meshlet/HIKARI_MeshletMSCommon.hlsli"
#include "Include/Surface/HIKARI_SurfaceTangentFrame.hlsli"
#include "Include/Surface/HIKARI_MaterialShell.hlsli"

#ifndef HIKARI_MESHLET_ENABLE_WATER_DEFORM
#define HIKARI_MESHLET_ENABLE_WATER_DEFORM 0
#endif

#define HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM HIKARI_MESHLET_ENABLE_WATER_DEFORM
#include "Include/Meshlet/HIKARI_WaterSurfaceDeform.hlsli"
#include "Include/Surface/HIKARI_TerrainSurfaceDeform.hlsli"
#include "Include/Surface/HIKARI_FoliageWind.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"
#if defined(HIKARI_GEOMETRY_OUTLINE_VERTEX)
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Features/HIKARI_GeometryOutlineVertex.hlsli"
#endif

#if HIKARI_SURFACE_LIGHTING == HIKARI_SURFACE_LIGHTING_MASS
// 大量配置の面の探針は meshlet の中心で一度だけ読む（上向きと下向き）。探針の間隔は meshlet より十分大きいので
// meshlet の境目は見えず、読み出しは画素ごとの数十分の一になる。
#include "Include/HIKARI_MeshMaterialData.hlsli"
SamplerState gLinearWrap : register(s0);
SamplerState gLinearClamp : register(s2);
SamplerState gSkyPanoramaSampler : register(s4);
#define HIKARI_SKY_ENVIRONMENT_PROBES_ONLY 1
#include "Include/HIKARI_SkyEnvironmentCommon.hlsli"
groupshared float4 gMassProbeUp;
groupshared float4 gMassProbeDown;
groupshared float2 gMassProbeSky;

void HikariReadMassProbes(float3 center)
{
    const float3 view = normalize(gCameraPos.xyz - center);
    const float3 up = float3(0.0f, 1.0f, 0.0f);
    float skyUp, skyDown;
    gMassProbeUp = HikariResolveLightProbeDiffuse(center, up, up, view, skyUp);
    gMassProbeDown = HikariResolveLightProbeDiffuse(center, -up, -up, view, skyDown);
    gMassProbeSky = float2(skyUp, skyDown);
}
#endif

typedef HikariForwardVertexOutput HikariMeshletVertexOut;

HikariMeshletVertexOut HikariBuildEmptyMeshletVertex()
{
    HikariMeshletVertexOut output = (HikariMeshletVertexOut)0;
    output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    output.normalWS = float3(0.0f, 1.0f, 0.0f);
    output.tangentWS = float4(1.0f, 0.0f, 0.0f, 1.0f);
    output.surfaceFeatureData = 0.0f.xxx;
    output.shellLayer = float4(0, 1, 0, 1);
    return output;
}

HikariMeshletVertexOut HikariBuildMeshletVertex(
    HikariSurfaceGpuSceneInstance instance,
    HikariMeshletResolvedCluster resolved,
    HikariClusterVertex vertex,
    float3 localCharacterRight,
    float3 localCharacterForward,
    uint surfaceGpuSceneIndex,
    float4 shellLayer)
{
    HikariMeshletVertexOut output = HikariBuildEmptyMeshletVertex();
    float3 localPosition = vertex.position.xyz;
    float3 localNormal = vertex.normal.xyz;
    HikariApplyWaterSurfaceDeformWithWorld(
        instance,
        gSimulationTimeParams.x,
        vertex.uv01.zw,
        instance.clusterWorld,
        localPosition,
        localNormal,
        output.surfaceFeatureData);
    // 地形も meshlet 本線で頂点位移する (地形は mesh shader 一本化)。
    // depth pass (MeshletDepthMS) と同じ関数を共有し、深度を一致させる。
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_TERRAIN_SURFACE) != 0u)
    {
        HikariApplyTerrainSurfaceDeform(
            instance,
            localPosition,
            localNormal,
            output.surfaceFeatureData);
    }

    // Depth prepass (MeshletDepthMS) と深度を bit 一致させるため、
    // clip 位置の計算は各 MS で同一式 + precise に固定する。
    float4 worldPos = mul(instance.clusterWorld, float4(localPosition, 1.0f));
    worldPos = HikariApplyFoliageWindWorldPosition(
        instance,
        gTimeParams.x,
        vertex.uv01.xy,
        worldPos);
    output.normalWS = normalize(mul((float3x3)instance.clusterNormalMatrix, localNormal));
    worldPos.xyz += output.normalWS *
        (instance.shellSettings.y * shellLayer.x * HikariShellPush(localPosition, instance.boundsCenterRadius.w));
    precise float4 clipPosition = mul(gViewProj, worldPos);
    output.worldPosWS = worldPos.xyz;
    output.shellLayer = shellLayer;
#if defined(HIKARI_GEOMETRY_OUTLINE_VERTEX)
    output.position = HikariExpandGeometryOutlineVertex(
        worldPos,
        output.normalWS,
        instance.materialDataIndex,
        instance.materialProgramFlags);
#else
    output.position = clipPosition;
#endif
    output.tangentWS = HikariTransformSurfaceTangent(
        vertex.tangent, (float3x3)instance.clusterWorld);
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
    output.characterRightWS = normalize(
        mul((float3x3)instance.clusterWorld, localCharacterRight));
    output.characterForwardWS = normalize(
        mul((float3x3)instance.clusterWorld, localCharacterForward));
    // 地形/水面は deform が surfaceFeatureData を書いたので上書きしない。
    if ((instance.flags & (HIKARI_SURFACE_GPU_SCENE_FLAG_TERRAIN_SURFACE |
                           HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_SURFACE)) == 0u)
    {
        // z は実例集の個体の色の揺らぎ（-1..1。他の面は 0）。
        output.surfaceFeatureData = float3(localPosition.xz,
            HikariIsMassGpuSceneIndex(surfaceGpuSceneIndex) ? instance.fxUser[1].x : 0.0f);
    }
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
        HikariGetSurfaceGpuSceneInstanceAt(
            resolved.visible.gpuSceneInstanceIndex);
#if HIKARI_SURFACE_LIGHTING == HIKARI_SURFACE_LIGHTING_MASS
    if (groupIndex == 0u)
    {
        // 代表点は最初、中ほど、最後の頂点の平均（meshlet の頂点は空間的にまとまっている）。
        const uint samples[3] = {0u, resolved.vertexCount / 2u, resolved.vertexCount - 1u};
        float3 center = 0.0f.xxx;
        float count = 0.0f;
        for (uint sampleIndex = 0u; sampleIndex < 3u; ++sampleIndex)
        {
            uint vertexIndex = 0u;
            if (HikariResolveMeshletVertexIndex(resolved, samples[sampleIndex], vertexIndex))
            {
                center += HikariLoadClusterVertexShading(geometry, resolved.header, vertexIndex).position.xyz;
                count += 1.0f;
            }
        }
        HikariReadMassProbes(mul(instance.clusterWorld, float4(center / max(count, 1.0f), 1.0f)).xyz);
    }
    GroupMemoryBarrierWithGroupSync();
#endif

    for (uint vertexSlot = groupIndex;
         vertexSlot < resolved.vertexCount;
         vertexSlot += HIKARI_MESHLET_MS_THREAD_COUNT)
    {
        uint vertexIndex = 0u;
        if (HikariResolveMeshletVertexIndex(resolved, vertexSlot, vertexIndex))
        {
            HikariClusterVertex vertex =
                HikariLoadClusterVertexShading(geometry, resolved.header, vertexIndex);
            float3 localCharacterRight = float3(1.0f, 0.0f, 0.0f);
            float3 localCharacterForward = float3(0.0f, 0.0f, 1.0f);
            HikariApplyGpuDrivenSkinning(
                geometry,
                instance,
                vertexIndex,
                vertex.position.xyz,
                vertex.normal.xyz,
                vertex.tangent.xyz,
                localCharacterRight,
                localCharacterForward);
            vertices[vertexSlot] =
                HikariBuildMeshletVertex(
                    instance,
                    resolved,
                    vertex,
                    localCharacterRight,
                    localCharacterForward,
                    resolved.visible.gpuSceneInstanceIndex,
                    HikariShellLayer(groupId.y, payload.shellLayerCount, payload.shellLodAmount));
#if HIKARI_SURFACE_LIGHTING == HIKARI_SURFACE_LIGHTING_MASS
            vertices[vertexSlot].massProbeUp = gMassProbeUp;
            vertices[vertexSlot].massProbeDown = gMassProbeDown;
            vertices[vertexSlot].massProbeSky = gMassProbeSky;
#endif
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
