#ifndef HIKARI_FORWARD_COMMON_INCLUDED
#define HIKARI_FORWARD_COMMON_INCLUDED

#include "Include/Contracts/HIKARI_ShaderResourceBindings.hlsli"

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
    float4 gPointLightPosRange[8];
    float4 gPointLightColorIntensity[8];
    float gDirectionalIntensity;
    float gAmbientIntensity;
    uint gPointLightCount;
    float gLightPadding;
    float4 gFogColorDensity;
    float4 gFogParams;
    uint gDebugView;
    float gForwardCostMode;
    float2 gDebugPadding;
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
    float4x4 gShadowLightViewProj;
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

cbuffer SkyEnvironmentCB : register(b5)
{
    float4 gSkyZenithExposure;
    float4 gSkyHorizonReflection;
    float4 gSkyGroundAmbient;
    float4 gSkyParams;
    float4 gIblParams;
    float4 gReflectionProbePositionRadius;
    float4 gReflectionProbeParams;
    float4 gReflectionProbeIntensity;
    float4 gReflectionProbeInfluenceBoxMin;
    float4 gReflectionProbeInfluenceBoxMax;
    float4 gReflectionProbeProjectionBoxMin;
    float4 gReflectionProbeProjectionBoxMax;
    float4 gReflectionProbeShapeParams;
    float4 gAoParams;
    float4 gLightProbeVolumeOrigin;
    float4 gLightProbeVolumeSpacing;
    float4 gLightProbeVolumeCounts;
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
#define gIblHasIrradiance gIblParams.x
#define gIblHasPrefiltered gIblParams.y
#define gIblHasBrdfLut gIblParams.z
#define gIblPrefilteredMipCount gIblParams.w
#define gReflectionProbePosition gReflectionProbePositionRadius.xyz
#define gReflectionProbeRadius gReflectionProbePositionRadius.w
#define gReflectionProbeEnabled gReflectionProbeParams.x
#define gReflectionProbeHasPrefiltered gReflectionProbeParams.y
#define gReflectionProbeHasBrdfLut gReflectionProbeParams.z
#define gReflectionProbeMipCount gReflectionProbeParams.w
#define gReflectionProbeSpecularIntensity gReflectionProbeIntensity.x
#define gReflectionProbeInfluenceShape gReflectionProbeShapeParams.x
#define gReflectionProbeProjectionShape gReflectionProbeShapeParams.y
#define gReflectionProbeBlendDistance gReflectionProbeShapeParams.z
#define gReflectionProbePriority gReflectionProbeShapeParams.w
#define gSsaoEnabled gAoParams.x
#define gSsaoDiffuseStrength gAoParams.y
#define gSsaoSpecularStrength gAoParams.z
#define gLightProbeSamplingMode gLightProbeVolumeOrigin.w
#define gLightProbeEnabled ((gLightProbeSamplingMode > 0.5f) ? 1.0f : 0.0f)
#define gLightProbeOrigin gLightProbeVolumeOrigin.xyz
#define gLightProbeSpacing gLightProbeVolumeSpacing.xyz
#define gLightProbeIntensity gLightProbeVolumeSpacing.w
#define gLightProbeCountX ((uint)(gLightProbeVolumeCounts.x + 0.5f))
#define gLightProbeCountY ((uint)(gLightProbeVolumeCounts.y + 0.5f))
#define gLightProbeCountZ ((uint)(gLightProbeVolumeCounts.z + 0.5f))
#define gLightProbeProbeCount ((uint)(gLightProbeVolumeCounts.w + 0.5f))

Texture2D gShadowMap : register(t2);
TextureCube gSkyCube : register(t6);
Texture2D gSceneDepthTex : register(t7);
Texture2D gSceneColorTex : register(t8);
TextureCube gIblIrradianceTex : register(t9);
TextureCube gIblPrefilteredTex : register(t10);
Texture2D gIblBrdfLutTex : register(t11);
TextureCube gReflectionProbePrefilteredTex : register(t12);
Texture2D gSsaoTex : register(t13);
// Light probe volume: SH9 係数を係数ごとに 1 枚の Texture3D (RGB=係数) に
// 焼き、hardware trilinear で係数を補間する。9 枚は root param LightProbeSh の
// descriptor table として連続確保される。
Texture3D<float4> gLightProbeShVolume[9] : register(t0, space2);
SamplerState gLinearWrap : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

#include "Include/HIKARI_PbrCommon.hlsli"
#include "Include/HIKARI_SkyEnvironmentCommon.hlsli"
#include "Include/HIKARI_DebugViewCommon.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"
#include "Include/Forward/HIKARI_ShadowSampling.hlsli"

#endif
