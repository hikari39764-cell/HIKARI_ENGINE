
#define gFxUser0 gFxUser[0]
#define gFxUser1 gFxUser[1]
#define gFxUser2 gFxUser[2]
#define gFxUser3 gFxUser[3]
#define gFxUser4 gFxUser[4]
#define gFxUser5 gFxUser[5]
#define gFxUser6 gFxUser[6]
#define gFxUser7 gFxUser[7]

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gScreenParams;
};

#include "Include/HIKARI_MeshObjectData.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"

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
};

VSOutput main(VSInput input)
{
    VSOutput output;
    uint objectDataIndex = HikariGetObjectDataAbsoluteIndex(gObjectDataIndex, input.instanceId);
    HikariMeshObjectData objectData =
        HikariGetMeshObjectDataForInstance(objectDataIndex, input.instanceId);
    float4 worldPos = mul(objectData.world, float4(input.position, 1.0f));
    output.position = mul(gViewProj, worldPos);
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(mul((float3x3)objectData.normalMatrix, input.normal));
    output.tangentWS = float4(normalize(mul((float3x3)objectData.normalMatrix, input.tangent.xyz)), input.tangent.w);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    output.materialDataIndex = objectData.materialDataIndex;
    output.forwardMeta = HikariPackForwardVertexMeta(
        objectData.receiveShadow, 0u, 0u, 0u);
    output.objectDataIndex = objectDataIndex;
    output.surfaceGpuSceneIndex = HikariGetSurfaceGpuSceneAbsoluteIndex(input.instanceId);
    output.debugSurfaceId = 0u;
    return output;
}
