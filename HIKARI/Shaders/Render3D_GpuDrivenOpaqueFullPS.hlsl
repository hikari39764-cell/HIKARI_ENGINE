// 通常の不透明 Forward は一般面だけを描く。catalog の define で
// 全経路 (depth-aware/transparent/SSR capture/debug) と地形専用を作り分ける。
#ifndef HIKARI_SURFACE_PARTITION
#define HIKARI_SURFACE_PARTITION 1
#endif
#include "Include/Surface/HIKARI_SurfacePartition.hlsli"
// 通常の描画 (SSR capture と地形専用を含む) は Full (0) に固定し、診断の分岐を持たない。診断用の PSO は catalog の
// define が mode を固定する (番号は C++ の ForwardShadingCostMode と同じ。何も省かない GeneralFull はこの既定と同じ
// PS なので define を持たない FullPS をそのまま使う)。地形の診断 PSO だけは全 mode を 1 つで扱う 255 (実行時に読む)。
#ifndef HIKARI_FORWARD_COST_MODE_STATIC
#define HIKARI_FORWARD_COST_MODE_STATIC 0
#endif
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Render3D_GpuDrivenOpaquePS.hlsl"
