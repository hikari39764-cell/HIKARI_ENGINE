#include "WaterSpectrum/HIKARI_WaterSpectrumCommon.hlsli"

cbuffer WaterSpectrumStockhamParams : register(b0)
{
    uint gAxis;
    uint gStage;
    uint gResolution;
    uint gFinalize;
};

StructuredBuffer<float4> gTwiddleTable : register(t0);
Texture2D<float4> gInput0 : register(t1);
Texture2D<float4> gInput1 : register(t2);
RWTexture2D<float4> gOutput0 : register(u0);
RWTexture2D<float4> gOutput1 : register(u1);

float4 HikariButterfly(float4 a, float4 b, float2 twiddle)
{
    return float4(
        HikariComplexMultiply(twiddle, b.xy) + a.xy,
        HikariComplexMultiply(twiddle, b.zw) + a.zw);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const uint transformIndex = gAxis == 0u
        ? dispatchThreadId.x
        : dispatchThreadId.y;
    const uint twiddleIndex = gStage * gResolution + transformIndex;
    const float4 twiddleData = gTwiddleTable[twiddleIndex];
    const int first = (int)round(twiddleData.z);
    const int second = (int)round(twiddleData.w);
    int2 coord0 = (int2)dispatchThreadId.xy;
    int2 coord1 = coord0;
    if (gAxis == 0u)
    {
        coord0.x = first;
        coord1.x = second;
    }
    else
    {
        coord0.y = first;
        coord1.y = second;
    }

    const float2 inverseTwiddle = float2(twiddleData.x, -twiddleData.y);
    float4 value0 = HikariButterfly(
        gInput0[coord0], gInput0[coord1], inverseTwiddle);
    float4 value1 = HikariButterfly(
        gInput1[coord0], gInput1[coord1], inverseTwiddle);
    if (gFinalize != 0u)
    {
        const float checker = 1.0 - 2.0 * ((dispatchThreadId.x + dispatchThreadId.y) & 1u);
        value0 *= checker;
        value1 *= checker;
    }

    gOutput0[dispatchThreadId.xy] = value0;
    gOutput1[dispatchThreadId.xy] = value1;
}
