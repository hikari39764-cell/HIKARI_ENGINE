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

#include "Include/Render3D/HIKARI_MeshCameraData.hlsli"

#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshObjectData.hlsli"

#if HIKARI_FORWARD_ENABLE_MATERIAL_FX
#undef gFxFlags
#define gFxFlags pixelObjectData.fxFlags
#undef gFxUser
#define gFxUser pixelObjectData.fxUser
#endif

static const uint MATERIAL_UNLIT = HIKARI_MATERIAL_FLAG_UNLIT;
static const uint MATERIAL_ALPHA_MASK = HIKARI_MATERIAL_FLAG_ALPHA_MASK;
static const uint MATERIAL_EMISSIVE = HIKARI_MATERIAL_FLAG_EMISSIVE;
static const uint MATERIAL_ALPHA_BLEND = HIKARI_MATERIAL_FLAG_ALPHA_BLEND;
static const uint MATERIAL_SPECULAR_GLOSS_COMPAT = HIKARI_MATERIAL_FLAG_LEGACY_SPECULAR;

#include "Include/Lighting/HIKARI_ViewLightingData.hlsli"

static const uint HIKARI_FORWARD_COST_FULL = 0u;
static const uint HIKARI_FORWARD_COST_ALBEDO_ONLY = 1u;
static const uint HIKARI_FORWARD_COST_NO_NORMAL_MAP = 2u;
static const uint HIKARI_FORWARD_COST_NO_SHADOW = 3u;
static const uint HIKARI_FORWARD_COST_NO_SSAO = 4u;
static const uint HIKARI_FORWARD_COST_NO_MATERIAL_EXTRAS = 5u;

#ifndef HIKARI_FORWARD_COST_MODE_STATIC
#define HIKARI_FORWARD_COST_MODE_STATIC 255
#endif

#include "Include/Lighting/HIKARI_LocalLightList.hlsli"


Texture2D<float> gShadowMap : register(t2);
Texture2D<float> gSceneDepthTex : register(t7);
Texture2D gSceneColorTex : register(t8);
Texture2D gSsaoTex : register(t13);
// t1 space7 は sand の高さバッファが使うため、screen-space は t2/t3 へ移動。
Texture2D<float> gScreenSpaceDepthPyramid : register(t2, space7);
Texture2D<float4> gScreenSpacePackedSurface : register(t3, space7);
cbuffer ScreenSpaceTraceControlCB : register(b10)
{
    uint gScreenSpaceTraceFlags;
    uint gScreenSpaceDepthPyramidMipCount;
};
static const uint HIKARI_SCREEN_SPACE_DEPTH_TRACE_VALID = 1u << 0;
static const uint HIKARI_SCREEN_SPACE_SCENE_COLOR_CAPTURE_VALID = 1u << 1;
SamplerState gLinearWrap : register(s0);
SamplerComparisonState gShadowSampler : register(s1);
SamplerState gLinearClamp : register(s2);
SamplerState gPointClamp : register(s3);
SamplerState gSkyPanoramaSampler : register(s4);

#include "Include/HIKARI_PbrCommon.hlsli"
#include "Include/HIKARI_SkyEnvironmentCommon.hlsli"

float HikariForwardCloudSolarVisibility(float3 worldPosition)
{
    if(gCloudSolarVisibility.originDescriptor.w<0.5f || gDirectionalIntensity<=0) return 1.0f;
    const uint descriptor=uint(gCloudSolarVisibility.originDescriptor.w)-1u;
    return HikariSampleCloudSolarVisibility(gMaterialTexturePool[NonUniformResourceIndex(descriptor)],gCloudSolarVisibility,worldPosition);
}
#include "Include/HIKARI_DebugViewCommon.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"
#include "Include/Forward/HIKARI_ShadowSampling.hlsli"

#endif
