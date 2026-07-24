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
    uint4 packetClusterIndices0;
    uint4 packetClusterIndices1;
    uint4 packetClusterIndices2;
    uint4 packetClusterIndices3;
};

static const uint HIKARI_MESHLET_VISIBLE_RANGE_FLAG_PACKET = 1u;
static const uint HIKARI_MESHLET_VISIBLE_RANGE_FLAG_PRECULLED = 2u;
static const uint HIKARI_MESHLET_VISIBLE_RANGE_FLAG_CLUSTER_LIST = 4u;
static const uint HIKARI_MESHLET_VISIBLE_RANGE_FLAG_AS_FINE_CULL = 8u;
static const uint HIKARI_MESHLET_VISIBLE_PACKET_CAPACITY = 16u;
static const uint HIKARI_MESHLET_VISIBLE_CLUSTER_LIST_CAPACITY = 64u;
static const uint HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD =
    HIKARI_CLUSTER_GEOMETRY_CONFIG_AS_CLUSTER_PAYLOAD;
static const uint HIKARI_MESHLET_AS_MODE_COMPACT = 0u;
static const uint HIKARI_MESHLET_AS_MODE_DENSE = 1u;

// AS→MS payload はサイズが占有率に直結するため、cluster あたり 16B に
// 詰める。vertex/primitive count は cluster 設定上 8bit に収まる
// (最大 64 vert / 124 prim)。
struct HikariMeshletPayloadCluster
{
    uint clusterIndex;
    uint firstVertex;
    uint firstPrimitive;
    uint packedCounts; // bits 0-7: vertexCount, bits 8-15: primitiveCount
};

uint HikariMeshletPackPayloadCounts(uint vertexCount, uint primitiveCount)
{
    return (vertexCount & 0xffu) | ((primitiveCount & 0xffu) << 8u);
}

uint HikariMeshletPayloadVertexCount(HikariMeshletPayloadCluster cluster)
{
    return cluster.packedCounts & 0xffu;
}

uint HikariMeshletPayloadPrimitiveCount(HikariMeshletPayloadCluster cluster)
{
    return (cluster.packedCounts >> 8u) & 0xffu;
}

struct HikariMeshletPayload
{
    uint visibleRangeIndex;
    uint visibleClusterCount;
    uint clusterMode;
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
    uint clusterIndices[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
    uint clusterFirstVertices[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
    uint clusterFirstPrimitives[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
    uint clusterPackedCounts[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
};

bool HikariMeshletVisibleRangeUsesPacket(HikariMeshletVisibleRange visible)
{
    return (visible.reserved0 & HIKARI_MESHLET_VISIBLE_RANGE_FLAG_PACKET) != 0u;
}

bool HikariMeshletVisibleRangeIsPreculled(HikariMeshletVisibleRange visible)
{
    return (visible.reserved0 & HIKARI_MESHLET_VISIBLE_RANGE_FLAG_PRECULLED) != 0u;
}

bool HikariMeshletVisibleRangeUsesClusterList(HikariMeshletVisibleRange visible)
{
    return (visible.reserved0 & HIKARI_MESHLET_VISIBLE_RANGE_FLAG_CLUSTER_LIST) != 0u;
}

bool HikariMeshletVisibleRangeRequiresAmplificationFineCull(
    HikariMeshletVisibleRange visible)
{
    return (visible.reserved0 & HIKARI_MESHLET_VISIBLE_RANGE_FLAG_AS_FINE_CULL) != 0u;
}

uint HikariMeshletVisibleRangePacketCount(HikariMeshletVisibleRange visible)
{
    return min(visible.reserved1, HIKARI_MESHLET_VISIBLE_PACKET_CAPACITY);
}

uint HikariMeshletVisibleRangeClusterListCount(HikariMeshletVisibleRange visible)
{
    return min(visible.reserved1, HIKARI_MESHLET_VISIBLE_CLUSTER_LIST_CAPACITY);
}

uint HikariMeshletVisibleRangeClusterListStart(HikariMeshletVisibleRange visible)
{
    return visible.packetClusterIndices0.x;
}

uint HikariMeshletVisibleRangePacketIndex(HikariMeshletVisibleRange visible, uint slot)
{
    if (slot < 4u)
    {
        return visible.packetClusterIndices0[slot];
    }
    if (slot < 8u)
    {
        return visible.packetClusterIndices1[slot - 4u];
    }
    if (slot < 12u)
    {
        return visible.packetClusterIndices2[slot - 8u];
    }
    if (slot < 16u)
    {
        return visible.packetClusterIndices3[slot - 12u];
    }
    return 0xffffffffu;
}

cbuffer MeshletDrawControlCB : register(b8)
{
    uint gMeshletVisibleRangeIndex;
    uint gMeshletDrawPassKind;
    uint gMeshletCullBucket;
    uint gMeshletReserved0;
};

StructuredBuffer<HikariMeshletVisibleRange> gMeshletVisibleRanges : register(t18);
StructuredBuffer<uint> gMeshletVisibleClusterList : register(t19);

#endif
