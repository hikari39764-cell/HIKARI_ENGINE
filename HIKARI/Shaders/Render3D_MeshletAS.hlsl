#include "Include/Meshlet/HIKARI_MeshletDraw.hlsli"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_CB 1
#define HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_HELPERS 1
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/HIKARI_ClusterGpuData.hlsli"
#include "Include/HIKARI_RenderDescriptorLayout.hlsli"
#include "Include/Cluster/HIKARI_ClusterCullingMath.hlsli"

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gSimulationTimeParams;
    float4 gScreenParams;
};

cbuffer CullingCameraCB : register(b9)
{
    float4x4 gCullViewProj;
    float4x4 gCullInvViewProj;
    float4 gCullCameraPos;
    float4 gCullTimeParams;
    float4 gCullSimulationTimeParams;
    float4 gCullScreenParams;
};

static const uint HIKARI_CLUSTER_SRV_POOL_BEGIN = HIKARI_RENDER_CLUSTER_GEOMETRY_SRV_BEGIN;
static const uint HIKARI_CLUSTER_SRV_POOL_COUNT = HIKARI_RENDER_CLUSTER_GEOMETRY_SRV_COUNT;
static const uint HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED = 1u;

ByteAddressBuffer gClusterGeometryPool[HIKARI_CLUSTER_SRV_POOL_COUNT] : register(t0, space1);

#if !defined(HIKARI_MESHLET_DISABLE_OCCLUSION)
#include "Include/Depth/HIKARI_DepthOcclusion.hlsli"
Texture2D<float> gMeshletOcclusionDepth : register(t19);
cbuffer MeshletOcclusionQuery : register(b12)
{
    float4x4 gMeshletOcclusionViewProj;
    uint gMeshletVisibilityControl;
    uint3 gMeshletOcclusionPadding;
};
RWByteAddressBuffer gMeshletVisibilityCounters : register(u0, space8);

void HikariMeshletAsRecordCount(uint offset, bool predicate)
{
    uint count = WaveActiveCountBits(predicate);
    if (WaveIsFirstLane() && count != 0u) gMeshletVisibilityCounters.InterlockedAdd(offset, count);
}

bool HikariMeshletAsOccluded(float4x4 cullWorld, float4 cullMin, float4 cullMax,
    out bool tested)
{
    tested = false;
    uint mipCount = gMeshletVisibilityControl & 255u;
    if (mipCount == 0u) return false;
    uint width, height;
    gMeshletOcclusionDepth.GetDimensions(width, height);
    HikariDepthOcclusionQuery query;
    if (!HikariBuildDepthOcclusionQuery(gMeshletOcclusionViewProj, cullWorld, cullMin.xyz,
        cullMax.xyz, uint2(width, height), query)) return false;
    tested = true;
    return HikariDepthOccluded(gMeshletOcclusionDepth, query, uint2(width, height), mipCount, 0.0f);

}
#endif

groupshared HikariMeshletPayload gMeshletAsPayload;
#include "Include/Meshlet/HIKARI_MeshletASCompaction.hlsli"

void HikariMeshletAsInitPayload()
{
    gMeshletAsPayload.visibleClusterCount = 0u;
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
    uint relativeClusterIndex,
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
        relativeClusterIndex < 65536u &&
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

    payloadCluster.clusterIndex = relativeClusterIndex;
    payloadCluster.firstVertex = cluster.firstVertex;
    payloadCluster.firstPrimitive = cluster.firstPrimitive;
    payloadCluster.packedCounts =
        HikariMeshletPackPayloadCounts(relativeClusterIndex, vertexCount, primitiveCount);
    return true;
}

bool HikariMeshletAsCanConeCull(HikariMeshletVisibleRange visible)
{
    return
        !HikariSurfaceUsesVertexDeformation(visible.flags) &&
        visible.drawBucket != HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED &&
        (visible.flags & (
            HIKARI_SURFACE_GPU_SCENE_FLAG_TRANSPARENT |
            HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED)) == 0u;
}

[numthreads(HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD, 1, 1)]
void main(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    const uint visibleRangeIndex = gMeshletVisibleRangeIndex + groupId.x;
    if (groupIndex == 0u)
    {
        HikariMeshletAsInitPayload();
    }
    GroupMemoryBarrierWithGroupSync();

    HikariMeshletVisibleRange visible = gMeshletVisibleRanges[visibleRangeIndex];
    const uint sourceClusterCount = visible.clusterCount;
    const bool candidateLane = groupIndex < min(sourceClusterCount, HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD);
    bool frustumCulled = false, coneTested = false, coneCulled = false;
    bool hzbTested = false, hzbCulled = false;

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
        groupIndex < min(
            sourceClusterCount,
            HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD);
    if (clusterValid)
    {
        clusterIndex = visible.firstCluster + groupIndex;
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
        instance = HikariGetSurfaceGpuSceneInstanceAt(
            visible.gpuSceneInstanceIndex);
        cluster = HikariLoadMeshCluster(metadata, header, clusterIndex);
        clusterValid =
            cluster.indexCount != 0u &&
            cluster.surfaceIndex == visible.clusterSurfaceIndex;
    }

    float4x4 cullWorld = instance.clusterWorld;
    float4 cullMin = cluster.boundsMin, cullMax = cluster.boundsMax;
    const bool requiresFineCull = HikariClusterHasConservativeBounds(instance);
    if (clusterValid && requiresFineCull)
    {
        const float4 worldSphere = HikariClusterBuildCullSphere(
            instance,
            cluster.sphereCenterRadius,
            cluster.boundsMin,
            cluster.boundsMax);
        clusterValid = HikariClusterSphereVisible(
            gCullViewProj,
            worldSphere);
        if (clusterValid) {
            HikariClusterBuildCullBox(instance, cullWorld, cullMin, cullMax);
            clusterValid = HikariClusterBoxVisible(gCullViewProj, cullWorld, cullMin.xyz, cullMax.xyz);
        }
        frustumCulled = !clusterValid;
        coneTested = clusterValid &&
            HikariMeshletAsCanConeCull(visible) &&
            cluster.coneAxisCutoff.w >= 0.0f && cluster.coneAxisCutoff.w < 1.0f &&
            dot(gCullViewProj[3].xyz, gCullViewProj[3].xyz) > 1e-12f;
        if (coneTested && HikariClusterConeBackfacing(
                instance.clusterWorld,
                cluster,
                gCullCameraPos.xyz))
        {
            coneCulled = true;
            clusterValid = false;
        }
    }

#if !defined(HIKARI_MESHLET_DISABLE_OCCLUSION)
    if (clusterValid && requiresFineCull) {
        hzbCulled = HikariMeshletAsOccluded(cullWorld, cullMin, cullMax, hzbTested);
        if (hzbCulled) clusterValid = false;
    }
#endif

    HikariMeshletPayloadCluster payloadCluster = (HikariMeshletPayloadCluster)0;
    const bool payloadValid =
        clusterValid &&
        HikariMeshletAsBuildPayloadCluster(
            header,
            clusterIndex - visible.firstCluster,
            cluster,
            payloadCluster);
#if !defined(HIKARI_MESHLET_DISABLE_OCCLUSION)
    // 診断フレームだけ wave 単位で加算。通常フレームは UAV/atomic を実行しない。
    if ((gMeshletVisibilityControl & 0x80000000u) != 0u) {
        HikariMeshletAsRecordCount(0u, candidateLane);
        HikariMeshletAsRecordCount(4u, frustumCulled);
        HikariMeshletAsRecordCount(8u, coneTested);
        HikariMeshletAsRecordCount(12u, coneCulled);
        HikariMeshletAsRecordCount(16u, hzbTested);
        HikariMeshletAsRecordCount(20u, hzbCulled);
        HikariMeshletAsRecordCount(24u, candidateLane && !requiresFineCull);
        HikariMeshletAsRecordCount(28u, payloadValid);
        HikariMeshletAsRecordCount(32u, candidateLane && !payloadValid &&
            !frustumCulled && !coneCulled && !hzbCulled);
    }
#endif
    const uint compactIndex = HikariMeshletAsCompactVisibleLane(
        payloadValid,
        groupIndex);
    if (payloadValid && compactIndex < HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD)
    {
        gMeshletAsPayload.clusterFirstVertices[compactIndex] =
            payloadCluster.firstVertex;
        gMeshletAsPayload.clusterFirstPrimitives[compactIndex] =
            payloadCluster.firstPrimitive;
        gMeshletAsPayload.clusterPackedCounts[compactIndex] =
            payloadCluster.packedCounts;
    }
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0u) gMeshletAsPayload.visibleClusterCount = gMeshletAsVisibleCount;
    GroupMemoryBarrierWithGroupSync();

    DispatchMesh(
        gMeshletAsPayload.visibleClusterCount,
        1u,
        1u,
        gMeshletAsPayload);
}
