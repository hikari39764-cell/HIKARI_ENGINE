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

// 診断モード。C++ の ForwardShadingCostMode と同じ番号（6 の GeneralFull は何も省かないので定数を持たない）。
static const uint HIKARI_FORWARD_COST_FULL = 0u;
static const uint HIKARI_FORWARD_COST_ALBEDO_ONLY = 1u;
static const uint HIKARI_FORWARD_COST_NO_NORMAL_MAP = 2u;
static const uint HIKARI_FORWARD_COST_NO_SHADOW = 3u;
static const uint HIKARI_FORWARD_COST_NO_SSAO = 4u;
static const uint HIKARI_FORWARD_COST_NO_MATERIAL_EXTRAS = 5u;
static const uint HIKARI_FORWARD_COST_NO_SUN_SHADOW = 7u;
static const uint HIKARI_FORWARD_COST_NO_CONTACT_SHADOW = 8u;
static const uint HIKARI_FORWARD_COST_NO_LIGHT_PROBES = 9u;
static const uint HIKARI_FORWARD_COST_NO_ENV_REFLECTION = 10u;
static const uint HIKARI_FORWARD_COST_NO_LOCAL_LIGHTS = 11u;
static const uint HIKARI_FORWARD_COST_NO_CLOUD_SHADOW = 12u;
static const uint HIKARI_FORWARD_COST_NO_DECALS = 13u;
static const uint HIKARI_FORWARD_COST_SKELETON = 14u;

#ifndef HIKARI_FORWARD_COST_MODE_STATIC
#define HIKARI_FORWARD_COST_MODE_STATIC 255
#endif

#include "Include/Lighting/HIKARI_LocalLightList.hlsli"


Texture2D<float> gShadowMap : register(t2);
Texture2D<float> gSceneDepthTex : register(t7);
Texture2D gSceneColorTex : register(t8);
// 近場の GI：rgb 照り返し E/pi（前露出済み）、a 可視性（Post_SSAOPS）。
Texture2D<float4> gSsaoTex : register(t13);
// t1 space7 は地形/水面の共有 float pool が使うため、screen-space は t2/t3 へ移動。
Texture2D<float> gScreenSpaceDepthPyramid : register(t2, space7);
Texture2D<float4> gScreenSpacePackedSurface : register(t3, space7);
// 光線追跡の太陽の可視率（現フレーム・描画解像度、1 = 照らされる）。HIKARI_SCREEN_SPACE_RAY_SHADOW_VALID の時だけ読む。
Texture2D<float> gRayTracedShadow : register(t11, space7);
cbuffer ScreenSpaceTraceControlCB : register(b10)
{
    uint gScreenSpaceTraceFlags;
    uint gScreenSpaceDepthPyramidMipCount;
};
static const uint HIKARI_SCREEN_SPACE_DEPTH_TRACE_VALID = 1u << 0;
static const uint HIKARI_SCREEN_SPACE_SCENE_COLOR_CAPTURE_VALID = 1u << 1;
static const uint HIKARI_SCREEN_SPACE_RAY_SHADOW_VALID = 1u << 2;
// 深度ピラミッドが現フレームの場面深度（接触影が辿れる）。不透明の Forward でも立つ。
static const uint HIKARI_SCREEN_SPACE_CONTACT_DEPTH_VALID = 1u << 3;
SamplerState gLinearWrap : register(s0);
SamplerComparisonState gShadowSampler : register(s1);
SamplerState gLinearClamp : register(s2);
SamplerState gPointClamp : register(s3);
SamplerState gSkyPanoramaSampler : register(s4);

#include "Include/HIKARI_PbrCommon.hlsli"
#include "Include/HIKARI_SkyEnvironmentCommon.hlsli"

// 面が受ける太陽の見え方：雲の影 × 流体の烟の影。
float HikariForwardCloudSolarVisibility(float3 worldPosition)
{
    if(gDirectionalIntensity<=0) return 1.0f;
    return HikariSampleCloudSolarVisibility(gMaterialTexturePool[HikariSolarVisibilityDescriptor(gCloudSolarVisibility)],
               gCloudSolarVisibility,worldPosition)*
           HikariSampleCloudSolarVisibility(gMaterialTexturePool[HikariSolarVisibilityDescriptor(gFluidSolarVisibility)],
               gFluidSolarVisibility,worldPosition);
}
#include "Include/HIKARI_DebugViewCommon.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"
#include "Include/Forward/HIKARI_ShadowSampling.hlsli"

#endif
