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
    uint lightmapTextureDescriptorIndex;

    uint materialProgramFlags;
    uint postGroupMask;
    uint surfaceFeatureDataIndex;
    uint stylizedFeaturePadding1;

    float4 lightmapUvScaleBias;
    float4 fxUser[8];
    float4 shellSettings;
};

static const uint HIKARI_SURFACE_GEOMETRY_BACKEND_TRIANGLE_MESH = 0;
static const uint HIKARI_SURFACE_GEOMETRY_BACKEND_CLUSTER_GEOMETRY = 1;

static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_RECEIVE_SHADOW = 1u << 2;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_ALPHA_MASKED = 1u << 3;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_TRANSPARENT = 1u << 4;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED = 1u << 6;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_MATERIAL_FX = 1u << 7;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_CLUSTER_MAINLINE = 1u << 8;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_SURFACE = 1u << 9;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_DEPTH_AWARE = 1u << 10;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_FORWARD_OPAQUE = 1u << 11;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_DEPTH_PREPASS = 1u << 12;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_FORWARD_DEPTH_AWARE = 1u << 13;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_FORWARD_TRANSPARENT = 1u << 14;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_SHADOW = 1u << 15;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED = 1u << 16;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_TERRAIN_SURFACE = 1u << 17;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_CAMERA_CENTERED_OCEAN = 1u << 18;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_FOLIAGE_WIND = 1u << 19;
static const uint HIKARI_SURFACE_GPU_SCENE_FLAG_MATERIAL_PROGRAM = 1u << 20;

// cook 時の静的 bounds/cone は頂点変形後の形状を保証しない。
bool HikariSurfaceUsesVertexDeformation(uint flags)
{
    return (flags & (HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED |
        HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_SURFACE |
        HIKARI_SURFACE_GPU_SCENE_FLAG_TERRAIN_SURFACE |
        HIKARI_SURFACE_GPU_SCENE_FLAG_CAMERA_CENTERED_OCEAN |
        HIKARI_SURFACE_GPU_SCENE_FLAG_FOLIAGE_WIND)) != 0u;
}
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_MESH = 1u << 0;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_MATERIAL = 1u << 1;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY = 1u << 2;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_SHADER_VISIBLE = 1u << 3;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_SURFACE_RANGE = 1u << 4;
static const uint HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_LOD_RANGES = 1u << 5;

StructuredBuffer<HikariSurfaceGpuSceneInstance> gSurfaceGpuSceneBuffer : register(t17);

#if !defined(HIKARI_MASS_GPU_SCENE_DISABLED)
#include "Include/HIKARI_MassGpuScene.hlsli"
#endif

#ifndef HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_CB
cbuffer SurfaceGpuSceneControlCB : register(b8)
{
    uint gSurfaceGpuSceneBaseIndex;
    uint gUseSurfaceGpuScene;
    uint gSurfaceGpuSceneShellLayerCount;
    uint gSurfaceGpuSceneShellLodBits;
};
#endif

HikariSurfaceGpuSceneInstance HikariGetSurfaceGpuSceneInstanceAt(uint absoluteIndex)
{
#if defined(HIKARI_MASS_GPU_SCENE_DISABLED)
    return gSurfaceGpuSceneBuffer[absoluteIndex];
#else
    if (HikariIsMassGpuSceneIndex(absoluteIndex))
    {
        return HikariGetMassSurfaceGpuSceneInstanceAt(absoluteIndex);
    }
    return gSurfaceGpuSceneBuffer[absoluteIndex];
#endif
}

#ifndef HIKARI_SURFACE_GPU_SCENE_SKIP_CONTROL_HELPERS
uint HikariGetSurfaceGpuSceneAbsoluteIndex(uint instanceId)
{
    return gSurfaceGpuSceneBaseIndex + (gSurfaceGpuSceneShellLayerCount != 0u ? 0u : instanceId);
}

HikariSurfaceGpuSceneInstance HikariGetSurfaceGpuSceneInstance(uint instanceId)
{
    return HikariGetSurfaceGpuSceneInstanceAt(HikariGetSurfaceGpuSceneAbsoluteIndex(instanceId));
}
#endif

#endif
