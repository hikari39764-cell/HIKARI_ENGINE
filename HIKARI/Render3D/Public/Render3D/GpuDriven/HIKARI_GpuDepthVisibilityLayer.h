#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "RenderCore/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

struct GpuDepthVisibilityStats {
    bool psoReady = false;
    bool resourcesReady = false;
    bool depthPrepassWritten = false;
    bool hzbBuilt = false;
    bool hzbBuildRequested = false;
    bool descriptorPoolReady = false;
    bool visibilityDepthReady = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t hzbWidth = 0;
    uint32_t hzbHeight = 0;
    uint32_t hzbMipCount = 0;
    uint32_t hzbDescriptorCount = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE visibilityDepthSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE hzbFinestSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE hzbCoarsestSrv{};
    MATH::Mat4 hzbViewProj{};
    bool hzbViewProjValid = false;
    HIKARI::RENDER3D::DEPTH::DepthPyramidView depthPyramid{};
};

class GpuDepthVisibilityLayer final {
public:
    void ResetFrame();
    // Uses the most recent EnsureDepthResource context, preserving the
    // legacy no-argument shutdown call without consulting global GFX
    // context state.
    void Release();
    // Explicit shutdown path for owners that still hold the frame context.
    void Release(const GFX::Context& graphics);

    void RecordDepthPrepass(bool written);
    D3D12_CPU_DESCRIPTOR_HANDLE BeginDepthPrepass(const GFX::Context& graphics, ID3D12GraphicsCommandList* cmd,
                                                  uint32_t width, uint32_t height);
    // Completes the independent visibility depth pass and publishes its
    // SRV in the existing frame descriptor pool. This never transitions
    // or writes the authoritative scene depth resource.
    bool EndDepthPrepass(const GFX::Context& graphics, ID3D12GraphicsCommandList* cmd);
    bool BuildDepthPyramidFromVisibilityPrepass(const GFX::Context& graphics, ID3D12GraphicsCommandList* cmd,
                                                uint32_t width, uint32_t height, const MATH::Mat4& viewProj,
                                                bool jitteredViewProj = false);
    bool BuildDepthPyramidFromDepthSrv(const GFX::Context& graphics, ID3D12GraphicsCommandList* cmd, uint32_t width,
                                       uint32_t height, D3D12_GPU_DESCRIPTOR_HANDLE sourceDepthSrv,
                                       const MATH::Mat4& viewProj, bool jitteredViewProj = false);

    const GpuDepthVisibilityStats& GetStats() const { return stats_; }

private:
    bool EnsureDepthResource(const GFX::Context& graphics, uint32_t width, uint32_t height);
    void ReleaseDepthResource(const GFX::Context* graphics);
    void TransitionDepth(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES nextState);
    void PublishDepthPyramidStats(const HIKARI::RENDER3D::DEPTH::DepthPyramidView& view);

    HIKARI::RENDER3D::DEPTH::DepthPyramidLayer depthPyramid_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> visibilityDepth_{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> visibilityDsvHeap_{};
    RenderResourceView visibilityDepthSrv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE visibilityDsv_{};
    D3D12_RESOURCE_STATES visibilityDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    uint32_t depthWidth_ = 0;
    uint32_t depthHeight_ = 0;
    GFX::Context lastGraphicsContext_{};
    bool hasGraphicsContext_ = false;
    GpuDepthVisibilityStats stats_{};
};

} // namespace HIKARI::RENDER3D::GPUDRIVEN
