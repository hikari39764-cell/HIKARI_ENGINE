#ifndef HIKARI_VIEW_LIGHTING_DATA_INCLUDED
#define HIKARI_VIEW_LIGHTING_DATA_INCLUDED
#include "HIKARI_LocalLightContract.hlsli"
#include "Include/Color/HIKARI_ViewExposure.hlsli"
// Shared CPU LightCB/ShadowCB layout; no pass owns a second copy.
cbuffer LightCB : register(b2)
{
    float4 gDirectionalDir; // xyz: ray direction, w: sin^2(solar angular radius / 2)
    float4 gDirectionalColor;
    float4 gAmbientColor;
    float4 gViewExposure; // {P, 1/P, P/E, P/P_prev}
    float gDirectionalIntensity;
    float gAmbientIntensity;
    uint gLocalLightCount;
    uint gLocalLightClusterCountX;
    uint gLocalLightClusterCountY;
    uint gLocalLightClusterCountZ;
    uint gLocalLightTileSizePixels;
    uint gLocalLightClusterIndexCount;
    float gLocalLightNearDistance;
    float gLocalLightFarDistance;
    float gLocalLightDepthScale;
    float gLocalLightDepthBias;
    float4 gFogColorDensity;
    float4 gFogParams;
    uint gDebugView;
    float gForwardCostMode;
    float2 gDebugPadding;
    float4 gScreenSpaceReflectionParams0;
    float4 gScreenSpaceReflectionParams1;
    // 描画の様式。0: {明るい側の段数（0 で物理）, 影の境, 境のぼかし, 鏡面反射の強さ}、1: {影の側の環境光の色, 縁の光}。
    float4 gStyleShading0;
    float4 gStyleShading1;
};

// 絶対量の色をこのビューの HDR 目標の空間へ移す（alpha はそのまま）。
float4 HikariPreExposeView(float4 color)
{
    return float4(HikariPreExpose(color.rgb, gViewExposure), color.a);
}

cbuffer ShadowCB : register(b4)
{
    float4x4 gShadowLightViewProj[4];
    float4 gShadowCascadeSplits;
    float4 gShadowCascadeParams;
    float4 gShadowCascadeDepthBiasScales;
    uint gShadowEnabled;
    float gShadowDepthBias;
    float gShadowNormalBias;
    float gShadowStrength;
    uint gShadowPcfEnabled;
    float gShadowPcfRadius;
    float gShadowTexelSizeX;
    float gShadowTexelSizeY;
    float gShadowEdgeFade;
    float gShadowFrameIndex;    // 画素ごとの tap 回転と接触影の始点のずらし
    float gShadowLightTangent;  // PCSS：tan(太陽の角半径) × 半影の倍率。0 で固定 PCF
    float gShadowContactLength; // 接触影の長さ [m]。0 で無効
    float gShadowContactStrength;
    float gShadowContactThickness; // 遮る物の厚みの想定 [m]
    float2 gShadowContactPadding;
    HikariLocalShadowData gLocalShadow;
};


#endif
