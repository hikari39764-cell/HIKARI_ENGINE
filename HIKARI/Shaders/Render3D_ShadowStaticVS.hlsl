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
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4x4 world = gWorld;
    uint materialFlags = gMaterialFlags;
    float alphaCutoff = gAlphaCutoff;
    uint materialDataIndex = HIKARI_INVALID_SHADOW_MATERIAL_INDEX;

    if (gUseSurfaceGpuScene != 0)
    {
        HikariSurfaceGpuSceneInstance instance = HikariGetSurfaceGpuSceneInstance(input.instanceId);
        world = instance.world;
        materialDataIndex = instance.materialDataIndex;
        HikariMeshMaterialData materialData = HikariGetMeshMaterialData(materialDataIndex);
        materialFlags = materialData.materialFlags;
        alphaCutoff = materialData.pbrParams.w;
    }

    float4 worldPos = mul(world, float4(input.position, 1.0f));
    output.position = mul(gLightViewProj, worldPos);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    output.materialFlags = materialFlags;
    output.alphaCutoff = alphaCutoff;
    output.materialDataIndex = materialDataIndex;
    return output;
}
