#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Cluster/HIKARI_ClusterGpuData.hlsli"

ByteAddressBuffer gGeometry : register(t0);
ByteAddressBuffer gMetadata : register(t1);
RWByteAddressBuffer gIndices : register(u0);
cbuffer Constants : register(b0)
{
    uint gClusterCount;
    uint gPrimitiveCount;
    uint gVertexCount;
    uint gDispatchWidth;
};

// HCMESH の meshlet-local 8-bit index を一度だけ global R32 index へ展開する。
// fallback R16 index stream の有無に依存しない。頂点/属性は元の GPU allocation を共有する。
[numthreads(64,1,1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    const uint clusterIndex = group.x + group.y * gDispatchWidth;
    if (clusterIndex >= gClusterCount) return;
    const HikariClusterGeometryHeader metadata = HikariLoadClusterGeometryHeader(gMetadata);
    const HikariClusterGeometryHeader geometry = HikariLoadClusterGeometryHeader(gGeometry);
    const HikariMeshCluster cluster = HikariLoadMeshCluster(gMetadata, metadata, clusterIndex);
    for (uint local = thread; local < cluster.triangleCount; local += 64)
    {
        const uint primitiveIndex = cluster.firstPrimitive + local;
        if (primitiveIndex >= gPrimitiveCount) continue;
        const uint3 localIndices = HikariLoadMeshletPrimitiveIndices(gGeometry, geometry, primitiveIndex);
        const uint3 indices = cluster.firstVertex + localIndices;
        const bool valid = all(localIndices < cluster.vertexCount) && all(indices < gVertexCount);
        gIndices.Store3(primitiveIndex * 12, valid ? indices : uint3(0,0,0));
    }
}
