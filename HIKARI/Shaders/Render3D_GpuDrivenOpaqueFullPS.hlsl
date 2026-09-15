// SSR capture は同じ MRT/PSO のまま診断する。通常 Forward は従来の静的 Full。
#if HIKARI_FORWARD_REFLECTION_CAPTURE
#define HIKARI_FORWARD_COST_MODE_STATIC 255
#else
#define HIKARI_FORWARD_COST_MODE_STATIC 0
#endif
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Render3D_GpuDrivenOpaquePS.hlsl"
