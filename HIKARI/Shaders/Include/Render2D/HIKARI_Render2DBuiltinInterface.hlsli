#ifndef HIKARI_RENDER2D_BUILTIN_INTERFACE_HLSLI
#define HIKARI_RENDER2D_BUILTIN_INTERFACE_HLSLI

cbuffer HikariRender2DGlobal : register(b0)
{
    float2 gHikariRender2DScreenSize;
    float2 gHikariRender2DGlobalPadding;
};

cbuffer HikariRender2DMask : register(b1)
{
    float2 gHikariRender2DMaskCenter;
    float gHikariRender2DMaskRadius;
    float gHikariRender2DMaskSoftness;
};

Texture2D<float4> gHikariRender2DTexture : register(t0);
SamplerState gHikariRender2DSampler : register(s0);

#endif
