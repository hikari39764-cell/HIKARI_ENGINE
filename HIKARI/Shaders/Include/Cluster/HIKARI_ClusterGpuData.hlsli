#ifndef HIKARI_CLUSTER_GPU_DATA_INCLUDED
#define HIKARI_CLUSTER_GPU_DATA_INCLUDED

#include "Include/HIKARI_ClusterGeometryConfig.h"

static const uint HIKARI_CLUSTER_GEOMETRY_GPU_MAGIC = 0x534c4348u;
// C++ 側 kClusterGeometryGpuVersion (HIKARI_ClusterGpuData.h) と同期させる。
static const uint HIKARI_CLUSTER_GEOMETRY_GPU_VERSION = 12u;
static const uint HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX = 0xffffffffu;
static const uint HIKARI_CLUSTER_GEOMETRY_HEADER_BYTES = 144u;
static const uint HIKARI_CLUSTER_GEOMETRY_SURFACE_BYTES = 112u;
static const uint HIKARI_CLUSTER_GEOMETRY_SURFACE_LOD_RANGE_BYTES = 64u;
static const uint HIKARI_CLUSTER_GEOMETRY_SURFACE_SECTION_BYTES = 128u;
static const uint HIKARI_CLUSTER_GEOMETRY_CLUSTER_BYTES = 112u;
static const uint HIKARI_CLUSTER_GEOMETRY_PAGE_BYTES = 64u;
static const uint HIKARI_CLUSTER_GEOMETRY_VERTEX_POSITION_BYTES = 12u;
static const uint HIKARI_CLUSTER_GEOMETRY_VERTEX_ATTRIBUTE_BYTES = 24u;
static const uint HIKARI_CLUSTER_GEOMETRY_SKIN_VERTEX_BYTES = 16u;
static const uint HIKARI_CLUSTER_GEOMETRY_MESHLET_PRIMITIVE_BYTES = 4u;
static const uint HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_PRIMITIVES =
    HIKARI_CLUSTER_GEOMETRY_CONFIG_MAX_MESHLET_TRIANGLES;
static const uint HIKARI_CLUSTER_GEOMETRY_MAX_MESHLET_VERTICES =
    HIKARI_CLUSTER_GEOMETRY_CONFIG_MAX_MESHLET_VERTICES;

struct HikariClusterGeometryHeader
{
    uint magic;
    uint version;
    uint flags;
    uint byteSize;

    uint surfaceCount;
    uint clusterCount;
    uint pageCount;
    uint vertexCount;

    uint indexCount;
    uint materialSlotCount;
    uint totalTriangleCount;
    uint totalVertexCount;

    uint surfaceOffsetBytes;
    uint clusterOffsetBytes;
    uint pageOffsetBytes;
    uint vertexOffsetBytes;

    uint indexOffsetBytes;
    uint materialSlotOffsetBytes;
    uint meshletPrimitiveCount;
    uint meshletPrimitiveOffsetBytes;

    uint surfaceLodRangeCount;
    uint surfaceLodRangeOffsetBytes;
    uint surfaceSectionCount;
    uint surfaceSectionOffsetBytes;

    uint skinVertexCount;
    uint skinVertexOffsetBytes;
    uint reserved0;
    uint reserved1;

    float4 localBoundsMin;
    float4 localBoundsMax;
};

struct HikariClusterGeometrySurface
{
    uint nodeIndex;
    uint meshIndex;
    uint primitiveIndex;
    uint materialIndex;

    uint firstCluster;
    uint clusterCount;
    uint firstIndex;
    uint indexCount;

    uint firstVertex;
    uint vertexCount;
    uint flags;
    uint firstPage;

    uint pageCount;
    uint firstPrimitive;
    uint primitiveCount;
    uint firstLodRange;

    uint lodRangeCount;
    uint firstSection;
    uint sectionCount;
    uint reserved4;

    float4 boundsMin;
    float4 boundsMax;
};

struct HikariClusterGeometrySurfaceLodRange
{
    uint surfaceIndex;
    uint lodIndex;
    uint firstCluster;
    uint clusterCount;

    uint firstIndex;
    uint indexCount;
    uint firstVertex;
    uint vertexCount;

    uint firstPage;
    uint pageCount;
    uint firstPrimitive;
    uint primitiveCount;

    float geometricError;
    float minScreenRadius;
    uint flags;
    uint sectionIndex;
};

struct HikariClusterGeometrySurfaceSection
{
    uint surfaceIndex;
    uint sectionIndex;
    uint firstCluster;
    uint clusterCount;

    uint firstIndex;
    uint indexCount;
    uint firstVertex;
    uint vertexCount;

    uint firstPage;
    uint pageCount;
    uint firstPrimitive;
    uint primitiveCount;

    uint firstLodRange;
    uint lodRangeCount;
    uint flags;
    uint reserved0;

    float4 boundsMin;
    float4 boundsMax;
    float4 lodMetricCenterRadius;

    float lodErrorBudgetNdc;
    uint reserved1;
    uint reserved2;
    uint reserved3;
};

struct HikariMeshCluster
{
    uint surfaceIndex;
    uint firstIndex;
    uint indexCount;
    uint firstVertex;

    uint vertexCount;
    uint triangleCount;
    uint flags;
    uint firstPrimitive;

    float4 boundsMin;
    float4 boundsMax;
    float4 sphereCenterRadius;
    float4 coneApex;
    float4 coneAxisCutoff;
};

struct HikariClusterPage
{
    uint firstCluster;
    uint clusterCount;
    uint firstIndex;
    uint indexCount;

    uint firstVertex;
    uint vertexCount;
    uint firstPrimitive;
    uint primitiveCount;

    float4 boundsMin;
    float4 boundsMax;
};

struct HikariClusterVertex
{
    float4 position;
    float4 normal;
    float4 tangent;
    float4 uv01;
    float4 color;
};

struct HikariClusterSkinVertex
{
    uint4 joints;
    float4 weights;
};

struct HikariMeshletPrimitive
{
    uint i0;
    uint i1;
    uint i2;
    uint reserved0;
};

bool HikariIsValidClusterGeometryHeader(HikariClusterGeometryHeader header)
{
    return
        header.magic == HIKARI_CLUSTER_GEOMETRY_GPU_MAGIC &&
        header.version == HIKARI_CLUSTER_GEOMETRY_GPU_VERSION &&
        header.byteSize > 0;
}

HikariClusterGeometryHeader HikariLoadClusterGeometryHeader(ByteAddressBuffer buffer)
{
    HikariClusterGeometryHeader header = (HikariClusterGeometryHeader)0;
    uint4 v0 = buffer.Load4(0u);
    uint4 v1 = buffer.Load4(16u);
    uint4 v2 = buffer.Load4(32u);
    uint4 v3 = buffer.Load4(48u);
    uint4 v4 = buffer.Load4(64u);
    uint4 v5 = buffer.Load4(80u);
    uint4 v6 = buffer.Load4(96u);
    header.magic = v0.x;
    header.version = v0.y;
    header.flags = v0.z;
    header.byteSize = v0.w;
    header.surfaceCount = v1.x;
    header.clusterCount = v1.y;
    header.pageCount = v1.z;
    header.vertexCount = v1.w;
    header.indexCount = v2.x;
    header.materialSlotCount = v2.y;
    header.totalTriangleCount = v2.z;
    header.totalVertexCount = v2.w;
    header.surfaceOffsetBytes = v3.x;
    header.clusterOffsetBytes = v3.y;
    header.pageOffsetBytes = v3.z;
    header.vertexOffsetBytes = v3.w;
    header.indexOffsetBytes = v4.x;
    header.materialSlotOffsetBytes = v4.y;
    header.meshletPrimitiveCount = v4.z;
    header.meshletPrimitiveOffsetBytes = v4.w;
    header.surfaceLodRangeCount = v5.x;
    header.surfaceLodRangeOffsetBytes = v5.y;
    header.surfaceSectionCount = v5.z;
    header.surfaceSectionOffsetBytes = v5.w;
    header.skinVertexCount = v6.x;
    header.skinVertexOffsetBytes = v6.y;
    header.reserved0 = v6.z;
    header.reserved1 = v6.w;
    header.localBoundsMin = asfloat(buffer.Load4(112u));
    header.localBoundsMax = asfloat(buffer.Load4(128u));
    return header;
}

HikariClusterGeometrySurface HikariLoadClusterGeometrySurface(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint surfaceIndex)
{
    HikariClusterGeometrySurface surface = (HikariClusterGeometrySurface)0;
    uint offset = header.surfaceOffsetBytes + surfaceIndex * HIKARI_CLUSTER_GEOMETRY_SURFACE_BYTES;
    uint4 v0 = buffer.Load4(offset + 0u);
    uint4 v1 = buffer.Load4(offset + 16u);
    uint4 v2 = buffer.Load4(offset + 32u);
    uint4 v3 = buffer.Load4(offset + 48u);
    uint4 v4 = buffer.Load4(offset + 64u);
    surface.nodeIndex = v0.x;
    surface.meshIndex = v0.y;
    surface.primitiveIndex = v0.z;
    surface.materialIndex = v0.w;
    surface.firstCluster = v1.x;
    surface.clusterCount = v1.y;
    surface.firstIndex = v1.z;
    surface.indexCount = v1.w;
    surface.firstVertex = v2.x;
    surface.vertexCount = v2.y;
    surface.flags = v2.z;
    surface.firstPage = v2.w;
    surface.pageCount = v3.x;
    surface.firstPrimitive = v3.y;
    surface.primitiveCount = v3.z;
    surface.firstLodRange = v3.w;
    surface.lodRangeCount = v4.x;
    surface.firstSection = v4.y;
    surface.sectionCount = v4.z;
    surface.reserved4 = v4.w;
    surface.boundsMin = asfloat(buffer.Load4(offset + 80u));
    surface.boundsMax = asfloat(buffer.Load4(offset + 96u));
    return surface;
}

HikariClusterGeometrySurfaceLodRange HikariLoadClusterGeometrySurfaceLodRange(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint lodRangeIndex)
{
    HikariClusterGeometrySurfaceLodRange range = (HikariClusterGeometrySurfaceLodRange)0;
    uint offset = header.surfaceLodRangeOffsetBytes +
        lodRangeIndex * HIKARI_CLUSTER_GEOMETRY_SURFACE_LOD_RANGE_BYTES;
    uint4 v0 = buffer.Load4(offset + 0u);
    uint4 v1 = buffer.Load4(offset + 16u);
    uint4 v2 = buffer.Load4(offset + 32u);
    uint4 v3 = buffer.Load4(offset + 48u);
    range.surfaceIndex = v0.x;
    range.lodIndex = v0.y;
    range.firstCluster = v0.z;
    range.clusterCount = v0.w;
    range.firstIndex = v1.x;
    range.indexCount = v1.y;
    range.firstVertex = v1.z;
    range.vertexCount = v1.w;
    range.firstPage = v2.x;
    range.pageCount = v2.y;
    range.firstPrimitive = v2.z;
    range.primitiveCount = v2.w;
    range.geometricError = asfloat(v3.x);
    range.minScreenRadius = asfloat(v3.y);
    range.flags = v3.z;
    range.sectionIndex = v3.w;
    return range;
}

HikariClusterGeometrySurfaceSection HikariLoadClusterGeometrySurfaceSection(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint sectionTableIndex)
{
    HikariClusterGeometrySurfaceSection section = (HikariClusterGeometrySurfaceSection)0;
    uint offset = header.surfaceSectionOffsetBytes +
        sectionTableIndex * HIKARI_CLUSTER_GEOMETRY_SURFACE_SECTION_BYTES;
    uint4 v0 = buffer.Load4(offset + 0u);
    uint4 v1 = buffer.Load4(offset + 16u);
    uint4 v2 = buffer.Load4(offset + 32u);
    uint4 v3 = buffer.Load4(offset + 48u);
    section.surfaceIndex = v0.x;
    section.sectionIndex = v0.y;
    section.firstCluster = v0.z;
    section.clusterCount = v0.w;
    section.firstIndex = v1.x;
    section.indexCount = v1.y;
    section.firstVertex = v1.z;
    section.vertexCount = v1.w;
    section.firstPage = v2.x;
    section.pageCount = v2.y;
    section.firstPrimitive = v2.z;
    section.primitiveCount = v2.w;
    section.firstLodRange = v3.x;
    section.lodRangeCount = v3.y;
    section.flags = v3.z;
    section.reserved0 = v3.w;
    section.boundsMin = asfloat(buffer.Load4(offset + 64u));
    section.boundsMax = asfloat(buffer.Load4(offset + 80u));
    section.lodMetricCenterRadius = asfloat(buffer.Load4(offset + 96u));
    uint4 v4 = buffer.Load4(offset + 112u);
    section.lodErrorBudgetNdc = asfloat(v4.x);
    section.reserved1 = v4.y;
    section.reserved2 = v4.z;
    section.reserved3 = v4.w;
    return section;
}

HikariMeshCluster HikariLoadMeshCluster(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint clusterIndex)
{
    HikariMeshCluster cluster = (HikariMeshCluster)0;
    uint offset = header.clusterOffsetBytes + clusterIndex * HIKARI_CLUSTER_GEOMETRY_CLUSTER_BYTES;
    uint4 v0 = buffer.Load4(offset + 0u);
    uint4 v1 = buffer.Load4(offset + 16u);
    cluster.surfaceIndex = v0.x;
    cluster.firstIndex = v0.y;
    cluster.indexCount = v0.z;
    cluster.firstVertex = v0.w;
    cluster.vertexCount = v1.x;
    cluster.triangleCount = v1.y;
    cluster.flags = v1.z;
    cluster.firstPrimitive = v1.w;
    cluster.boundsMin = asfloat(buffer.Load4(offset + 32u));
    cluster.boundsMax = asfloat(buffer.Load4(offset + 48u));
    cluster.sphereCenterRadius = asfloat(buffer.Load4(offset + 64u));
    cluster.coneApex = asfloat(buffer.Load4(offset + 80u));
    cluster.coneAxisCutoff = asfloat(buffer.Load4(offset + 96u));
    return cluster;
}

HikariClusterPage HikariLoadClusterPage(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint pageIndex)
{
    HikariClusterPage page = (HikariClusterPage)0;
    uint offset = header.pageOffsetBytes + pageIndex * HIKARI_CLUSTER_GEOMETRY_PAGE_BYTES;
    uint4 v0 = buffer.Load4(offset + 0u);
    uint4 v1 = buffer.Load4(offset + 16u);
    page.firstCluster = v0.x;
    page.clusterCount = v0.y;
    page.firstIndex = v0.z;
    page.indexCount = v0.w;
    page.firstVertex = v1.x;
    page.vertexCount = v1.y;
    page.firstPrimitive = v1.z;
    page.primitiveCount = v1.w;
    page.boundsMin = asfloat(buffer.Load4(offset + 32u));
    page.boundsMax = asfloat(buffer.Load4(offset + 48u));
    return page;
}

uint HikariClusterVertexAttributeOffset(
    HikariClusterGeometryHeader header,
    uint vertexIndex)
{
    uint attributeBase =
        header.vertexOffsetBytes + header.vertexCount * HIKARI_CLUSTER_GEOMETRY_VERTEX_POSITION_BYTES;
    return attributeBase + vertexIndex * HIKARI_CLUSTER_GEOMETRY_VERTEX_ATTRIBUTE_BYTES;
}

float HikariUnpackSnorm16(uint value)
{
    int signedValue = int(value & 0x7fffu) - int(value & 0x8000u);
    return max(float(signedValue) / 32767.0f, -1.0f);
}

float2 HikariUnpackSnorm16x2(uint packed)
{
    return float2(
        HikariUnpackSnorm16(packed),
        HikariUnpackSnorm16(packed >> 16u));
}

float HikariUnpackHalf16(uint value)
{
    return f16tof32(value & 0xffffu).x;
}

float2 HikariUnpackHalf16x2(uint packed)
{
    return f16tof32(packed);
}

float3 HikariNormalizeOrDefault(float3 value, float3 fallbackValue)
{
    return dot(value, value) > 1.0e-8f ? normalize(value) : fallbackValue;
}

HikariClusterVertex HikariDecodeClusterVertex(
    float3 position,
    uint4 a0,
    uint2 a1)
{
    HikariClusterVertex vertex = (HikariClusterVertex)0;
    float2 normalXY = HikariUnpackSnorm16x2(a0.x);
    float normalZ = HikariUnpackSnorm16(a0.y);
    float tangentW = HikariUnpackSnorm16(a0.y >> 16u);
    float2 tangentXY = HikariUnpackSnorm16x2(a0.z);
    float tangentZ = HikariUnpackSnorm16(a0.w);
    float uv0x = HikariUnpackHalf16(a0.w >> 16u);
    float2 uv0YUv1X = HikariUnpackHalf16x2(a1.x);
    float uv1y = HikariUnpackHalf16(a1.y);

    vertex.position = float4(position, 1.0f);
    vertex.normal = float4(
        HikariNormalizeOrDefault(float3(normalXY, normalZ), float3(0.0f, 1.0f, 0.0f)),
        0.0f);
    vertex.tangent = float4(
        HikariNormalizeOrDefault(float3(tangentXY, tangentZ), float3(1.0f, 0.0f, 0.0f)),
        tangentW >= 0.0f ? 1.0f : -1.0f);
    vertex.uv01 = float4(uv0x, uv0YUv1X.x, uv0YUv1X.y, uv1y);
    vertex.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    return vertex;
}

HikariClusterVertex HikariLoadClusterVertex(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint vertexIndex)
{
    uint positionOffset =
        header.vertexOffsetBytes + vertexIndex * HIKARI_CLUSTER_GEOMETRY_VERTEX_POSITION_BYTES;
    uint attributeOffset = HikariClusterVertexAttributeOffset(header, vertexIndex);
    return HikariDecodeClusterVertex(
        asfloat(buffer.Load3(positionOffset)),
        buffer.Load4(attributeOffset + 0u),
        buffer.Load2(attributeOffset + 16u));
}

HikariClusterVertex HikariLoadClusterVertexShading(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint vertexIndex)
{
    uint positionOffset =
        header.vertexOffsetBytes + vertexIndex * HIKARI_CLUSTER_GEOMETRY_VERTEX_POSITION_BYTES;
    uint attributeOffset = HikariClusterVertexAttributeOffset(header, vertexIndex);
    return HikariDecodeClusterVertex(
        asfloat(buffer.Load3(positionOffset)),
        buffer.Load4(attributeOffset + 0u),
        buffer.Load2(attributeOffset + 16u));
}

HikariClusterSkinVertex HikariLoadClusterSkinVertex(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint vertexIndex)
{
    HikariClusterSkinVertex skin = (HikariClusterSkinVertex)0;
    if (vertexIndex >= header.skinVertexCount || header.skinVertexOffsetBytes == 0u)
    {
        skin.weights.x = 1.0f;
        return skin;
    }

    uint offset = header.skinVertexOffsetBytes +
        vertexIndex * HIKARI_CLUSTER_GEOMETRY_SKIN_VERTEX_BYTES;
    uint4 packed = buffer.Load4(offset);
    skin.joints = uint4(
        packed.x & 0xffffu,
        packed.x >> 16u,
        packed.y & 0xffffu,
        packed.y >> 16u);
    skin.weights = float4(
        HikariUnpackHalf16(packed.z),
        HikariUnpackHalf16(packed.z >> 16u),
        HikariUnpackHalf16(packed.w),
        HikariUnpackHalf16(packed.w >> 16u));
    float weightSum = dot(skin.weights, 1.0f);
    skin.weights = weightSum > 1.0e-6f
        ? skin.weights / weightSum
        : float4(1.0f, 0.0f, 0.0f, 0.0f);
    return skin;
}

float4 HikariLoadClusterVertexPosition(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint vertexIndex)
{
    uint offset =
        header.vertexOffsetBytes + vertexIndex * HIKARI_CLUSTER_GEOMETRY_VERTEX_POSITION_BYTES;
    return float4(asfloat(buffer.Load3(offset)), 1.0f);
}

float4 HikariLoadClusterVertexNormal(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint vertexIndex)
{
    uint offset = HikariClusterVertexAttributeOffset(header, vertexIndex);
    uint4 a0 = buffer.Load4(offset + 0u);
    float2 normalXY = HikariUnpackSnorm16x2(a0.x);
    float normalZ = HikariUnpackSnorm16(a0.y);
    return float4(
        HikariNormalizeOrDefault(float3(normalXY, normalZ), float3(0.0f, 1.0f, 0.0f)),
        0.0f);
}

float4 HikariLoadClusterVertexUv01(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint vertexIndex)
{
    uint offset = HikariClusterVertexAttributeOffset(header, vertexIndex);
    uint4 a0 = buffer.Load4(offset + 0u);
    uint2 a1 = buffer.Load2(offset + 16u);
    float uv0x = HikariUnpackHalf16(a0.w >> 16u);
    float2 uv0YUv1X = HikariUnpackHalf16x2(a1.x);
    float uv1y = HikariUnpackHalf16(a1.y);
    return float4(uv0x, uv0YUv1X.x, uv0YUv1X.y, uv1y);
}

uint HikariLoadClusterIndex(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint indexIndex)
{
    uint byteOffset = header.indexOffsetBytes + indexIndex * 2u;
    uint packed = buffer.Load(byteOffset & ~3u);
    return (byteOffset & 2u) == 0u
        ? (packed & 0xffffu)
        : ((packed >> 16u) & 0xffffu);
}

HikariMeshletPrimitive HikariLoadMeshletPrimitive(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint primitiveIndex)
{
    HikariMeshletPrimitive primitive = (HikariMeshletPrimitive)0;
    uint offset = header.meshletPrimitiveOffsetBytes +
        primitiveIndex * HIKARI_CLUSTER_GEOMETRY_MESHLET_PRIMITIVE_BYTES;
    uint packed = buffer.Load(offset);
    primitive.i0 = packed & 0xffu;
    primitive.i1 = (packed >> 8u) & 0xffu;
    primitive.i2 = (packed >> 16u) & 0xffu;
    primitive.reserved0 = (packed >> 24u) & 0xffu;
    return primitive;
}

uint3 HikariLoadMeshletPrimitiveIndices(
    ByteAddressBuffer buffer,
    HikariClusterGeometryHeader header,
    uint primitiveIndex)
{
    uint offset = header.meshletPrimitiveOffsetBytes +
        primitiveIndex * HIKARI_CLUSTER_GEOMETRY_MESHLET_PRIMITIVE_BYTES;
    uint packed = buffer.Load(offset);
    return uint3(
        packed & 0xffu,
        (packed >> 8u) & 0xffu,
        (packed >> 16u) & 0xffu);
}

#endif
