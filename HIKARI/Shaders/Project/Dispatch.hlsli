#ifndef HIKARI_PROJECT_DISPATCH_HLSLI
#define HIKARI_PROJECT_DISPATCH_HLSLI

#define HIKARI_PROJECT_DISPATCH_THREADS_X 8
#define HIKARI_PROJECT_DISPATCH_THREADS_Y 8
#define HIKARI_PROJECT_DISPATCH_THREADS_Z 1

cbuffer HikariProjectDispatchParams : register(b0)
{
    uint gHikariDispatchWidth;
    uint gHikariDispatchHeight;
    uint gHikariDispatchFrameIndex;
    uint gHikariDispatchReserved;
};

Texture2D<float4> gHikariDispatchInput : register(t0);
RWTexture2D<float4> gHikariDispatchOutput : register(u0);

bool HikariProjectDispatchInBounds(uint2 pixel)
{
    return pixel.x < gHikariDispatchWidth &&
        pixel.y < gHikariDispatchHeight;
}

#endif
