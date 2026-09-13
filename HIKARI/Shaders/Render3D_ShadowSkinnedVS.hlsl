#define HIKARI_MASS_GPU_SCENE_DISABLED 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Render3D/HIKARI_MeshCameraData.hlsli"

cbuffer ShadowObjectCB : register(b1)
{
    float4x4 gWorld;
    uint gMaterialFlags;
    float gAlphaCutoff;
    float2 gShadowObjectPadding;
};

#define MAX_JOINTS 128

cbuffer JointPaletteCB : register(b3)
{
    float4x4 gJointMatrices[MAX_JOINTS];
};

#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Surface/HIKARI_SurfaceTangentFrame.hlsli"

static const uint HIKARI_INVALID_SHADOW_MATERIAL_INDEX = 0xffffffffu;

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color0 : COLOR0;
    uint4 joints : JOINTS0;
    float4 weights : WEIGHTS0;
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
    float3 worldPosWS : TEXCOORD4;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
};

float4x4 ResolveJointMatrix(uint jointIndex)
{
    return gJointMatrices[min(jointIndex, MAX_JOINTS - 1)];
}

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 localPos =
        mul(ResolveJointMatrix(input.joints.x), float4(input.position, 1.0f)) * input.weights.x +
        mul(ResolveJointMatrix(input.joints.y), float4(input.position, 1.0f)) * input.weights.y +
        mul(ResolveJointMatrix(input.joints.z), float4(input.position, 1.0f)) * input.weights.z +
        mul(ResolveJointMatrix(input.joints.w), float4(input.position, 1.0f)) * input.weights.w;

    float4x4 world = gWorld;
    float3x3 normalMatrix = (float3x3)world;
    uint materialFlags = gMaterialFlags;
    float alphaCutoff = gAlphaCutoff;
    uint materialDataIndex = HIKARI_INVALID_SHADOW_MATERIAL_INDEX;
    uint surfaceGpuSceneIndex = 0u;
    uint surfaceFeatureFlags = 0u;

    if (gUseSurfaceGpuScene != 0u)
    {
        HikariSurfaceGpuSceneInstance instance =
            HikariGetSurfaceGpuSceneInstance(input.instanceId);
        world = instance.world;
        normalMatrix = (float3x3)instance.normalMatrix;
        surfaceGpuSceneIndex = HikariGetSurfaceGpuSceneAbsoluteIndex(input.instanceId);
        surfaceFeatureFlags = instance.flags;
        materialDataIndex = instance.materialDataIndex;
        HikariMeshMaterialData materialData = HikariGetMeshMaterialData(materialDataIndex);
        materialFlags = materialData.materialFlags;
        alphaCutoff = materialData.pbrParams.w;
    }

    float4 worldPos = mul(world, float4(localPos.xyz, 1.0f));
    output.position = mul(gViewProj, worldPos);
    output.uv = input.uv0;
    output.uv1 = input.uv1;
    output.materialFlags = materialFlags;
    output.alphaCutoff = alphaCutoff;
    output.materialDataIndex = materialDataIndex;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.surfaceFeatureFlags = surfaceFeatureFlags;
    output.worldPosWS = worldPos.xyz;
    output.normalWS = 0.0f.xxx;
    output.tangentWS = 0.0f.xxxx;
    if ((materialFlags & HIKARI_MATERIAL_FLAG_ALPHA_MASK) != 0u)
    {
        const float3x3 skin =
            (float3x3)ResolveJointMatrix(input.joints.x) * input.weights.x +
            (float3x3)ResolveJointMatrix(input.joints.y) * input.weights.y +
            (float3x3)ResolveJointMatrix(input.joints.z) * input.weights.z +
            (float3x3)ResolveJointMatrix(input.joints.w) * input.weights.w;
        output.normalWS = mul(normalMatrix, mul(skin, input.normal));
        output.tangentWS = HikariTransformSurfaceTangent(
            float4(mul(skin, input.tangent.xyz), input.tangent.w), (float3x3)world);
    }
    return output;
}
