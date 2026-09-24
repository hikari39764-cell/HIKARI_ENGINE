#ifndef HIKARI_WATER_SPECTRUM_COMMON_HLSLI
#define HIKARI_WATER_SPECTRUM_COMMON_HLSLI

static const float HIKARI_WATER_GRAVITY = 9.81;
static const float HIKARI_WATER_EPSILON = 1.0e-6;

float HikariWaterSpectrumFiniteOr(float value, float fallback)
{
    return isfinite(value) ? value : fallback;
}

float2 HikariWaterSpectrumFiniteOr(float2 value, float2 fallback)
{
    return all(isfinite(value)) ? value : fallback;
}

float3 HikariWaterSpectrumFiniteOr(float3 value, float3 fallback)
{
    return all(isfinite(value)) ? value : fallback;
}

float4 HikariWaterSpectrumFiniteOr(float4 value, float4 fallback)
{
    return all(isfinite(value)) ? value : fallback;
}

float2 HikariComplexMultiply(float2 a, float2 b)
{
    return float2(
        a.x * b.x - a.y * b.y,
        a.x * b.y + a.y * b.x);
}

uint2 HikariWrapCoord(int2 coord, uint size)
{
    const int signedSize = (int)size;
    int2 wrapped = coord % signedSize;
    wrapped += (int2)(wrapped < 0) * signedSize;
    return (uint2)wrapped;
}

#endif
