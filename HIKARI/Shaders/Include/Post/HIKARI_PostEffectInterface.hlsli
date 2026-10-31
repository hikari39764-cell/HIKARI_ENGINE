#ifndef HIKARI_POST_EFFECT_INTERFACE_HLSLI
#define HIKARI_POST_EFFECT_INTERFACE_HLSLI

#include "HIKARI_ScreenReadability.hlsli"

cbuffer CommonParams : register(b0)
{
    float gTime;
    float gDeltaTime;
    float gCombo;
    float gIntensity;
    float gResolutionX;
    float gResolutionY;
    float2 gCommonPadding;
    float4 gUser[16];
    // 表示参照の色から、このビューの前露出済み HDR への変換。
    float gDisplayToScene;
    float gDisplayGamma;
    float2 gRasterJitterPixels;
};

struct HikariPostEffectInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

#endif
