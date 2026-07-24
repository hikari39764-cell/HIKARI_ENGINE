#ifndef HIKARI_MESH_OBJECT_DATA_INCLUDED
#define HIKARI_MESH_OBJECT_DATA_INCLUDED

#include "Include/HIKARI_MeshMaterialData.hlsli"

// Keep this layout in sync with C++ ObjectGpuData.
struct HikariMeshObjectData
{
    float4x4 world;
    float4x4 normalMatrix;
    float4 baseColor;
    uint hasBaseColorTexture;
    uint fxFlags;
    uint materialFlags;
    float alphaCutoff;
    float4 emissiveFactor;
    uint hasNormalTexture;
    float normalScale;
    float2 normalPadding;
    uint receiveShadow;
    float3 shadowObjectPadding;
    uint hasEmissiveTexture;
    float3 emissivePadding;
    float metallicFactor;
    float roughnessFactor;
    uint hasMetallicRoughnessTexture;
    uint hasOcclusionTexture;
    float occlusionStrength;
    uint materialDataIndex;
    float2 pbrPadding;
    float4 fxUser[8];
};

StructuredBuffer<HikariMeshObjectData> gObjectDataBuffer : register(t15);

#ifndef HIKARI_SURFACE_GPU_SCENE_CONSUME
#define HIKARI_SURFACE_GPU_SCENE_CONSUME 1
#endif

#if HIKARI_SURFACE_GPU_SCENE_CONSUME
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#endif

cbuffer ObjectIndexCB : register(b6)
{
    uint gObjectDataIndex;
    uint3 gObjectDataPadding;
};

uint HikariGetObjectDataAbsoluteIndex(uint objectDataIndex, uint instanceId)
{
    return objectDataIndex + instanceId;
}

#if !HIKARI_SURFACE_GPU_SCENE_CONSUME
uint HikariGetSurfaceGpuSceneAbsoluteIndex(uint instanceId)
{
    return instanceId;
}
#endif

HikariMeshObjectData HikariGetMeshObjectData(uint objectDataIndex)
{
    return gObjectDataBuffer[objectDataIndex];
}

#if HIKARI_SURFACE_GPU_SCENE_CONSUME
HikariMeshObjectData HikariBuildMeshObjectDataFromSurfaceGpuScene(
    HikariSurfaceGpuSceneInstance instance)
{
    HikariMeshObjectData data = (HikariMeshObjectData)0;
    data.world = instance.world;
    data.normalMatrix = instance.normalMatrix;
    data.fxFlags = instance.fxFlags;
    data.receiveShadow =
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_RECEIVE_SHADOW) != 0 ? 1u : 0u;
    data.materialDataIndex = instance.materialDataIndex;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        data.fxUser[i] = instance.fxUser[i];
    }
    return data;
}

HikariMeshObjectData HikariGetMeshObjectDataForPixel(
    uint objectDataIndex,
    uint surfaceGpuSceneIndex)
{
#if defined(HIKARI_FORCE_SURFACE_GPU_SCENE_PIXEL) && HIKARI_FORCE_SURFACE_GPU_SCENE_PIXEL
    return HikariBuildMeshObjectDataFromSurfaceGpuScene(
        HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex));
#else
    if (gUseSurfaceGpuScene != 0)
    {
        return HikariBuildMeshObjectDataFromSurfaceGpuScene(
            HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex));
    }
    return HikariGetMeshObjectData(objectDataIndex);
#endif
}

HikariMeshObjectData HikariGetMeshObjectDataForSurfaceIndex(
    uint objectDataIndex,
    uint surfaceGpuSceneIndex)
{
    if (gUseSurfaceGpuScene != 0)
    {
        return HikariBuildMeshObjectDataFromSurfaceGpuScene(
            HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex));
    }
    return HikariGetMeshObjectData(objectDataIndex);
}
#else
HikariMeshObjectData HikariGetMeshObjectDataForPixel(
    uint objectDataIndex,
    uint surfaceGpuSceneIndex)
{
    return HikariGetMeshObjectData(objectDataIndex);
}

HikariMeshObjectData HikariGetMeshObjectDataForSurfaceIndex(
    uint objectDataIndex,
    uint surfaceGpuSceneIndex)
{
    return HikariGetMeshObjectData(objectDataIndex);
}
#endif

HikariMeshObjectData HikariGetMeshObjectDataForInstance(uint objectDataIndex, uint instanceId)
{
#if HIKARI_SURFACE_GPU_SCENE_CONSUME
    if (gUseSurfaceGpuScene != 0)
    {
        return HikariBuildMeshObjectDataFromSurfaceGpuScene(
            HikariGetSurfaceGpuSceneInstance(instanceId));
    }
#endif
    return HikariGetMeshObjectData(objectDataIndex);
}

#define gObjectData gObjectDataBuffer[gObjectDataIndex]
#define gWorld gObjectData.world
#define gNormalMatrix gObjectData.normalMatrix
#define gFxFlags gObjectData.fxFlags
#define gNormalPadding gObjectData.normalPadding
#define gReceiveShadow gObjectData.receiveShadow
#define gShadowObjectPadding gObjectData.shadowObjectPadding
#define gEmissivePadding gObjectData.emissivePadding
#define gPbrPadding gObjectData.pbrPadding
#define gFxUser gObjectData.fxUser

#endif
