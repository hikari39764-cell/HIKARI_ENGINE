
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Surface/HIKARI_SurfaceTangentFrame.hlsli"
#include "Include/Surface/HIKARI_MaterialShell.hlsli"

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
#include "Include/Surface/HIKARI_TerrainSurfaceDeform.hlsli"
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

typedef HikariForwardVertexOutput VSOutput;

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
    HikariApplyTerrainSurfaceDeform(
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
    output.normalWS = normalize(
        mul((float3x3)objectData.normalMatrix, localNormal));
    output.shellLayer = HikariShellLayer(input.instanceId,
        gUseSurfaceGpuScene != 0u ? gSurfaceGpuSceneShellLayerCount : 0u,
        asfloat(gSurfaceGpuSceneShellLodBits));
    worldPos.xyz += output.normalWS * (surfaceInstance.shellSettings.y * output.shellLayer.x);
    output.worldPosWS = worldPos.xyz;
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
