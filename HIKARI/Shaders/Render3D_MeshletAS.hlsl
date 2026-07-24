#include "Include/HIKARI_MeshletDraw.hlsli"
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_CB 1
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_HELPERS 1
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/HIKARI_ClusterGpuData.hlsli"
#include "Include/HIKARI_RenderDescriptorLayout.hlsli"
#include "Include/Meshlet/HIKARI_MeshletASCulling.hlsli"

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gScreenParams;
};

cbuffer CullingCameraCB : register(b9)
{
    float4x4 gCullViewProj;
    float4x4 gCullInvViewProj;
    float4 gCullCameraPos;
    float4 gCullTimeParams;
    float4 gCullScreenParams;
};

static const uint HIKARI_CLUSTER_SRV_POOL_BEGIN = HIKARI_RENDER_SYSTEM_SRV_DYNAMIC_BEGIN;
static const uint HIKARI_CLUSTER_SRV_POOL_COUNT = HIKARI_RENDER_SYSTEM_SRV_DYNAMIC_COUNT;
static const uint HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED = 1u;

ByteAddressBuffer gClusterGeometryPool[HIKARI_CLUSTER_SRV_POOL_COUNT] : register(t0, space1);

groupshared HikariMeshletPayload gMeshletAsPayload;
#include "Include/Meshlet/HIKARI_MeshletASCompaction.hlsli"

void HikariMeshletAsInitPayload(uint visibleRangeIndex)
{
    gMeshletAsPayload.visibleRangeIndex = visibleRangeIndex;
    gMeshletAsPayload.visibleClusterCount = 0u;
    gMeshletAsPayload.clusterMode = HIKARI_MESHLET_AS_MODE_COMPACT;
    gMeshletAsPayload.clusterGeometryPoolIndex = 0u;
    gMeshletAsPayload.gpuSceneInstanceIndex = 0u;
    gMeshletAsPayload.clusterSurfaceIndex = 0u;
    gMeshletAsPayload.sectionIndex = 0u;
    gMeshletAsPayload.lodIndex = 0u;
    gMeshletAsPayload.drawBucket = 0u;
    gMeshletAsPayload.vertexOffsetBytes = 0u;
    gMeshletAsPayload.vertexCount = 0u;
    gMeshletAsPayload.meshletPrimitiveOffsetBytes = 0u;
    gMeshletAsPayload.meshletPrimitiveCount = 0u;
    gMeshletAsPayload.firstCluster = 0u;
    gMeshletAsPayload.rangeClusterCount = 0u;
    gMeshletAsPayload.geometryClusterCount = 0u;
    HikariMeshletAsResetCompaction();
}

void HikariMeshletAsStoreRangePayload(
    HikariMeshletVisibleRange visible,
    uint clusterGeometryPoolIndex)
{
    gMeshletAsPayload.clusterGeometryPoolIndex = clusterGeometryPoolIndex;
    gMeshletAsPayload.gpuSceneInstanceIndex = visible.gpuSceneInstanceIndex;
    gMeshletAsPayload.clusterSurfaceIndex = visible.clusterSurfaceIndex;
    gMeshletAsPayload.sectionIndex = visible.sectionIndex;
    gMeshletAsPayload.lodIndex = visible.lodIndex;
    gMeshletAsPayload.drawBucket = visible.drawBucket;
    gMeshletAsPayload.vertexOffsetBytes = visible.vertexOffsetBytes;
    gMeshletAsPayload.vertexCount = visible.vertexCount;
    gMeshletAsPayload.meshletPrimitiveOffsetBytes = visible.meshletPrimitiveOffsetBytes;
    gMeshletAsPayload.meshletPrimitiveCount = visible.meshletPrimitiveCount;
    gMeshletAsPayload.firstCluster = visible.firstCluster;
    gMeshletAsPayload.rangeClusterCount = visible.clusterCount;
    gMeshletAsPayload.geometryClusterCount = visible.geometryClusterCount;
}

bool HikariMeshletAsBuildPayloadCluster(
    HikariClusterGeometryHeader header,
    uint clusterIndex,
    HikariMeshCluster cluster,
    out HikariMeshletPayloadCluster payloadCluster)
{
    payloadCluster = (HikariMeshletPayloadCluster)0;
    uint vertexCount = min(
        cluster.vertexCount,
        HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_VERTICES);
    uint primitiveCount = min(
        cluster.triangleCount,
        HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_PRIMITIVES);
    bool valid =
        vertexCount != 0u &&
        primitiveCount != 0u &&
        cluster.firstVertex < header.vertexCount &&
        cluster.firstPrimitive < header.meshletPrimitiveCount;
    if (!valid)
    {
        return false;
    }

    vertexCount = min(vertexCount, header.vertexCount - cluster.firstVertex);
    primitiveCount = min(
        primitiveCount,
        header.meshletPrimitiveCount - cluster.firstPrimitive);
    if (vertexCount == 0u || primitiveCount == 0u)
    {
        return false;
    }

    payloadCluster.clusterIndex = clusterIndex;
    payloadCluster.firstVertex = cluster.firstVertex;
    payloadCluster.firstPrimitive = cluster.firstPrimitive;
    payloadCluster.packedCounts =
        HikariMeshletPackPayloadCounts(vertexCount, primitiveCount);
    return true;
}

uint HikariMeshletAsResolveSourceClusterIndex(
    HikariMeshletVisibleRange visible,
    bool clusterListRange,
    bool packetRange,
    uint groupIndex)
{
    if (clusterListRange)
    {
        uint listStart = HikariMeshletVisibleRangeClusterListStart(visible);
        return listStart != 0xffffffffu
            ? gMeshletVisibleClusterList[listStart + groupIndex]
            : 0xffffffffu;
    }
    if (packetRange)
    {
        return HikariMeshletVisibleRangePacketIndex(visible, groupIndex);
    }
    return visible.firstCluster + groupIndex;
}

bool HikariMeshletAsCanConeCull(HikariMeshletVisibleRange visible)
{
    return
        visible.drawBucket != HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED &&
        (visible.flags & (
            HIKARI_SURFACE_GPU_SCENE_FLAG_ALPHA_MASKED |
            HIKARI_SURFACE_GPU_SCENE_FLAG_TRANSPARENT |
            HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED |
            HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED |
            HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX)) == 0u;
}

[numthreads(HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD, 1, 1)]
void main(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    const uint visibleRangeIndex = gMeshletVisibleRangeIndex + groupId.x;
    if (groupIndex == 0u)
    {
        HikariMeshletAsInitPayload(visibleRangeIndex);
    }
    GroupMemoryBarrierWithGroupSync();

    HikariMeshletVisibleRange visible = gMeshletVisibleRanges[visibleRangeIndex];
    const bool clusterListRange = HikariMeshletVisibleRangeUsesClusterList(visible);
    const bool packetRange = HikariMeshletVisibleRangeUsesPacket(visible);
    const bool preculledRange = HikariMeshletVisibleRangeIsPreculled(visible);
    const bool amplificationFineCull =
        HikariMeshletVisibleRangeRequiresAmplificationFineCull(visible);
    const uint sourceClusterCount = clusterListRange
        ? HikariMeshletVisibleRangeClusterListCount(visible)
        : (packetRange
            ? HikariMeshletVisibleRangePacketCount(visible)
            : visible.clusterCount);

    bool rangeValid =
        sourceClusterCount != 0u &&
        visible.clusterGeometryMetadataSrvDescriptorIndex >= HIKARI_CLUSTER_SRV_POOL_BEGIN &&
        visible.clusterGeometrySrvDescriptorIndex >= HIKARI_CLUSTER_SRV_POOL_BEGIN;
    const uint clusterMetadataPoolIndex = rangeValid
        ? visible.clusterGeometryMetadataSrvDescriptorIndex - HIKARI_CLUSTER_SRV_POOL_BEGIN
        : 0u;
    const uint clusterGeometryPoolIndex = rangeValid
        ? visible.clusterGeometrySrvDescriptorIndex - HIKARI_CLUSTER_SRV_POOL_BEGIN
        : 0u;
    rangeValid =
        rangeValid &&
        clusterMetadataPoolIndex < HIKARI_CLUSTER_SRV_POOL_COUNT &&
        clusterGeometryPoolIndex < HIKARI_CLUSTER_SRV_POOL_COUNT;

    if (rangeValid && groupIndex == 0u)
    {
        HikariMeshletAsStoreRangePayload(visible, clusterGeometryPoolIndex);
    }

    const bool denseRange =
        rangeValid &&
        preculledRange &&
        !amplificationFineCull &&
        !clusterListRange &&
        !packetRange &&
        visible.clusterCount > HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD;

    HikariClusterGeometryHeader header = (HikariClusterGeometryHeader)0;
    header.clusterOffsetBytes = visible.clusterOffsetBytes;
    header.vertexOffsetBytes = visible.vertexOffsetBytes;
    header.vertexCount = visible.vertexCount;
    header.meshletPrimitiveOffsetBytes = visible.meshletPrimitiveOffsetBytes;
    header.meshletPrimitiveCount = visible.meshletPrimitiveCount;
    header.clusterCount = visible.geometryClusterCount;

    uint clusterIndex = 0xffffffffu;
    bool clusterValid =
        rangeValid &&
        !denseRange &&
        groupIndex < min(
            sourceClusterCount,
            HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD);
    if (clusterValid)
    {
        clusterIndex = HikariMeshletAsResolveSourceClusterIndex(
            visible,
            clusterListRange,
            packetRange,
            groupIndex);
        clusterValid =
            clusterIndex != 0xffffffffu &&
            clusterIndex < header.clusterCount;
    }

    HikariSurfaceGpuSceneInstance instance = (HikariSurfaceGpuSceneInstance)0;
    HikariMeshCluster cluster = (HikariMeshCluster)0;
    if (clusterValid)
    {
        ByteAddressBuffer metadata =
            gClusterGeometryPool[NonUniformResourceIndex(clusterMetadataPoolIndex)];
        instance = gSurfaceGpuSceneBuffer[visible.gpuSceneInstanceIndex];
        cluster = HikariLoadMeshCluster(metadata, header, clusterIndex);
        clusterValid =
            cluster.indexCount != 0u &&
            cluster.surfaceIndex == visible.clusterSurfaceIndex;
    }

    const bool vertexDeformedRange =
        (visible.flags & (
            HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED |
            HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX)) != 0u;
    const bool requiresFineCull =
        !vertexDeformedRange && (amplificationFineCull || !preculledRange);
    if (clusterValid && requiresFineCull)
    {
        const float4 worldSphere = HikariMeshletAsBuildWorldSphere(
            instance.clusterWorld,
            cluster.sphereCenterRadius,
            cluster.boundsMin,
            cluster.boundsMax);
        clusterValid = HikariMeshletAsSphereVisible(
            gCullViewProj,
            worldSphere);
        if (clusterValid &&
            HikariMeshletAsCanConeCull(visible) &&
            HikariMeshletAsConeBackfacing(
                instance.clusterWorld,
                cluster,
                worldSphere,
                gCullCameraPos.xyz))
        {
            clusterValid = false;
        }
    }

    HikariMeshletPayloadCluster payloadCluster = (HikariMeshletPayloadCluster)0;
    const bool payloadValid =
        clusterValid &&
        HikariMeshletAsBuildPayloadCluster(
            header,
            clusterIndex,
            cluster,
            payloadCluster);
    const uint compactIndex = HikariMeshletAsCompactVisibleLane(
        payloadValid,
        groupIndex);
    if (payloadValid && compactIndex < HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD)
    {
        gMeshletAsPayload.clusterIndices[compactIndex] =
            payloadCluster.clusterIndex;
        gMeshletAsPayload.clusterFirstVertices[compactIndex] =
            payloadCluster.firstVertex;
        gMeshletAsPayload.clusterFirstPrimitives[compactIndex] =
            payloadCluster.firstPrimitive;
        gMeshletAsPayload.clusterPackedCounts[compactIndex] =
            payloadCluster.packedCounts;
    }
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0u)
    {
        if (denseRange)
        {
            gMeshletAsPayload.visibleClusterCount = visible.clusterCount;
            gMeshletAsPayload.clusterMode = HIKARI_MESHLET_AS_MODE_DENSE;
        }
        else
        {
            gMeshletAsPayload.visibleClusterCount = gMeshletAsVisibleCount;
            gMeshletAsPayload.clusterMode = HIKARI_MESHLET_AS_MODE_COMPACT;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    DispatchMesh(
        gMeshletAsPayload.visibleClusterCount,
        1u,
        1u,
        gMeshletAsPayload);
}
