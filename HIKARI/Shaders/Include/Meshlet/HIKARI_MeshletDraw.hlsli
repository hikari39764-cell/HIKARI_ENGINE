#ifndef HIKARI_MESHLET_DRAW_INCLUDED
#define HIKARI_MESHLET_DRAW_INCLUDED

#include "Include/HIKARI_ClusterGeometryConfig.h"

struct HikariMeshletVisibleRange
{
    uint gpuSceneInstanceIndex;
    uint clusterGeometrySrvDescriptorIndex;
    uint firstCluster;
    uint clusterCount;
    uint clusterSurfaceIndex;
    uint passKind;
    uint flags;
    uint clusterGeometryMetadataSrvDescriptorIndex;
    uint lodIndex;
    uint pageIndex;
    uint drawBucket;
    uint sectionIndex;
    uint clusterOffsetBytes;
    uint vertexOffsetBytes;
    uint vertexCount;
    uint meshletPrimitiveOffsetBytes;
    uint meshletPrimitiveCount;
    uint geometryClusterCount;
    uint reserved0;
    uint reserved1;
};

static const uint HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD =
    HIKARI_CLUSTER_GEOMETRY_CONFIG_AS_CLUSTER_PAYLOAD;

// AS→MS は meshlet あたり 12B。相対 cluster index と counts を 1 DWORD に
// 詰め、頂点/primitive offset は 32bit のまま保持する。再ロードは不要。
struct HikariMeshletPayloadCluster
{
    uint clusterIndex;
    uint firstVertex;
    uint firstPrimitive;
    uint packedCounts; // bits 0-15: relative index, 16-22: vertices, 23-29: primitives
};

uint HikariMeshletPackPayloadCounts(uint relativeClusterIndex, uint vertexCount, uint primitiveCount)
{
    return relativeClusterIndex | (vertexCount << 16u) | (primitiveCount << 23u);
}

uint HikariMeshletPayloadVertexCount(HikariMeshletPayloadCluster cluster)
{
    return (cluster.packedCounts >> 16u) & 0x7fu;
}

uint HikariMeshletPayloadPrimitiveCount(HikariMeshletPayloadCluster cluster)
{
    return (cluster.packedCounts >> 23u) & 0x7fu;
}

struct HikariMeshletPayload
{
    uint visibleClusterCount;
    uint clusterGeometryPoolIndex;

    uint gpuSceneInstanceIndex;
    uint clusterSurfaceIndex;
    uint sectionIndex;
    uint lodIndex;

    uint drawBucket;
    uint vertexOffsetBytes;
    uint vertexCount;
    uint meshletPrimitiveOffsetBytes;

    uint meshletPrimitiveCount;
    uint firstCluster;
    uint rangeClusterCount;
    uint geometryClusterCount;

    // Keep compact cluster payloads in SoA form. DXIL requires dynamic TGSM
    // accesses to resolve directly to a scalar global allocation; an AoS
    // member access introduces an ambiguous intermediate structure pointer.
    uint clusterFirstVertices[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
    uint clusterFirstPrimitives[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
    uint clusterPackedCounts[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
};

cbuffer MeshletDrawControlCB : register(b8)
{
    uint gMeshletVisibleRangeIndex;
    uint gMeshletDrawPassKind;
    uint gMeshletCullBucket;
    uint gMeshletVisibleRangeCount;
};

StructuredBuffer<HikariMeshletVisibleRange> gMeshletVisibleRanges : register(t18);

#endif
