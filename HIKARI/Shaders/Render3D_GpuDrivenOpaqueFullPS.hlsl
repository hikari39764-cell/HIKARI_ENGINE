// 通常の不透明 Forward は一般面だけを描く。catalog の define で
// 全経路 (depth-aware/transparent/SSR capture/debug) と地形専用を作り分ける。
#ifndef HIKARI_SURFACE_PARTITION
#define HIKARI_SURFACE_PARTITION 1
#endif
#include "Include/Surface/HIKARI_SurfacePartition.hlsli"
// SSR capture と地形専用 PSO は cost mode を実行時に読む (地形は全 mode を 1 PSO で扱う)。
#if HIKARI_FORWARD_REFLECTION_CAPTURE || HIKARI_SURFACE_PARTITION == HIKARI_SURFACE_PARTITION_TERRAIN
#define HIKARI_FORWARD_COST_MODE_STATIC 255
#else
#define HIKARI_FORWARD_COST_MODE_STATIC 0
#endif
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Render3D_GpuDrivenOpaquePS.hlsl"
