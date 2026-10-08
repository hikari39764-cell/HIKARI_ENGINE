#ifndef HIKARI_NOISE_INCLUDED
#define HIKARI_NOISE_INCLUDED

// Shared deterministic periodic spatial noise for participating media.
uint HikariNoiseMixUint(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

uint HikariNoiseHashInt3(int3 cell, uint salt)
{
    uint x = (uint)cell.x * 0x9e3779b9u;
    uint y = (uint)cell.y * 0x85ebca6bu;
    uint z = (uint)cell.z * 0xc2b2ae35u;
    return HikariNoiseMixUint(x ^ y ^ z ^ salt);
}

uint HikariNoiseHashInt2(int2 cell, uint salt)
{
    uint x = (uint)cell.x * 0x9e3779b9u;
    uint y = (uint)cell.y * 0x85ebca6bu;
    return HikariNoiseMixUint(x ^ y ^ salt);
}

float HikariNoiseHashUnit(uint value)
{
    return (float)(value & 0x00ffffffu) / 16777216.0f;
}

int HikariNoiseWrapInt(int value, int period)
{
    int wrapped = value % max(period, 1);
    return wrapped < 0 ? wrapped + max(period, 1) : wrapped;
}

int2 HikariNoiseWrapCell2(int2 cell, int2 period)
{
    return int2(
        HikariNoiseWrapInt(cell.x, period.x),
        HikariNoiseWrapInt(cell.y, period.y));
}

int3 HikariNoiseWrapCell3(int3 cell, int3 period)
{
    return int3(
        HikariNoiseWrapInt(cell.x, period.x),
        HikariNoiseWrapInt(cell.y, period.y),
        HikariNoiseWrapInt(cell.z, period.z));
}

float HikariNoiseFade(float value)
{
    value = saturate(value);
    return value * value * value *
        (value * (value * 6.0f - 15.0f) + 10.0f);
}

float HikariNoisePeriodicValue2D(
    float2 position,
    int2 period,
    uint salt)
{
    int2 cell = int2(floor(position));
    float2 local = frac(position);
    float2 fade = float2(
        HikariNoiseFade(local.x),
        HikariNoiseFade(local.y));

    float n00 = HikariNoiseHashUnit(
        HikariNoiseHashInt2(
            HikariNoiseWrapCell2(cell, period), salt));
    float n10 = HikariNoiseHashUnit(
        HikariNoiseHashInt2(
            HikariNoiseWrapCell2(cell + int2(1, 0), period), salt));
    float n01 = HikariNoiseHashUnit(
        HikariNoiseHashInt2(
            HikariNoiseWrapCell2(cell + int2(0, 1), period), salt));
    float n11 = HikariNoiseHashUnit(
        HikariNoiseHashInt2(
            HikariNoiseWrapCell2(cell + int2(1, 1), period), salt));
    return lerp(
        lerp(n00, n10, fade.x),
        lerp(n01, n11, fade.x),
        fade.y);
}

float HikariNoisePeriodicValue3D(
    float3 position,
    int3 period,
    uint salt)
{
    int3 cell = int3(floor(position));
    float3 local = frac(position);
    float3 fade = float3(
        HikariNoiseFade(local.x),
        HikariNoiseFade(local.y),
        HikariNoiseFade(local.z));

    float value = 0.0f;
    [unroll]
    for (int z = 0; z <= 1; ++z)
    {
        [unroll]
        for (int y = 0; y <= 1; ++y)
        {
            [unroll]
            for (int x = 0; x <= 1; ++x)
            {
                int3 corner = cell + int3(x, y, z);
                float cornerValue = HikariNoiseHashUnit(
                    HikariNoiseHashInt3(
                        HikariNoiseWrapCell3(corner, period),
                        salt));
                float weightX = x == 0 ? 1.0f - fade.x : fade.x;
                float weightY = y == 0 ? 1.0f - fade.y : fade.y;
                float weightZ = z == 0 ? 1.0f - fade.z : fade.z;
                value += cornerValue * weightX * weightY * weightZ;
            }
        }
    }
    return saturate(value);
}

// 周期的な勾配ノイズ（Perlin）。値はおよそ [-1, 1]。
float HikariNoisePeriodicGradient3D(
    float3 position,
    int3 period,
    uint salt)
{
    int3 cell = int3(floor(position));
    float3 local = frac(position);
    float3 fade = float3(
        HikariNoiseFade(local.x),
        HikariNoiseFade(local.y),
        HikariNoiseFade(local.z));

    float value = 0.0f;
    [unroll]
    for (int z = 0; z <= 1; ++z)
    {
        [unroll]
        for (int y = 0; y <= 1; ++y)
        {
            [unroll]
            for (int x = 0; x <= 1; ++x)
            {
                int3 corner = int3(x, y, z);
                uint hash = HikariNoiseHashInt3(
                    HikariNoiseWrapCell3(cell + corner, period),
                    salt);
                float3 gradient = float3(
                    HikariNoiseHashUnit(hash),
                    HikariNoiseHashUnit(HikariNoiseMixUint(hash ^ 0x3c6ef372u)),
                    HikariNoiseHashUnit(HikariNoiseMixUint(hash ^ 0xa54ff53au))) * 2.0f - 1.0f;
                gradient *= rsqrt(max(dot(gradient, gradient), 1.0e-4f));
                float weightX = x == 0 ? 1.0f - fade.x : fade.x;
                float weightY = y == 0 ? 1.0f - fade.y : fade.y;
                float weightZ = z == 0 ? 1.0f - fade.z : fade.z;
                value += dot(gradient, local - float3(corner)) * weightX * weightY * weightZ;
            }
        }
    }
    return clamp(value * 1.4f, -1.0f, 1.0f);
}

float HikariNoisePeriodicCellular2D(
    float2 position,
    int2 period,
    uint salt)
{
    int2 baseCell = int2(floor(position));
    float2 local = frac(position);
    float nearestSquared = 16.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 cell = HikariNoiseWrapCell2(
                baseCell + int2(x, y),
                period);
            uint hash = HikariNoiseHashInt2(cell, salt);
            float2 feature = float2(
                HikariNoiseHashUnit(hash),
                HikariNoiseHashUnit(
                    HikariNoiseMixUint(hash ^ 0xa511e9b3u)));
            float2 delta = float2(x, y) + feature - local;
            nearestSquared = min(nearestSquared, dot(delta, delta));
        }
    }

    return saturate(1.0f - sqrt(nearestSquared) * 0.72f);
}

float HikariNoisePeriodicCellular3D(
    float3 position,
    int3 period,
    uint salt)
{
    int3 baseCell = int3(floor(position));
    float3 local = frac(position);
    float nearestSquared = 27.0f;

    [unroll]
    for (int z = -1; z <= 1; ++z)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                int3 cell = HikariNoiseWrapCell3(
                    baseCell + int3(x, y, z),
                    period);
                uint hash = HikariNoiseHashInt3(cell, salt);
                float3 feature = float3(
                    HikariNoiseHashUnit(hash),
                    HikariNoiseHashUnit(
                        HikariNoiseMixUint(hash ^ 0x68bc21ebu)),
                    HikariNoiseHashUnit(
                        HikariNoiseMixUint(hash ^ 0x02e5be93u)));
                float3 delta = float3(x, y, z) + feature - local;
                nearestSquared = min(nearestSquared, dot(delta, delta));
            }
        }
    }

    return saturate(1.0f - sqrt(nearestSquared) * 0.58f);
}

#endif
