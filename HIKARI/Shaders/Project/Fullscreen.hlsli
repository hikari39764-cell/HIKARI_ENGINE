#ifndef HIKARI_PROJECT_FULLSCREEN_HLSLI
#define HIKARI_PROJECT_FULLSCREEN_HLSLI

Texture2D<float4> HikariProjectInput : register(t0);
SamplerState HikariProjectLinearClamp : register(s0);

struct HikariProjectFullscreenVertex {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

#endif
