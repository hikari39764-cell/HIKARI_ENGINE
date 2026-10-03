#define HIKARI_FORWARD_COST_MODE_STATIC 3
// 地形は専用 PSO、水面は depth-aware PSO が描く。
#define HIKARI_SURFACE_PARTITION 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Render3D_GpuDrivenOpaquePS.hlsl"
