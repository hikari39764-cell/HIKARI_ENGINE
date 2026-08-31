#ifndef HIKARI_AUTOMATIC_EXPOSURE_INTERFACE_HLSLI
#define HIKARI_AUTOMATIC_EXPOSURE_INTERFACE_HLSLI

#define HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT 64

cbuffer MeteringConstants : register(b0)
{
    uint gWidth;
    uint gHeight;
    uint2 gPadding;
};

Texture2D<float4> gSceneColor : register(t0);
RWStructuredBuffer<float4> gMeasurement : register(u0);

#endif
