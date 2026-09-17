#ifndef HIKARI_VIEW_LIGHTING_DATA_INCLUDED
#define HIKARI_VIEW_LIGHTING_DATA_INCLUDED
#include "HIKARI_LocalLightContract.hlsli"
// Shared CPU LightCB/ShadowCB layout; no pass owns a second copy.
cbuffer LightCB : register(b2)
{
    float4 gDirectionalDir; // xyz: ray direction, w: sin^2(solar angular radius / 2)
    float4 gDirectionalColor;
    float4 gAmbientColor;
    float4 gLightReserved;
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
};

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
    float3 gShadowPadding;
    HikariLocalShadowData gLocalShadow;
};


#endif
