#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::RENDER3D::SURFACE {

    class SceneSurfaceBuffer {
    public:
        bool EnsureSize(
            const GFX::Context& graphics,
            uint32_t width,
            uint32_t height);
        void Release();

        void BeginWrite(
            ID3D12GraphicsCommandList* cmd,
            D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);
        void EndWrite(ID3D12GraphicsCommandList* cmd);

        bool IsValid() const;
        uint32_t GetWidth() const { return width_; }
        uint32_t GetHeight() const { return height_; }
        DXGI_FORMAT GetFormat() const { return kNormalRoughnessFormat; }
        ID3D12Resource* GetPackedSurfaceResource() const {
            return packedSurface_.Get();
        }
        D3D12_GPU_DESCRIPTOR_HANDLE GetPackedSurfaceSrv() const {
            return packedSurfaceSrvGpu_;
        }
        RenderTextureView GetPackedSurfaceView() const;

    private:
        bool CreateResources(
            const GFX::Context& graphics,
            uint32_t width,
            uint32_t height);

        static constexpr DXGI_FORMAT kNormalRoughnessFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        Microsoft::WRL::ComPtr<ID3D12Resource> packedSurface_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_{};
        D3D12_CPU_DESCRIPTOR_HANDLE packedSurfaceRtv_{};
        D3D12_CPU_DESCRIPTOR_HANDLE packedSurfaceSrvCpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE packedSurfaceSrvGpu_{};
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        D3D12_RESOURCE_STATES packedSurfaceState_ =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };

} // namespace HIKARI::RENDER3D::SURFACE
