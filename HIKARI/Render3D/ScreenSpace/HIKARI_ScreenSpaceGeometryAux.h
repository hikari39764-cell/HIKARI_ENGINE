#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>

namespace HIKARI::RENDER3D::SCREENSPACE {

    class ScreenSpaceGeometryAux {
    public:
        bool EnsureSize(uint32_t width, uint32_t height);
        void Release();

        void BeginNormalRoughnessPass(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);
        void EndNormalRoughnessPass(ID3D12GraphicsCommandList* cmd);

        bool IsValid() const;
        uint32_t GetWidth() const { return width_; }
        uint32_t GetHeight() const { return height_; }
        DXGI_FORMAT GetFormat() const { return kNormalRoughnessFormat; }
        ID3D12Resource* GetNormalRoughnessResource() const { return normalRoughness_.Get(); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetNormalRoughnessSrv() const { return normalRoughnessSrvGpu_; }

    private:
        bool CreateResources(uint32_t width, uint32_t height);

        static constexpr DXGI_FORMAT kNormalRoughnessFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        Microsoft::WRL::ComPtr<ID3D12Resource> normalRoughness_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_{};
        D3D12_CPU_DESCRIPTOR_HANDLE normalRoughnessRtv_{};
        D3D12_CPU_DESCRIPTOR_HANDLE normalRoughnessSrvCpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSrvGpu_{};
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        D3D12_RESOURCE_STATES normalRoughnessState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };

} // namespace HIKARI::RENDER3D::SCREENSPACE
