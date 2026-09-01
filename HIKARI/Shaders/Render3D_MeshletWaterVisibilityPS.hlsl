#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"

// Keep the water coverage test identical to the forward water shader while
// consuming the compact depth-MS signature (TEXCOORD0/4/5).
#define HIKARI_FORWARD_MESHLET_SURFACE_FEATURES 1
#define HIKARI_FORCE_SURFACE_GPU_SCENE_PIXEL 1
#define main HikariWaterVisibilityForwardMain
#include "Render3D_GpuDrivenOpaquePS.hlsl"
#undef main
#include "Include/HIKARI_WaterSurfaceParams.hlsli"

struct MeshletWaterVisibilityPSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD4;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD5;
};

// Match the traditional visibility pass: DSV clear (1.0) must remain
// distinguishable from water clipped at the camera far plane.
static const float HIKARI_WATER_VISIBILITY_HORIZON_DEPTH = 0.99999f;

float main(MeshletWaterVisibilityPSInput input) : SV_Depth
{
    if ((input.surfaceFeatureFlags &
            HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_SURFACE) == 0u)
    {
        discard;
    }

    const HikariWaterSurfaceParams waterParams =
        HikariLoadWaterSurfaceParams(input.surfaceGpuSceneIndex);
    if (HikariSampleLiquidCoverage(waterParams, input.uv) < 0.5f)
    {
        discard;
    }

    // SV_Position.z is already device depth at the pixel stage; dividing by
    // SV_Position.w would compare against the wrong space.
    const uint2 pixel = uint2(input.position.xy);
    const float opaqueDepth = gSceneDepthTex.Load(int3(pixel, 0)).r;
    const float waterDepth = input.position.z;
    if (opaqueDepth < 0.999999f && waterDepth > opaqueDepth + 1.0e-5f)
    {
        discard;
    }

    return min(waterDepth, HIKARI_WATER_VISIBILITY_HORIZON_DEPTH);
}
