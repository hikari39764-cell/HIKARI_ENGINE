// Water surfaces are excluded from the current shadow route. Keep the shadow
// mesh shader independent from the forward/depth water-data root descriptor.
#define HIKARI_MASS_GPU_SCENE_DISABLED 1
#define HIKARI_MESHLET_DEPTH_ENABLE_WATER 0
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Render3D_MeshletDepthMS.hlsl"
