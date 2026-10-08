#include "Include/Sky/HIKARI_VolumetricCloudNoise.hlsli"

// 体積雲のノイズを一度だけ作る。生の値（HIKARI_VolumetricCloudNoise.hlsli）は平均の周りに狭く集まるので、GPU で
// 1024 段のヒストグラムを取り、累積分布で一様分布へ写す。こうすると閾値 1 - c を超える割合がちょうど c になり、
// 天気の被覆率がそのまま雲の量になる（分位表を手で測り直さずにノイズを変えられる）。
//   形状 128^3: 雲の塊
//   細部 32^3:  縁を侵食する細かい凹凸
//   天気 1024^2: r 雲域の揺らぎ、g 雲型の揺らぎ、b 対流セル、a 雲頂の塔
// 手順: ClearCloudHistogramCS → Histogram*CS（生の値を数える）→ ScanCloudHistogramCS（累積）→ Generate*CS（生の値を
// 作り直して写す）。形状と細部はその後 DownsampleCloudVolumeCS で 1 段ずつ箱フィルタの mip を作る。

cbuffer HikariCloudNoiseCB : register(b0)
{
    uint3 gCloudNoiseSize;
    uint gCloudNoisePadding;
};

RWTexture3D<unorm float> gCloudNoiseVolume : register(u0);
RWTexture2D<unorm float4> gCloudNoiseWeather : register(u1);
// [channel * 1024 + bin] に度数、HIKARI_CLOUD_NOISE_CDF_OFFSET + channel * 1025 + bin に bin の下端の累積分布
// （float を asuint で）、末尾は 1。
RWStructuredBuffer<uint> gCloudNoiseHistogram : register(u2);
Texture3D<float> gCloudNoiseSource : register(t0);

static const uint HIKARI_CLOUD_NOISE_BINS = 1024u;
static const uint HIKARI_CLOUD_NOISE_CHANNELS = 4u;
static const uint HIKARI_CLOUD_NOISE_CDF_OFFSET = HIKARI_CLOUD_NOISE_BINS * HIKARI_CLOUD_NOISE_CHANNELS;
static const uint HIKARI_CLOUD_NOISE_BUFFER_SIZE =
    HIKARI_CLOUD_NOISE_CDF_OFFSET + HIKARI_CLOUD_NOISE_CHANNELS * (HIKARI_CLOUD_NOISE_BINS + 1u);

void HikariCloudNoiseCount(uint channel, float value)
{
    const uint bin = min(uint(saturate(value) * float(HIKARI_CLOUD_NOISE_BINS)), HIKARI_CLOUD_NOISE_BINS - 1u);
    InterlockedAdd(gCloudNoiseHistogram[channel * HIKARI_CLOUD_NOISE_BINS + bin], 1u);
}

// 累積分布を bin の中で線形に補間する（段差を出さない）。
float HikariCloudNoiseEqualize(uint channel, float value)
{
    const float scaled = saturate(value) * float(HIKARI_CLOUD_NOISE_BINS);
    const uint bin = min(uint(scaled), HIKARI_CLOUD_NOISE_BINS - 1u);
    const uint base = HIKARI_CLOUD_NOISE_CDF_OFFSET + channel * (HIKARI_CLOUD_NOISE_BINS + 1u);
    return lerp(asfloat(gCloudNoiseHistogram[base + bin]), asfloat(gCloudNoiseHistogram[base + bin + 1u]),
        scaled - float(bin));
}

float3 HikariCloudVoxelUv(uint3 voxel)
{
    return (float3(voxel) + 0.5f) / float3(gCloudNoiseSize);
}

float2 HikariCloudTexelUv(uint2 texel)
{
    return (float2(texel) + 0.5f) / float2(gCloudNoiseSize.xy);
}

[numthreads(256, 1, 1)]
void ClearCloudHistogramCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x < HIKARI_CLOUD_NOISE_BUFFER_SIZE)
    {
        gCloudNoiseHistogram[id.x] = 0u;
    }
}

[numthreads(8, 8, 1)]
void HistogramCloudShapeCS(uint3 voxel : SV_DispatchThreadID)
{
    if (any(voxel >= gCloudNoiseSize))
    {
        return;
    }
    HikariCloudNoiseCount(0u, HikariCloudShapeField(HikariCloudVoxelUv(voxel)));
}

[numthreads(8, 8, 1)]
void HistogramCloudDetailCS(uint3 voxel : SV_DispatchThreadID)
{
    if (any(voxel >= gCloudNoiseSize))
    {
        return;
    }
    HikariCloudNoiseCount(0u, HikariCloudDetailField(HikariCloudVoxelUv(voxel)));
}

[numthreads(8, 8, 1)]
void HistogramCloudWeatherCS(uint3 texel : SV_DispatchThreadID)
{
    if (any(texel.xy >= gCloudNoiseSize.xy))
    {
        return;
    }
    const float4 weather = HikariCloudWeatherField(HikariCloudTexelUv(texel.xy));
    [unroll]
    for (uint channel = 0u; channel < HIKARI_CLOUD_NOISE_CHANNELS; ++channel)
    {
        HikariCloudNoiseCount(channel, weather[channel]);
    }
}

// 1 グループが 1 チャンネルの 1024 段を累積する。
groupshared uint sCloudNoiseScan[HIKARI_CLOUD_NOISE_BINS];

[numthreads(1024, 1, 1)]
void ScanCloudHistogramCS(uint3 groupId : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint channel = groupId.x;
    const uint count = gCloudNoiseHistogram[channel * HIKARI_CLOUD_NOISE_BINS + lane];
    sCloudNoiseScan[lane] = count;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint offset = 1u; offset < HIKARI_CLOUD_NOISE_BINS; offset <<= 1u)
    {
        const uint value = lane >= offset ? sCloudNoiseScan[lane - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        sCloudNoiseScan[lane] += value;
        GroupMemoryBarrierWithGroupSync();
    }
    const float total = max(float(sCloudNoiseScan[HIKARI_CLOUD_NOISE_BINS - 1u]), 1.0f);
    const uint base = HIKARI_CLOUD_NOISE_CDF_OFFSET + channel * (HIKARI_CLOUD_NOISE_BINS + 1u);
    gCloudNoiseHistogram[base + lane] = asuint(float(sCloudNoiseScan[lane] - count) / total);
    if (lane == HIKARI_CLOUD_NOISE_BINS - 1u)
    {
        gCloudNoiseHistogram[base + HIKARI_CLOUD_NOISE_BINS] = asuint(1.0f);
    }
}

[numthreads(8, 8, 1)]
void GenerateCloudShapeCS(uint3 voxel : SV_DispatchThreadID)
{
    if (any(voxel >= gCloudNoiseSize))
    {
        return;
    }
    gCloudNoiseVolume[voxel] = HikariCloudNoiseEqualize(0u, HikariCloudShapeField(HikariCloudVoxelUv(voxel)));
}

[numthreads(8, 8, 1)]
void GenerateCloudDetailCS(uint3 voxel : SV_DispatchThreadID)
{
    if (any(voxel >= gCloudNoiseSize))
    {
        return;
    }
    gCloudNoiseVolume[voxel] = HikariCloudNoiseEqualize(0u, HikariCloudDetailField(HikariCloudVoxelUv(voxel)));
}

[numthreads(8, 8, 1)]
void GenerateCloudWeatherCS(uint3 texel : SV_DispatchThreadID)
{
    if (any(texel.xy >= gCloudNoiseSize.xy))
    {
        return;
    }
    const float4 weather = HikariCloudWeatherField(HikariCloudTexelUv(texel.xy));
    gCloudNoiseWeather[texel.xy] = float4(HikariCloudNoiseEqualize(0u, weather.x),
        HikariCloudNoiseEqualize(1u, weather.y), HikariCloudNoiseEqualize(2u, weather.z),
        HikariCloudNoiseEqualize(3u, weather.w));
}

// 1 つ細かい段の 2x2x2 の平均（gCloudNoiseSize はこの段の大きさ）。
[numthreads(8, 8, 1)]
void DownsampleCloudVolumeCS(uint3 voxel : SV_DispatchThreadID)
{
    if (any(voxel >= gCloudNoiseSize))
    {
        return;
    }
    float sum = 0.0f;
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        sum += gCloudNoiseSource.Load(int4(voxel * 2u + uint3(corner & 1u, (corner >> 1u) & 1u, corner >> 2u), 0));
    }
    gCloudNoiseVolume[voxel] = sum * 0.125f;
}
