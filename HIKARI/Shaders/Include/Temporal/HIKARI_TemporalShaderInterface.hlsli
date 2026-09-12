#ifndef HIKARI_TEMPORAL_SHADER_INTERFACE
#define HIKARI_TEMPORAL_SHADER_INTERFACE

// 既存の Temporal パスの定数とレジスタを維持する。

#if defined(HIKARI_TEMPORAL_GEOMETRY)
#define MAX_JOINTS 128

cbuffer TemporalGeometryCB : register(b0)
{
    float4x4 gRenderViewProj;
    float4x4 gCurrentViewProj;
    float4x4 gPreviousViewProj;
    float4x4 gCurrentWorld;
    float4x4 gPreviousWorld;
    float4 gScreenParams;
    float4 gTemporalParams; // x: history valid
};

cbuffer TemporalSkinCB : register(b1)
{
    float4x4 gCurrentJoints[MAX_JOINTS];
    float4x4 gPreviousJoints[MAX_JOINTS];
};
#endif

#if defined(HIKARI_TEMPORAL_MASKS)
Texture2D<float4> gCompositionBase : register(t0);
Texture2D<float4> gCompositedColor : register(t1);
#endif

#if defined(HIKARI_TEMPORAL_MOTIONVECTOR)
cbuffer TemporalMotionVectorCB : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gViewProj;
    float4x4 gPrevViewProj;
    float4 gScreenParams;  // xy: render size, zw: inverse render size
    float4 gHistoryParams; // x: history valid, y: depth valid, z: far plane
    float4 gCameraPos;
    float4 gPrevCameraPos;
};

Texture2D<float> gSceneDepth : register(t0);
#endif

#if defined(HIKARI_TEMPORAL_TAARESOLVE)
cbuffer TemporalTaaCB : register(b0)
{
    float4 gScreenParams; // xy: render size, zw: inverse render size
    float4 gTaaParams;    // x: history valid, y: history weight, z: exposure valid, w: clip gamma
    float4 gRejectParams; // x: depth threshold, y: luma threshold, z: sharpness, w: debug mode
};

Texture2D<float4> gCurrentColor : register(t0);
Texture2D<float4> gHistoryColor : register(t1);
Texture2D<float2> gMotionVectors : register(t2);
Texture2D<float2> gMotionMetadata : register(t3);
Texture2D<float> gSceneDepth : register(t4);
Texture2D<float> gHistoryDepth : register(t5);
Texture2D<float> gReactiveMask : register(t6);
Texture2D<float> gTransparencyMask : register(t7);
Texture2D<float> gInvalidDepthMotionMask : register(t8);
Texture2D<float> gExposure : register(t9);
SamplerState gLinearClamp : register(s0);
#endif

#endif
