#define HIKARI_MASS_GPU_SCENE_DISABLED 1
cbuffer ShadowCameraCB : register(b0)
{
    float4x4 gLightViewProj;
};

cbuffer ShadowObjectCB : register(b1)
{
    float4x4 gWorld;
    uint gMaterialFlags;
    float gAlphaCutoff;
    float2 gShadowObjectPadding;
};

#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Surface/HIKARI_SandSurfaceDeform.hlsli"

static const uint HIKARI_INVALID_SHADOW_MATERIAL_INDEX = 0xffffffffu;

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    uint instanceId : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    nointerpolation uint materialFlags : MATERIALFLAGS;
    nointerpolation float alphaCutoff : ALPHACUTOFF;
    nointerpolation uint materialDataIndex : MATERIALINDEX;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD2;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4x4 world = gWorld;
    uint materialFlags = gMaterialFlags;
    float alphaCutoff = gAlphaCutoff;
    uint materialDataIndex = HIKARI_INVALID_SHADOW_MATERIAL_INDEX;
    uint surfaceGpuSceneIndex = 0u;
    uint surfaceFeatureFlags = 0u;
    float3 localPosition = input.position;

    if (gUseSurfaceGpuScene != 0)
    {
        HikariSurfaceGpuSceneInstance instance = HikariGetSurfaceGpuSceneInstance(input.instanceId);
        world = instance.world;
        surfaceGpuSceneIndex = input.instanceId;
        surfaceFeatureFlags = instance.flags;
        float3 localNormal = input.normal;
        float3 sandSurfaceData = float3(0.0f, 0.0f, 0.0f);
        HikariApplySandSurfaceDeform(
            instance,
            localPosition,
            localNormal,
            sandSurfaceData);
        materialDataIndex = instance.materialDataIndex;
        HikariMeshMaterialData materialData = HikariGetMeshMaterialData(materialDataIndex);
        materialFlags = materialData.materialFlags;
        alphaCutoff = materialData.pbrParams.w;
    }

    float4 worldPos = mul(world, float4(localPosition, 1.0f));
    output.position = mul(gLightViewProj, worldPos);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    output.materialFlags = materialFlags;
    output.alphaCutoff = alphaCutoff;
    output.materialDataIndex = materialDataIndex;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.surfaceFeatureFlags = surfaceFeatureFlags;
    return output;
}
