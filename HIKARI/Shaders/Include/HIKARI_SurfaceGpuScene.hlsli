#ifndef HIKARI_SURFACE_GPU_SCENE_INCLUDED
#define HIKARI_SURFACE_GPU_SCENE_INCLUDED

// C++ 側の SurfaceGpuSceneInstance と同じ layout に保つ、E
struct HikariSurfaceGpuSceneInstance
{
    float4x4 world;
    float4x4 normalMatrix;
    float4x4 clusterWorld;
    float4x4 clusterNormalMatrix;
    float4 boundsCenterRadius;

    uint sourceRecordIndex;
    uint sourceSurfaceInstanceIndex;
    uint objectIdLow;
    uint objectIdHigh;

    uint meshIndex;
    uint primitiveIndex;
    uint sourceMaterialIndex;
    uint nodeIndex;

    uint flags;
    uint materialDataIndex;
    uint clusterRangeIndex;
    uint clusterRangeCount;

    uint meshResourceIndex;
    uint meshResourceGeneration;
    uint materialResourceIndex;
    uint materialResourceGeneration;

    uint clusterGeometryResourceIndex;
    uint clusterGeometryMetadataSrvDescriptorIndex;
    uint resourceFlags;
    uint geometryBackend;

    uint fxFlags;
    uint clusterGeometrySrvDescriptorIndex;
    uint clusterSurfaceIndex;
    uint clusterIndexCount;

    uint clusterLodRangeIndex;
    uint clusterLodRangeCount;
    uint clusterSelectedLodIndex;
    uint clusterLodFlags;

    uint jointPaletteOffsetBytes;
    uint jointPaletteMatrixCount;
    uint deformationFlags;
    uint deformationReserved;

    float4 fxUser[8];
};

static const uint HIKARI_SURFACE_GEOMETRY_BACKEND_TRIANGLE_MESH = 0;
static const uint HIKARI_SURFACE_GEOMETRY_BACKEND_CLUSTER_GEOMETRY = 1;

static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_RECEIVE_SHADOW = 1u << 2;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_ALPHA_MASKED = 1u << 3;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_TRANSPARENT = 1u << 4;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED = 1u << 6;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_MATERIAL_FX = 1u << 7;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_CLUSTER_MAINLINE = 1u << 8;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX = 1u << 9;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_DEPTH_AWARE = 1u << 10;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_FORWARD_OPAQUE = 1u << 11;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_DEPTH_PREPASS = 1u << 12;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_FORWARD_DEPTH_AWARE = 1u << 13;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_FORWARD_TRANSPARENT = 1u << 14;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_SHADOW = 1u << 15;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED = 1u << 16;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_MESH = 1u << 0;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_MATERIAL = 1u << 1;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY = 1u << 2;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_SHADER_VISIBLE = 1u << 3;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_SURFACE_RANGE = 1u << 4;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_LOD_RANGES = 1u << 5;

StructuredBuffer<HikariSurfaceGpuSceneInstance> gSurfaceGpuSceneBuffer : register(t17);

#ifndef HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_CB
cbuffer SurfaceGpuSceneControlCB : register(b8)
{
    uint gSurfaceGpuSceneBaseIndex;
    uint gUseSurfaceGpuScene;
    uint gSurfaceGpuSceneDrawCommandIndex;
    uint gSurfaceGpuSceneDrawPassKind;
};
#endif

#ifndef HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_HELPERS
uint HikariGetSurfaceGpuSceneAbsoluteIndex(uint instanceId)
{
    return gSurfaceGpuSceneBaseIndex + instanceId;
}

HikariSurfaceGpuSceneInstance HikariGetSurfaceGpuSceneInstanceAt(uint absoluteIndex)
{
    return gSurfaceGpuSceneBuffer[absoluteIndex];
}

HikariSurfaceGpuSceneInstance HikariGetSurfaceGpuSceneInstance(uint instanceId)
{
    return HikariGetSurfaceGpuSceneInstanceAt(HikariGetSurfaceGpuSceneAbsoluteIndex(instanceId));
}
#endif

#endif
