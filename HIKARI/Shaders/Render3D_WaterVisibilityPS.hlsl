// Reuse the authoritative water coverage helper and its GPU water-data
// contract from the forward shader. This pass only keeps the coverage and
// depth-occlusion decisions; it never writes SceneColor.
#define main HikariWaterVisibilityForwardMain
#include "Render3D_StaticPS.hlsl"
#undef main
#include "Include/HIKARI_WaterSurfaceParams.hlsli"

// Keep the clear value (1.0) distinguishable from a water surface that reaches
// the camera far plane.  The visibility target is only a producer-owned
// depth view; this conservative clamp does not alter SceneDepth or the
// forward water shading path.
static const float HIKARI_WATER_VISIBILITY_HORIZON_DEPTH = 0.99999f;

float main(PSInput input) : SV_Depth
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

    // SV_Position.z is already device depth.  Keep far-plane water valid in
    // the optional visibility view while leaving a clear texel at exactly 1.
    return min(waterDepth, HIKARI_WATER_VISIBILITY_HORIZON_DEPTH);
}
