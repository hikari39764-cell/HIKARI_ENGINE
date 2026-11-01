#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "RenderCore/Resources/HIKARI_RenderResourcePool.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::RENDER3D::SCREENSPACE {

// 画面空間の次表面散乱。Forward が SubsurfaceDiffuse（拡散光 × 強さ, 半径 [m]）へ書いた面だけを、深度に沿って
// 水平→垂直にぼかし、ぼかした分と元の差を場面色へ加算する。中間の水平結果だけを所有し、入力は frame graph が渡す。
class SubsurfaceScatteringRenderer final {
public:
    SubsurfaceScatteringRenderer() = default;
    SubsurfaceScatteringRenderer(const SubsurfaceScatteringRenderer&) = delete;
    SubsurfaceScatteringRenderer& operator=(const SubsurfaceScatteringRenderer&) = delete;

    bool Render(const GFX::Context& graphics, const PIPELINE::RenderTargetAccess& targetAccess,
                const RenderTextureView& subsurfaceDiffuse, const RenderTextureView& sceneDepth,
                const MESHRENDERER::CameraCB& camera, const SubsurfaceScatteringSettings& settings);
    void Release();

private:
    bool EnsurePipeline(const GFX::Context& graphics);
    bool EnsureTarget(const GFX::Context& graphics, uint32_t width, uint32_t height);
    void ReleaseTarget(const GFX::Context& graphics);
    void Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES next);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> horizontalPso_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePso_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> horizontal_{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_{};
    D3D12_CPU_DESCRIPTOR_HANDLE horizontalRtv_{};
    RenderResourceView horizontalSrv_{};
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads_{};
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    uint64_t deviceGeneration_ = 0u;
    uint32_t width_ = 0u;
    uint32_t height_ = 0u;
};

} // namespace HIKARI::RENDER3D::SCREENSPACE
