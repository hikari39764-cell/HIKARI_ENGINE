#ifndef HIKARI_FORWARD_COMMON_INCLUDED
#define HIKARI_FORWARD_COMMON_INCLUDED

#include "Include/Contracts/HIKARI_ShaderResourceBindings.hlsli"
#include "Include/Lighting/HIKARI_LocalLightContract.hlsli"

#ifndef HIKARI_FORWARD_ENABLE_MATERIAL_FX
#define HIKARI_FORWARD_ENABLE_MATERIAL_FX 0
#endif

// IMPORTANT:
// This cbuffer/register layout must stay in sync with MeshRenderer root
// parameters and the CPU-side CameraCB / LightCB / ShadowCB / SkyEnvironmentCB.

#define gFxUser0 gFxUser[0]
#define gFxUser1 gFxUser[1]
#define gFxUser2 gFxUser[2]
#define gFxUser3 gFxUser[3]
#define gFxUser4 gFxUser[4]
#define gFxUser5 gFxUser[5]
#define gFxUser6 gFxUser[6]
#define gFxUser7 gFxUser[7]

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gSimulationTimeParams;
    float4 gScreenParams;
};

#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshObjectData.hlsli"

#if HIKARI_FORWARD_ENABLE_MATERIAL_FX
#undef gFxFlags
#define gFxFlags pixelObjectData.fxFlags
#undef gFxUser
#define gFxUser pixelObjectData.fxUser
#endif

static const uint MATERIAL_UNLIT = 1u << 0;
static const uint MATERIAL_ALPHA_MASK = 1u << 1;
static const uint MATERIAL_EMISSIVE = 1u << 2;
static const uint MATERIAL_ALPHA_BLEND = 1u << 3;
static const uint MATERIAL_SPECULAR_GLOSS_COMPAT = 1u << 4;

#ifndef HIKARI_USE_COOK_TORRANCE_PBR
#define HIKARI_USE_COOK_TORRANCE_PBR 1
#endif

cbuffer LightCB : register(b2)
{
    float4 gDirectionalDir;
    float4 gDirectionalColor;
    float4 gAmbientColor;
    float4 gSpecularParams;
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

static const uint HIKARI_FORWARD_COST_FULL = 0u;
static const uint HIKARI_FORWARD_COST_ALBEDO_ONLY = 1u;
static const uint HIKARI_FORWARD_COST_NO_NORMAL_MAP = 2u;
static const uint HIKARI_FORWARD_COST_NO_SHADOW = 3u;
static const uint HIKARI_FORWARD_COST_NO_SSAO = 4u;
static const uint HIKARI_FORWARD_COST_NO_MATERIAL_EXTRAS = 5u;

#ifndef HIKARI_FORWARD_COST_MODE_STATIC
#define HIKARI_FORWARD_COST_MODE_STATIC 255
#endif

cbuffer ShadowCB : register(b4)
{
    float4x4 gShadowLightViewProj[4];
    float4 gShadowCascadeSplits;
    float4 gShadowCascadeParams;
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
};

struct HikariReflectionProbeData
{
    float4 positionRadius;
    float4 params;
    float4 intensity;
    float4 influenceBoxMin;
    float4 influenceBoxMax;
    float4 projectionBoxMin;
    float4 projectionBoxMax;
    float4 shapeParams;
};

#include "Include/Lighting/HIKARI_LocalLightList.hlsli"

struct HikariIrradianceVolumeData
{
    float4 originSampling;
    float4 spacingIntensity;
    float4 countsProbeCount;
    float4 sizeBlendDistance;
    float4 selectionParams;
};

cbuffer SkyEnvironmentCB : register(b5)
{
    float4 gSkyZenithExposure;
    float4 gSkyHorizonReflection;
    float4 gSkyGroundAmbient;
    float4 gSkyParams;
    float4 gSkyTintLighting;
    float4 gIblParams;
    HikariReflectionProbeData gReflectionProbes[2];
    float4 gAmbientSamplingParams;
    HikariIrradianceVolumeData gIrradianceVolumes[2];
};

#define gSkyZenithColor gSkyZenithExposure.rgb
#define gSkyExposure gSkyZenithExposure.a
#define gSkyHorizonColor gSkyHorizonReflection.rgb
#define gSkyReflectionIntensity gSkyHorizonReflection.a
#define gSkyGroundColor gSkyGroundAmbient.rgb
#define gSkyAmbientFromSky gSkyGroundAmbient.a
#define gSkyMode gSkyParams.x
#define gSkyHasCubemap gSkyParams.y
#define gSkyHorizonPower gSkyParams.z
#define gSkyYaw gSkyParams.w
#define gSkyTint gSkyTintLighting.rgb
#define gIblHasIrradiance gIblParams.x
#define gIblHasPrefiltered gIblParams.y
#define gIblHasBrdfLut gIblParams.z
#define gIblPrefilteredMipCount gIblParams.w
#define gSsaoEnabled gAmbientSamplingParams.x
#define gSsaoDiffuseStrength gAmbientSamplingParams.y
#define gSsaoSpecularStrength gAmbientSamplingParams.z
#define gSurfaceLightmapEnabled gAmbientSamplingParams.w

Texture2D gShadowMap : register(t2);
TextureCube gSkyCube : register(t6);
Texture2D<float> gSceneDepthTex : register(t7);
Texture2D gSceneColorTex : register(t8);
TextureCube gIblIrradianceTex : register(t9);
TextureCube gIblPrefilteredTex : register(t10);
Texture2D gIblBrdfLutTex : register(t11);
TextureCube gReflectionProbePrefilteredTex0 : register(t12);
Texture2D gSsaoTex : register(t13);
TextureCube gReflectionProbePrefilteredTex1 : register(t14);
// t1 space7 は sand の高さバッファが使うため、screen-space は t2/t3 へ移動。
Texture2D<float> gScreenSpaceDepthPyramid : register(t2, space7);
Texture2D<float4> gScreenSpacePackedSurface : register(t3, space7);
cbuffer ScreenSpaceTraceControlCB : register(b10)
{
    uint gScreenSpaceTraceValid;
    uint gScreenSpaceDepthPyramidMipCount;
};
// Light probe volume: SH9 係数を係数ごとに 1 枚の Texture3D (RGB=係数) に
// 焼き、hardware trilinear で係数を補間する。9 枚は root param LightProbeSh の
// descriptor table として連続確保される。
Texture3D<float4> gLightProbeShVolume0[9] : register(t0, space2);
Texture3D<float4> gLightProbeShVolume1[9] : register(t0, space4);
SamplerState gLinearWrap : register(s0);
SamplerComparisonState gShadowSampler : register(s1);
SamplerState gLinearClamp : register(s2);
SamplerState gPointClamp : register(s3);

#include "Include/HIKARI_PbrCommon.hlsli"
#include "Include/HIKARI_SkyEnvironmentCommon.hlsli"
#include "Include/HIKARI_DebugViewCommon.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"
#include "Include/Forward/HIKARI_ShadowSampling.hlsli"

#endif
