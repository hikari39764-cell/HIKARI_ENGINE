#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::RENDER3D::SURFACE {

class SceneSurfaceBuffer {
public:
    bool EnsureSize(const GFX::Context& graphics, uint32_t width, uint32_t height, bool enableFeatureMask = false);
    bool SetFeatureMaskEnabled(const GFX::Context& graphics, bool enabled);
    // Retire transient render targets while preserving descriptor slots for
    // a later re-enable of the same view.
    void DiscardResources();
    void Release();

    void BeginWrite(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);
    void EndWrite(ID3D12GraphicsCommandList* cmd);

    bool IsValid() const;
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    DXGI_FORMAT GetFormat() const { return kNormalRoughnessFormat; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetPackedSurfaceSrv() const { return packedSurfaceSrvs_[activeFrameSlot_].gpu; }
    RenderTextureView GetPackedSurfaceView() const;
    bool HasFeatureMask() const;
    RenderTextureView GetFeatureMaskView() const;
    bool HasStylizedOutlineStyle() const;
    RenderTextureView GetStylizedOutlineStyleView() const;

private:
    bool CreateResources(const GFX::Context& graphics, uint32_t width, uint32_t height, bool enableFeatureMask);
    bool EnsureFeatureMask(const GFX::Context& graphics, bool enabled);
    bool PublishPackedSurfaceSrv(const GFX::Context& graphics);
    bool PublishFeatureMaskSrv(const GFX::Context& graphics);
    bool PublishStylizedOutlineStyleSrv(const GFX::Context& graphics);
    void ResetResources(bool releaseDescriptors);

    static constexpr DXGI_FORMAT kNormalRoughnessFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT kFeatureMaskFormat = DXGI_FORMAT_R32G32_UINT;
    static constexpr DXGI_FORMAT kStylizedOutlineStyleFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    Microsoft::WRL::ComPtr<ID3D12Resource> packedSurface_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> featureMask_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> stylizedOutlineStyle_{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_{};
    D3D12_CPU_DESCRIPTOR_HANDLE packedSurfaceRtv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE featureMaskRtv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE stylizedOutlineStyleRtv_{};
    std::array<RenderResourceView, GFX::kFrameResourceCount> packedSurfaceSrvs_{};
    std::array<RenderResourceView, GFX::kFrameResourceCount> featureMaskSrvs_{};
    std::array<RenderResourceView, GFX::kFrameResourceCount> stylizedOutlineStyleSrvs_{};
    std::array<uint64_t, GFX::kFrameResourceCount> packedSurfaceSrvGenerations_{};
    std::array<uint64_t, GFX::kFrameResourceCount> featureMaskSrvGenerations_{};
    std::array<uint64_t, GFX::kFrameResourceCount> stylizedOutlineStyleSrvGenerations_{};
    uint64_t packedSurfaceGeneration_ = 0u;
    uint64_t featureMaskGeneration_ = 0u;
    uint64_t stylizedOutlineStyleGeneration_ = 0u;
    uint32_t activeFrameSlot_ = 0u;
    UINT rtvDescriptorSize_ = 0u;
    GFX::Context lastGraphicsContext_{};
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    D3D12_RESOURCE_STATES packedSurfaceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES featureMaskState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES stylizedOutlineStyleState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};

} // namespace HIKARI::RENDER3D::SURFACE
