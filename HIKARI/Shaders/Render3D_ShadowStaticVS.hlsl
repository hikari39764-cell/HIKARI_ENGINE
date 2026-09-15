#define HIKARI_MASS_GPU_SCENE_DISABLED 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Render3D/HIKARI_MeshCameraData.hlsli"


#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Surface/HIKARI_SandSurfaceDeform.hlsli"
#include "Include/Surface/HIKARI_FoliageWind.hlsli"
#include "Include/Surface/HIKARI_SurfaceTangentFrame.hlsli"


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
    float3 worldPosWS : TEXCOORD4;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    const HikariSurfaceGpuSceneInstance instance = HikariGetSurfaceGpuSceneInstance(input.instanceId);
    const float4x4 world = instance.world;
    const float3x3 normalMatrix = (float3x3)instance.normalMatrix;
    const uint surfaceGpuSceneIndex = HikariGetSurfaceGpuSceneAbsoluteIndex(input.instanceId);
    const uint surfaceFeatureFlags = instance.flags;
    const uint materialDataIndex = instance.materialDataIndex;
    const HikariMeshMaterialData materialData = HikariGetMeshMaterialData(materialDataIndex);
    const uint materialFlags = materialData.materialFlags;
    const float alphaCutoff = materialData.pbrParams.w;
    float3 localPosition = input.position;
    float3 localNormal = input.normal;
    float3 sandSurfaceData = 0.0f.xxx;
    HikariApplySandSurfaceDeform(instance, localPosition, localNormal, sandSurfaceData);

    float4 worldPos = mul(world, float4(localPosition, 1.0f));
    worldPos = HikariApplyFoliageWindWorldPosition(instance, gTimeParams.x, input.uv, worldPos);
    output.position = mul(gViewProj, worldPos);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    output.materialFlags = materialFlags;
    output.alphaCutoff = alphaCutoff;
    output.materialDataIndex = materialDataIndex;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.surfaceFeatureFlags = surfaceFeatureFlags;
    output.worldPosWS = worldPos.xyz;
    output.normalWS = mul(normalMatrix, localNormal);
    output.tangentWS = HikariTransformSurfaceTangent(input.tangent, (float3x3)world);
    return output;
}
