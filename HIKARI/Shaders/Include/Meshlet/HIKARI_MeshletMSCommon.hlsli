#ifndef HIKARI_MESHLET_MS_COMMON_INCLUDED
#define HIKARI_MESHLET_MS_COMMON_INCLUDED

#include "Include/Render3D/HIKARI_MeshCameraData.hlsli"

#include "Include/Meshlet/HIKARI_MeshletDraw.hlsli"
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_CB 1
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_HELPERS 1
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Cluster/HIKARI_ClusterGpuData.hlsli"
#include "Include/Meshlet/HIKARI_GpuDrivenSkinning.hlsli"
#include "Include/HIKARI_RenderDescriptorLayout.hlsli"

static const uint HIKARI_CLUSTER_SRV_POOL_BEGIN = HIKARI_RENDER_CLUSTER_GEOMETRY_SRV_BEGIN;
static const uint HIKARI_CLUSTER_SRV_POOL_COUNT = HIKARI_RENDER_CLUSTER_GEOMETRY_SRV_COUNT;
static const uint HIKARI_MESHLET_MAX_PRIMITIVES =
    HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_PRIMITIVES;
static const uint HIKARI_MESHLET_MAX_VERTICES =
    HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_VERTICES;
static const uint HIKARI_MESHLET_MS_THREAD_COUNT =
    HIKARI_CLUSTER_GEOMETRY_CONFIG_MESHLET_MS_THREAD_COUNT;

ByteAddressBuffer gClusterGeometryPool[HIKARI_CLUSTER_SRV_POOL_COUNT] : register(t0, space1);

struct HikariMeshletResolvedCluster
{
    bool valid;
    uint clusterGeometryPoolIndex;
    uint clusterMetadataPoolIndex;
    uint clusterIndex;
    uint firstVertex;
    uint firstPrimitive;
    uint vertexCount;
    uint primitiveCount;
    HikariMeshletVisibleRange visible;
    HikariClusterGeometryHeader header;
    HikariMeshCluster cluster;
};

HikariMeshletVisibleRange HikariBuildMeshletVisibleRangeFromPayload(
    HikariMeshletPayload payload)
{
    HikariMeshletVisibleRange visible = (HikariMeshletVisibleRange)0;
    visible.gpuSceneInstanceIndex = payload.gpuSceneInstanceIndex;
    visible.clusterGeometrySrvDescriptorIndex =
        HIKARI_CLUSTER_SRV_POOL_BEGIN + payload.clusterGeometryPoolIndex;
    visible.firstCluster = payload.firstCluster;
    visible.clusterCount = payload.rangeClusterCount;
    visible.clusterSurfaceIndex = payload.clusterSurfaceIndex;
    visible.clusterGeometryMetadataSrvDescriptorIndex = 0xffffffffu;
    visible.lodIndex = payload.lodIndex;
    visible.drawBucket = payload.drawBucket;
    visible.sectionIndex = payload.sectionIndex;
    visible.vertexOffsetBytes = payload.vertexOffsetBytes;
    visible.vertexCount = payload.vertexCount;
    visible.meshletPrimitiveOffsetBytes = payload.meshletPrimitiveOffsetBytes;
    visible.meshletPrimitiveCount = payload.meshletPrimitiveCount;
    visible.geometryClusterCount = payload.geometryClusterCount;
    return visible;
}

HikariClusterGeometryHeader HikariBuildMeshletHeaderFromPayload(
    HikariMeshletPayload payload)
{
    HikariClusterGeometryHeader header = (HikariClusterGeometryHeader)0;
    header.vertexOffsetBytes = payload.vertexOffsetBytes;
    header.vertexCount = payload.vertexCount;
    header.meshletPrimitiveOffsetBytes = payload.meshletPrimitiveOffsetBytes;
    header.meshletPrimitiveCount = payload.meshletPrimitiveCount;
    header.clusterCount = payload.geometryClusterCount;
    return header;
}

HikariMeshletResolvedCluster HikariResolveMeshletCluster(
    HikariMeshletPayload payload,
    uint3 groupId)
{
    HikariMeshletResolvedCluster result = (HikariMeshletResolvedCluster)0;

    result.visible = HikariBuildMeshletVisibleRangeFromPayload(payload);
    result.header = HikariBuildMeshletHeaderFromPayload(payload);
    result.clusterGeometryPoolIndex =
        min(payload.clusterGeometryPoolIndex, HIKARI_CLUSTER_SRV_POOL_COUNT - 1u);
    result.clusterMetadataPoolIndex = 0u;

    const bool slotValid =
        payload.visibleClusterCount != 0u &&
        groupId.x < payload.visibleClusterCount &&
        groupId.x < HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD;
    HikariMeshletPayloadCluster payloadCluster =
        (HikariMeshletPayloadCluster)0;
    if (slotValid)
    {
        payloadCluster.firstVertex = payload.clusterFirstVertices[groupId.x];
        payloadCluster.firstPrimitive = payload.clusterFirstPrimitives[groupId.x];
        payloadCluster.packedCounts = payload.clusterPackedCounts[groupId.x];
    }

    result.clusterIndex = payload.firstCluster + (payloadCluster.packedCounts & 0xffffu);
    result.firstVertex = payloadCluster.firstVertex;
    result.firstPrimitive = payloadCluster.firstPrimitive;
    result.vertexCount =
        min(
            HikariMeshletPayloadVertexCount(payloadCluster),
            HIKARI_MESHLET_MAX_VERTICES);
    result.primitiveCount =
        min(
            HikariMeshletPayloadPrimitiveCount(payloadCluster),
            HIKARI_MESHLET_MAX_PRIMITIVES);
    result.cluster.firstVertex = result.firstVertex;
    result.cluster.vertexCount = result.vertexCount;
    result.cluster.firstPrimitive = result.firstPrimitive;
    result.cluster.triangleCount = result.primitiveCount;

    const bool vertexSpanValid =
        result.firstVertex < result.header.vertexCount &&
        result.vertexCount <= result.header.vertexCount - result.firstVertex;
    const bool primitiveSpanValid =
        result.firstPrimitive < result.header.meshletPrimitiveCount &&
        result.primitiveCount <=
            result.header.meshletPrimitiveCount - result.firstPrimitive;
    result.valid =
        slotValid &&
        payload.clusterGeometryPoolIndex < HIKARI_CLUSTER_SRV_POOL_COUNT &&
        result.vertexCount != 0u &&
        result.primitiveCount != 0u &&
        vertexSpanValid &&
        primitiveSpanValid;
    if (!result.valid)
    {
        result.vertexCount = 0u;
        result.primitiveCount = 0u;
    }
    return result;
}

bool HikariResolveMeshletVertexIndex(
    HikariMeshletResolvedCluster resolved,
    uint localVertexIndex,
    out uint vertexIndex)
{
    vertexIndex = 0u;
    if (localVertexIndex >= resolved.vertexCount)
    {
        return false;
    }

    vertexIndex = resolved.firstVertex + localVertexIndex;
    return vertexIndex < resolved.header.vertexCount;
}

uint3 HikariLoadResolvedMeshletPrimitive(
    ByteAddressBuffer geometry,
    HikariMeshletResolvedCluster resolved,
    uint localPrimitiveIndex)
{
    return HikariLoadMeshletPrimitiveIndices(
        geometry,
        resolved.header,
        resolved.firstPrimitive + localPrimitiveIndex);
}

#endif
