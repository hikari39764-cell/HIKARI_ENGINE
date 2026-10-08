#pragma once

#include <array>
#include <span>

#include <d3d12.h>
#include <d3dx12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingLighting.h"

namespace HIKARI::MESHRENDERER {
struct MeshMaterialGpuFrameView;
}

// 実行時光線クエリ（HIKARI_RuntimeRayLighting.hlsli）を使う compute pass が共有する根パラメーター。
// 0: CBV(b0)、1: 加速構造(t0,s8)、2: 命中記録(t1,s8)、3: 材質(t16)、4/5: program の引数と資源(t0/t1,s6)、
// 6/7: 局所光と CDF(t1/t2,s7)、8: 幾何の記述子表(t0,s1)、9: テクスチャ表(t20)。空の立方体(t0,s7)は呼び出し側が
// 指定した番号の表に置く（動的 GI は自分の表の後ろに置く）。呼び出し側は自分の範囲を 10 番以降に続ける。
namespace HIKARI::RENDER3D::RAYTRACING {

inline constexpr UINT kRuntimeRayQueryRootParameterCount = 10u;
inline constexpr UINT kRuntimeRayQueryRangeCount = 3u; // 幾何、テクスチャ、空

// parameters[0..9] と ranges[0..2] を埋め、空の表の範囲を ranges[2] に用意する（parameters[skyParameter] へ付ける）。
void InitRuntimeRayQueryRootParameters(std::span<CD3DX12_ROOT_PARAMETER> parameters,
                                       std::span<CD3DX12_DESCRIPTOR_RANGE> ranges, UINT skyParameter);

// s0: linear clamp（空）、s1: linear wrap（材質テクスチャ）。
std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> RuntimeRayQueryStaticSamplers();

// 1..9 と空の表を束ねる。CBV(0) と呼び出し側の表は呼び出し側が束ねる。
void BindRuntimeRayQueryRoots(ID3D12GraphicsCommandList* commands, const GFX::Context& graphics,
                              const RuntimeRayLightingFrame& lighting,
                              const MESHRENDERER::MeshMaterialGpuFrameView& materials, UINT skyParameter);

} // namespace HIKARI::RENDER3D::RAYTRACING
