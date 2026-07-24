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

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4x4 gNormalMatrix;
    float4 gBaseColor;
    uint gHasBaseColorTexture;
    uint gFxFlags;
    uint gMaterialFlags;
    float gAlphaCutoff;
    float4 gEmissiveFactor;
    uint gHasNormalTexture;
    float gNormalScale;
    float2 gNormalPadding;
    uint gReceiveShadow;
    float3 gShadowObjectPadding;
    float4 gFxUser[8];

};

#define MAX_JOINTS 128

cbuffer JointPaletteCB : register(b3)
{
    float4x4 gJointMatrices[MAX_JOINTS];
};

cbuffer MaterialIndexCB : register(b7)
{
    uint gMaterialDataIndex;
    uint3 gMaterialDataPadding;
};

#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"

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

    float3 localNormal =
        mul((float3x3)ResolveJointMatrix(input.joints.x), input.normal) * input.weights.x +
        mul((float3x3)ResolveJointMatrix(input.joints.y), input.normal) * input.weights.y +
        mul((float3x3)ResolveJointMatrix(input.joints.z), input.normal) * input.weights.z +
        mul((float3x3)ResolveJointMatrix(input.joints.w), input.normal) * input.weights.w;

    float3 localTangent =
        mul((float3x3)ResolveJointMatrix(input.joints.x), input.tangent.xyz) * input.weights.x +
        mul((float3x3)ResolveJointMatrix(input.joints.y), input.tangent.xyz) * input.weights.y +
        mul((float3x3)ResolveJointMatrix(input.joints.z), input.tangent.xyz) * input.weights.z +
        mul((float3x3)ResolveJointMatrix(input.joints.w), input.tangent.xyz) * input.weights.w;

    float4x4 world = gWorld;
    float4x4 normalMatrix = gNormalMatrix;
    uint materialDataIndex = gMaterialDataIndex;
    uint receiveShadow = gReceiveShadow;
    uint surfaceGpuSceneIndex = 0u;
    uint debugSurfaceId = 0u;
    uint debugDrawBucket = 0u;
    if (gUseSurfaceGpuScene != 0u)
    {
        surfaceGpuSceneIndex = HikariGetSurfaceGpuSceneAbsoluteIndex(input.instanceId);
        HikariSurfaceGpuSceneInstance instance =
            HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex);
        world = instance.world;
        normalMatrix = instance.normalMatrix;
        materialDataIndex = instance.materialDataIndex;
        receiveShadow =
            (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_RECEIVE_SHADOW) != 0u
                ? 1u
                : 0u;
        debugSurfaceId = instance.sourceSurfaceInstanceIndex;
        debugDrawBucket = instance.geometryBackend;
    }

    float4 worldPos = mul(world, float4(localPos.xyz, 1.0f));
    output.position = mul(gViewProj, worldPos);
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(mul((float3x3)normalMatrix, normalize(localNormal)));
    output.tangentWS = float4(normalize(mul((float3x3)normalMatrix, normalize(localTangent))), input.tangent.w);
    output.uv = input.uv0;
    output.uv1 = input.uv1;
    output.materialDataIndex = materialDataIndex;
    output.forwardMeta = HikariPackForwardVertexMeta(
        receiveShadow, debugDrawBucket, 0u, 0u);
    output.objectDataIndex = 0u;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.debugSurfaceId = debugSurfaceId;
    return output;
}
