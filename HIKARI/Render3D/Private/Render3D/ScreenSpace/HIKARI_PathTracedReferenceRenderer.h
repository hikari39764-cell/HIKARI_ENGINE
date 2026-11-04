#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "RenderCore/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D::RAYTRACING {
struct RuntimeRayLightingFrame;
}

namespace HIKARI::RENDER3D::SCREENSPACE {

struct PathTracedReferenceInputs {
    const TEMPORAL::TemporalFrameState* temporal = nullptr;
    const RAYTRACING::RuntimeRayLightingFrame* rayLighting = nullptr;
    const MESHRENDERER::MeshMaterialGpuFrameView* materials = nullptr;
    uint32_t width = 0u;
    uint32_t height = 0u;
    // 初回の面の直接光と鏡面を除き、間接拡散だけを出す（調試視図 IndirectDiffuse と比べる）。
    bool indirectDiffuseOnly = false;
};

// 調試視図の路径追跡の参照。実行時の加速構造へ画素ごとに一本の路径を飛ばして RGBA32F に和を取り、相機・場面・照明・
// 材質が変わらない間はフレームをまたいで累積する。面に当たった画素だけ、平均を前露出して Forward が書いた場面色へ
// 上書きする（空はラスターのまま）。
class PathTracedReferenceRenderer final {
public:
    PathTracedReferenceRenderer() = default;
    PathTracedReferenceRenderer(const PathTracedReferenceRenderer&) = delete;
    PathTracedReferenceRenderer& operator=(const PathTracedReferenceRenderer&) = delete;

    bool Render(const GFX::Context& graphics, const PIPELINE::RenderTargetAccess& targetAccess,
                const PathTracedReferenceInputs& inputs);
    void Release();

private:
    bool EnsurePipelines(const GFX::Context& graphics);
    bool EnsureAccumulation(const GFX::Context& graphics, uint32_t width, uint32_t height);
    void ReleaseAccumulation(const GFX::Context& graphics);
    void Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES next);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> traceRootSignature_{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> compositeRootSignature_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> tracePipeline_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePipeline_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> accumulation_{};
    RenderResourceView accumulationSrv_{};
    RenderResourceView accumulationUav_{};
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads_{};
    uint64_t accumulationKey_ = 0u;
    uint32_t sampleIndex_ = 0u;
    uint64_t deviceGeneration_ = 0u;
    uint32_t width_ = 0u;
    uint32_t height_ = 0u;
    bool pipelineFailed_ = false;
};

} // namespace HIKARI::RENDER3D::SCREENSPACE
