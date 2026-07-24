#define G_ITERATIONS 30

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    // x = elapsed time, y = unscaled dt, z = game dt, w = frame index
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

float2 WaveDx(float2 position, float2 direction, float frequency, float timeShift)
{
    direction = normalize(direction);

    float phase = dot(direction, position) * frequency + timeShift;
    float wave = exp(cos(phase) - 1.0f);
    float dx = wave * cos(phase);

    return float2(wave, -dx);
}

float GetWaves(float2 position, float time, float drag, int iterations)
{
    float phaseOffset = length(position) * 0.1f;
    float directionSeed = 0.0f;
    float frequency = 1.0f;
    float timeMultiplier = 2.0f;
    float weight = 1.0f;

    float valueSum = 0.0f;
    float weightSum = 0.0f;

    [loop]
    for (int i = 0; i < iterations; ++i)
    {
        float2 dir = float2(sin(directionSeed), cos(directionSeed));
        float2 waveData = WaveDx(position, dir, frequency, time * timeMultiplier + phaseOffset);

        position += dir * waveData.y * weight * drag;

        valueSum += waveData.x * weight;
        weightSum += weight;

        weight = lerp(weight, 0.0f, 0.2f);
        frequency *= 1.18f;
        timeMultiplier *= 1.07f;
        directionSeed += 1232.399963f;
    }

    return valueSum / max(weightSum, 0.0001f);
}

float WaterHeight(float2 localXZ, float time, float waveSpeed, float waveHeight, float waveScale, float drag)
{
    float2 p = localXZ * waveScale;
    float h = GetWaves(p, time * waveSpeed, drag, G_ITERATIONS);
    return (h - 0.5f) * waveHeight;
}

float3 WaterNormalLocal(float2 localXZ, float time, float waveSpeed, float waveHeight, float waveScale, float drag)
{
    const float e = 0.08f;

    float h = WaterHeight(localXZ, time, waveSpeed, waveHeight, waveScale, drag);
    float hx = WaterHeight(localXZ + float2(e, 0.0f), time, waveSpeed, waveHeight, waveScale, drag);
    float hz = WaterHeight(localXZ + float2(0.0f, e), time, waveSpeed, waveHeight, waveScale, drag);

    return normalize(float3(h - hx, e, h - hz));
}

VSOutput main(VSInput input)
{
    uint objectDataIndex = HikariGetObjectDataAbsoluteIndex(gObjectDataIndex, input.instanceId);
    uint surfaceGpuSceneIndex = HikariGetSurfaceGpuSceneAbsoluteIndex(input.instanceId);
    HikariMeshObjectData objectData =
        HikariGetMeshObjectDataForSurfaceIndex(objectDataIndex, surfaceGpuSceneIndex);

    float waveSpeed = objectData.fxUser[0].x;
    float waveHeight = objectData.fxUser[0].y;
    float waveScale = max(objectData.fxUser[0].z, 0.001f);

    // gFxUser1.w is optional choppiness / drag.
    // If it is not set from JSON, fall back to the ShaderToy-like value.
    float drag = objectData.fxUser[1].w;
    if (drag <= 0.0001f)
    {
        drag = 0.38f;
    }

    float time = gTimeParams.x;

    float3 localPos = input.position;
    localPos.y += WaterHeight(localPos.xz, time, waveSpeed, waveHeight, waveScale, drag);

    float3 normalLocal = WaterNormalLocal(input.position.xz, time, waveSpeed, waveHeight, waveScale, drag);

    VSOutput output;
    float4 worldPos = mul(objectData.world, float4(localPos, 1.0f));

    output.position = mul(gViewProj, worldPos);
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(mul((float3x3)objectData.normalMatrix, normalLocal));
    output.tangentWS = float4(normalize(mul((float3x3)objectData.normalMatrix, input.tangent.xyz)), input.tangent.w);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    output.materialDataIndex = objectData.materialDataIndex;
    output.forwardMeta = HikariPackForwardVertexMeta(
        objectData.receiveShadow, 0u, 0u, 0u);
    output.objectDataIndex = objectDataIndex;
    output.surfaceGpuSceneIndex = surfaceGpuSceneIndex;
    output.debugSurfaceId = 0u;

    return output;
}
