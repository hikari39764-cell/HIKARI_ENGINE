#include "Include/Contracts/HIKARI_ShaderResourceBindings.hlsli"
#include "Include/Cluster/HIKARI_ClusterGpuData.hlsli"
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Meshlet/HIKARI_GpuDrivenSkinning.hlsli"
#include "Include/Surface/HIKARI_TerrainSurfaceDeform.hlsli"

// 光線追跡の加速構造のために、変形する面（骨格の蒙皮・地形の高さ場）の頂点を描画と同じ変形関数で world 空間へ変形して書く。
// 出力は一つの pool の中の [位置 float3 × N][法線 float3 × N][接線 float4 × N]。位置は BLAS の頂点、
// 三つとも命中の解決（HikariDecodeRayTriangle）が読む。添字は HCMESH の頂点番号のまま。

cbuffer RayTracingDeformJob : register(b0)
{
    float4 gWorldRow0;          // world 行列の 1..3 行（列優先の mul(world, p) と同じ）
    float4 gWorldRow1;
    float4 gWorldRow2;
    uint gGeometryDescriptor;   // cluster geometry 記述子 arena 内の番号
    uint gVertexCount;
    uint gPaletteOffsetBytes;   // 蒙皮は骨格の行列の byte 位置、地形は地形の記録の番号
    uint gPaletteMatrixCount;
    uint gOutputBase;           // pool 内の byte 位置
    uint gJobKind;              // 0 蒙皮、1 地形
    uint2 gJobPadding;
};

ByteAddressBuffer gDeformGeometry[HIKARI_SHADER_CLUSTER_GEOMETRY_SRV_COUNT] : register(t0, space1);
RWByteAddressBuffer gDeformedVertices : register(u0);

float3 TransformPoint(float3 p)
{
    return float3(dot(gWorldRow0.xyz, p) + gWorldRow0.w, dot(gWorldRow1.xyz, p) + gWorldRow1.w,
        dot(gWorldRow2.xyz, p) + gWorldRow2.w);
}

float3 TransformVector(float3 v)
{
    return float3(dot(gWorldRow0.xyz, v), dot(gWorldRow1.xyz, v), dot(gWorldRow2.xyz, v));
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint vertexIndex = id.x;
    if (vertexIndex >= gVertexCount || gGeometryDescriptor >= HIKARI_SHADER_CLUSTER_GEOMETRY_SRV_COUNT)
        return;
    ByteAddressBuffer geometry = gDeformGeometry[NonUniformResourceIndex(gGeometryDescriptor)];
    const HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(geometry);
    if (!HikariIsValidClusterGeometryHeader(header) || vertexIndex >= header.vertexCount)
        return;
    const HikariClusterVertex vertex = HikariLoadClusterVertex(geometry, header, vertexIndex);
    float3 position = vertex.position.xyz;
    float3 normal = vertex.normal.xyz;
    float3 tangent = vertex.tangent.xyz;
    HikariSurfaceGpuSceneInstance instance = (HikariSurfaceGpuSceneInstance)0;
    if (gJobKind == 1u)
    {
        instance.flags = HIKARI_SURFACE_GPU_SCENE_FLAG_TERRAIN_SURFACE;
        instance.surfaceFeatureDataIndex = gPaletteOffsetBytes;
        float3 terrainSurfaceData;
        HikariApplyTerrainSurfaceDeform(instance, position, normal, terrainSurfaceData);
    }
    else
    {
        instance.flags = HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED;
        instance.jointPaletteOffsetBytes = gPaletteOffsetBytes;
        instance.jointPaletteMatrixCount = gPaletteMatrixCount;
        HikariApplyGpuDrivenSkinning(geometry, instance, vertexIndex, position, normal, tangent);
    }

    // 法線は world の逆転置が正しいが、蒙皮の対象は一様な拡大の範囲として線形部分で回してから正規化する。
    const float3 worldPosition = TransformPoint(position);
    const float3 worldNormal = HikariNormalizeOrDefault(TransformVector(normal), float3(0.0f, 1.0f, 0.0f));
    const float3 worldTangent = HikariNormalizeOrDefault(TransformVector(tangent), float3(1.0f, 0.0f, 0.0f));
    const uint count = gVertexCount;
    gDeformedVertices.Store3(gOutputBase + vertexIndex * 12u, asuint(worldPosition));
    gDeformedVertices.Store3(gOutputBase + count * 12u + vertexIndex * 12u, asuint(worldNormal));
    gDeformedVertices.Store4(gOutputBase + count * 24u + vertexIndex * 16u,
        asuint(float4(worldTangent, vertex.tangent.w)));
}
