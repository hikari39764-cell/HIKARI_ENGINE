#ifndef HIKARI_DEPTH_PYRAMID_INTERFACE_HLSLI
#define HIKARI_DEPTH_PYRAMID_INTERFACE_HLSLI

#define HIKARI_DEPTH_PYRAMID_THREAD_GROUP_SIZE 8

cbuffer DepthPyramidBuildCB : register(b0)
{
    uint2 gSourceSize;
    uint2 gDestSize;
};

Texture2D<float> gSourceDepth : register(t0);
RWTexture2D<float> gDestDepthPyramid : register(u0);

#endif
