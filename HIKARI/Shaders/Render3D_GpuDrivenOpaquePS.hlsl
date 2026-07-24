// GPU-driven opaque entry point for the meshlet/cluster mainline.
// Surface features branch on a nointerpolation flag. Ordinary surfaces avoid
// the SurfaceGpuScene parameter read; only MaterialFx pixels resolve fxUser.
#define HIKARI_FORWARD_ENABLE_MATERIAL_FX 1
#define HIKARI_FORWARD_MESHLET_SURFACE_FEATURES 1
// Meshlet draw constants reuse b8 for visible-range dispatch metadata. They
// are not the legacy SurfaceGpuSceneControl layout, so GPU-driven pixels must
// resolve object/FX data directly from the absolute SurfaceGpuScene index.
#define HIKARI_FORCE_SURFACE_GPU_SCENE_PIXEL 1
#include "Render3D_StaticPS.hlsl"
