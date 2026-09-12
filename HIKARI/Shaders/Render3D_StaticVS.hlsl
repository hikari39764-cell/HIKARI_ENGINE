
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Surface/HIKARI_SurfaceTangentFrame.hlsli"

#define gFxUser0 gFxUser[0]
#define gFxUser1 gFxUser[1]
#define gFxUser2 gFxUser[2]
#define gFxUser3 gFxUser[3]
#define gFxUser4 gFxUser[4]
#define gFxUser5 gFxUser[5]
#define gFxUser6 gFxUser[6]
#define gFxUser7 gFxUser[7]

#include "Include/Render3D/HIKARI_MeshCameraData.hlsli"

#include "Include/HIKARI_MeshObjectData.hlsli"
#include "Include/Meshlet/HIKARI_WaterSurfaceDeform.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"
#include "Include/Surface/HIKARI_SandSurfaceDeform.hlsli"
#include "Include/Surface/HIKARI_FoliageWind.hlsli"
#if defined(HIKARI_GEOMETRY_OUTLINE_VERTEX)
#include "Include/Features/HIKARI_GeometryOutlineVertex.hlsli"
#endif

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
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD10;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    nointerpolation uint forwardMeta : TEXCOORD3;
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    nointerpolation uint debugSurfaceId : TEXCOORD6;
    float3 characterRightWS : TEXCOORD8;
    float3 characterForwardWS : TEXCOORD9;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD7;
    // 水面と沙地が共有する surface feature 出力。TEXCOORD11 を一つに
    // 統合し、フラグに応じてどちらかの変形関数が値を書き込む。
    float3 surfaceFeatureData : TEXCOORD11;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    uint objectDataIndex = HikariGetObjectDataAbsoluteIndex(gObjectDataIndex, input.instanceId);
    uint surfaceGpuSceneIndex =
        HikariGetSurfaceGpuSceneAbsoluteIndex(input.instanceId);
    uint surfaceMaterialProgramFlags = 0u;
    HikariSurfaceGpuSceneInstance surfaceInstance =
        (HikariSurfaceGpuSceneInstance)0;
    if (gUseSurfaceGpuScene != 0u)
    {
        surfaceInstance = HikariGetSurfaceGpuSceneInstanceAt(
            surfaceGpuSceneIndex);
        surfaceMaterialProgramFlags =
            surfaceInstance.materialProgramFlags;
    }
    HikariMeshObjectData objectData =
    HikariGetMeshObjectDataForInstance(objectDataIndex, input.instanceId);
    float3 localPosition = input.position;
    float3 localNormal = input.normal;
    output.surfaceFeatureData = float3(localPosition.xz, 0.0f);
    if (gUseSurfaceGpuScene != 0u)
    {
        HikariApplyWaterSurfaceDeformWithWorld(
            surfaceInstance,
            gSimulationTimeParams.x,
            input.uv1,
            objectData.world,
            localPosition,
            localNormal,
            output.surfaceFeatureData);
    }
    HikariApplySandSurfaceDeform(
        surfaceInstance,
        localPosition,
        localNormal,
        output.surfaceFeatureData);
    float4 worldPos = mul(objectData.world, float4(localPosition, 1.0f));
    worldPos = HikariApplyFoliageWindWorldPosition(
        surfaceInstance,
        gTimeParams.x,
        input.uv,
        worldPos);
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(
        mul((float3x3)objectData.normalMatrix, localNormal));
#if defined(HIKARI_GEOMETRY_OUTLINE_VERTEX)
    output.position = HikariExpandGeometryOutlineVertex(
        worldPos,
        output.normalWS,
        objectData.materialDataIndex,
        surfaceMaterialProgramFlags);
#else
    output.position = mul(gViewProj, worldPos);
#endif
    output.tangentWS = HikariTransformSurfaceTangent(
        input.tangent, (float3x3)objectData.world);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    output.materialDataIndex = objectData.materialDataIndex;
    output.forwardMeta = HikariPackForwardVertexMeta(
        objectData.receiveShadow, 0u, 0u, 0u);
    output.objectDataIndex = objectDataIndex;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.debugSurfaceId = 0u;
    output.characterRightWS = normalize(
        mul((float3x3)objectData.world, float3(1.0f, 0.0f, 0.0f)));
    output.characterForwardWS = normalize(
        mul((float3x3)objectData.world, float3(0.0f, 0.0f, 1.0f)));
    output.surfaceFeatureFlags =
        gUseSurfaceGpuScene != 0u ? surfaceInstance.flags : 0u;
    return output;
}
