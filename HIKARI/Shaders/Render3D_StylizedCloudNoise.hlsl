#include "Include/Sky/HIKARI_StylizedCloudVolume.hlsli"

// The CPU stage creates the output resources at the dimensions in the
// renderer handoff and dispatches one of these three entries.  The generated
// fields are periodic by construction, so texture wrap never introduces a
// seam at the authored volume boundary.

cbuffer HikariStylizedCloudNoiseCB : register(b0)
{
    uint3 gHikariStylizedCloudNoiseSize;
    uint gHikariStylizedCloudNoiseKind;
};

RWTexture3D<float4> gHikariStylizedCloudNoiseVolume : register(u0);
RWTexture2D<float4> gHikariStylizedCloudNoiseWeather : register(u1);

float4 HikariStylizedCloudBuildShapeVoxel(
    uint3 voxel,
    uint3 volumeSize)
{
    float3 uv = (float3(voxel) + 0.5f) /
        max(float3(volumeSize), 1.0f.xxx);
    float3 macroPosition = uv * 8.0f;
    float3 detailPosition = uv * 16.0f + 3.17f.xxx;
    float macroValue = HikariStylizedCloudPeriodicValue3D(
        macroPosition,
        int3(8, 8, 8),
        0x31a7c9edu);
    float secondaryValue = HikariStylizedCloudPeriodicValue3D(
        detailPosition,
        int3(16, 16, 16),
        0x6d2b79f5u);
    float cellular = HikariStylizedCloudPeriodicCellular3D(
        macroPosition,
        int3(8, 8, 8),
        0x9e3779b9u);
    float secondaryCellular = HikariStylizedCloudPeriodicCellular3D(
        detailPosition,
        int3(16, 16, 16),
        0x243f6a88u);

    return float4(
        saturate(macroValue * 0.74f + secondaryValue * 0.26f),
        saturate(cellular),
        saturate(secondaryValue * 0.68f + (1.0f - secondaryCellular) * 0.32f),
        1.0f);
}

float4 HikariStylizedCloudBuildDetailVoxel(
    uint3 voxel,
    uint3 volumeSize)
{
    float3 uv = (float3(voxel) + 0.5f) /
        max(float3(volumeSize), 1.0f.xxx);
    float3 primaryPosition = uv * 12.0f + 5.11f.xxx;
    float3 secondaryPosition = uv * 24.0f + 7.73f.xxx;
    float primaryValue = HikariStylizedCloudPeriodicValue3D(
        primaryPosition,
        int3(12, 12, 12),
        0x85ebca6bu);
    float secondaryValue = HikariStylizedCloudPeriodicValue3D(
        secondaryPosition,
        int3(24, 24, 24),
        0xc2b2ae35u);
    float cellular = HikariStylizedCloudPeriodicCellular3D(
        primaryPosition,
        int3(12, 12, 12),
        0x27d4eb2fu);
    float secondaryCellular = HikariStylizedCloudPeriodicCellular3D(
        secondaryPosition,
        int3(24, 24, 24),
        0x165667b1u);

    return float4(
        saturate(primaryValue * 0.62f + secondaryValue * 0.38f),
        saturate(cellular),
        saturate(secondaryValue * 0.58f + (1.0f - secondaryCellular) * 0.42f),
        1.0f);
}

float4 HikariStylizedCloudBuildWeatherTexel(
    uint2 texel,
    uint2 textureSize)
{
    float2 uv = (float2(texel) + 0.5f) /
        max(float2(textureSize), 1.0f.xx);
    float2 coveragePosition = uv * 32.0f;
    float2 typePosition = uv * 16.0f + 4.27f.xx;
    float coverage = HikariStylizedCloudPeriodicValue2D(
        coveragePosition,
        int2(32, 32),
        0x4f1bbcdcu);
    float coverageCellular = HikariStylizedCloudPeriodicCellular2D(
        coveragePosition,
        int2(32, 32),
        0x7f4a7c15u);
    float cloudType = HikariStylizedCloudPeriodicValue2D(
        typePosition,
        int2(16, 16),
        0x94d049bbu);
    float rain = HikariStylizedCloudPeriodicValue2D(
        uv * 8.0f + 9.31f.xx,
        int2(8, 8),
        0x369dea0fu);

    return float4(
        saturate(coverage * 0.72f + (1.0f - coverageCellular) * 0.28f),
        saturate(cloudType),
        saturate(rain * 0.82f + coverageCellular * 0.18f),
        1.0f);
}

[numthreads(8, 8, 1)]
void GenerateStylizedCloudShapeCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint3 volumeSize = max(gHikariStylizedCloudNoiseSize, uint3(1u, 1u, 1u));
    if (any(dispatchThreadId >= volumeSize))
    {
        return;
    }

    gHikariStylizedCloudNoiseVolume[dispatchThreadId] =
        HikariStylizedCloudBuildShapeVoxel(
            dispatchThreadId,
            volumeSize);
}

[numthreads(8, 8, 1)]
void GenerateStylizedCloudDetailCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint3 volumeSize = max(gHikariStylizedCloudNoiseSize, uint3(1u, 1u, 1u));
    if (any(dispatchThreadId >= volumeSize))
    {
        return;
    }

    gHikariStylizedCloudNoiseVolume[dispatchThreadId] =
        HikariStylizedCloudBuildDetailVoxel(
            dispatchThreadId,
            volumeSize);
}

[numthreads(8, 8, 1)]
void GenerateStylizedCloudWeatherCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 textureSize = max(
        gHikariStylizedCloudNoiseSize.xy,
        uint2(1u, 1u));
    if (any(dispatchThreadId.xy >= textureSize))
    {
        return;
    }

    gHikariStylizedCloudNoiseWeather[dispatchThreadId.xy] =
        HikariStylizedCloudBuildWeatherTexel(
            dispatchThreadId.xy,
            textureSize);
}
