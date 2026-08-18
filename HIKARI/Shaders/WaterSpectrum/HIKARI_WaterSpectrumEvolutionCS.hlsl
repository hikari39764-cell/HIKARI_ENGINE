#include "WaterSpectrum/HIKARI_WaterSpectrumCommon.hlsli"

cbuffer WaterSpectrumEvolutionParams : register(b0)
{
    float gTimeSeconds;
    uint gResolution;
    float gDeltaK;
    float gLengthScale;
    float gChoppiness;
    float gUnused0;
    uint gUnused1;
    uint gUnused2;
};

StructuredBuffer<float4> gInitialSpectrum : register(t0);
StructuredBuffer<float4> gWaveData : register(t1);
RWTexture2D<float4> gField0 : register(u0);
RWTexture2D<float4> gField1 : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const uint index = dispatchThreadId.y * gResolution + dispatchThreadId.x;
    const float4 initial = gInitialSpectrum[index];
    const float4 wave = gWaveData[index];
    const float phase = wave.w * gTimeSeconds;
    const float2 exponent = float2(cos(phase), sin(phase));
    const float2 h = HikariComplexMultiply(initial.xy, exponent) +
        HikariComplexMultiply(initial.zw, float2(exponent.x, -exponent.y));
    const float2 ih = float2(-h.y, h.x);

    // Four complex fields travel through the same Stockham passes. The
    // explicit packing is the stable CPU/GPU contract for the resolver:
    // field0.xy = horizontal-X displacement, field0.zw = height,
    // field1.xy = horizontal-Z displacement, field1.zw = dHeight/dX.
    const float2 displacementX = ih * wave.x * wave.y * gChoppiness;
    const float2 displacementZ = ih * wave.z * wave.y * gChoppiness;
    const float2 slopeX = ih * wave.x;

    gField0[dispatchThreadId.xy] = float4(displacementX, h);
    gField1[dispatchThreadId.xy] = float4(displacementZ, slopeX);
}
