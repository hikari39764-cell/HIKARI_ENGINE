#pragma once

#include <cstdint>
#include <span>

#include <d3d12.h>
#include <wrl/client.h>

#include "Core/Math/HIKARI_Math3D.h"
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::RENDER3D::RAYTRACING {

// 変形した頂点 1 個の大きさ（位置 float3 + 法線 float3 + 接線 float4）。
inline constexpr uint32_t kRuntimeRayTracingDeformedVertexBytes = 40u;

struct RuntimeRayTracingDeformJob {
    MATH::Mat4 world{};
    uint32_t geometryDescriptor = 0; // cluster geometry 記述子 arena の中の番号
    uint32_t vertexCount = 0;
    uint32_t paletteOffsetBytes = 0;
    uint32_t paletteMatrixCount = 0;
    uint32_t outputBase = 0; // pool の中の byte 位置
};

// 光線追跡のために変形する面（骨格の蒙皮）の頂点を、描画と同じ変形関数で world 空間へ変形して一つの pool に並べる
// compute pass。pool は [位置 × N][法線 × N][接線 × N] を面ごとに続け、BLAS の頂点と光線の命中の解決が読む。
class RuntimeRayTracingDeformation final {
public:
    RuntimeRayTracingDeformation() = default;
    ~RuntimeRayTracingDeformation();
    RuntimeRayTracingDeformation(const RuntimeRayTracingDeformation&) = delete;
    RuntimeRayTracingDeformation& operator=(const RuntimeRayTracingDeformation&) = delete;

    // pool を bytes 以上にする。作り直すと番地が変わる（それを指す BLAS は組み直す）。
    bool EnsurePool(const GFX::Context& graphics, uint64_t bytes);
    // jobs を pool へ書き、BLAS の build と光線の読み取りに使える状態へ移す。
    bool Dispatch(const GFX::Context& graphics, std::span<const RuntimeRayTracingDeformJob> jobs,
                  D3D12_GPU_VIRTUAL_ADDRESS palettes);
    D3D12_GPU_VIRTUAL_ADDRESS PoolAddress() const noexcept;
    uint64_t PoolBytes() const noexcept { return poolBytes_; }
    void Reset(const GFX::Context& graphics);

private:
    bool EnsurePipeline(const GFX::Context& graphics);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pool_;
    D3D12_RESOURCE_STATES poolState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    uint64_t poolBytes_ = 0;
    uint64_t deviceGeneration_ = 0;
    bool pipelineFailed_ = false;
};

} // namespace HIKARI::RENDER3D::RAYTRACING
