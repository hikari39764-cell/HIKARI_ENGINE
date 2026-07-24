#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <d3dx12.h>
#include <array>
#include <string>
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI {

    class RenderTarget2D
    {
    public:
        RenderTarget2D() = default;
        ~RenderTarget2D() { Finalize(); }

        RenderTarget2D(const RenderTarget2D&) = delete;
        RenderTarget2D& operator=(const RenderTarget2D&) = delete;

        bool Init(
            int width,
            int height,
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
            bool withDepth = false,
            const std::array<float, 4>& optimizedClearColor = { 0.0f, 0.0f, 0.0f, 0.0f },
            bool publishDepthSrv = true,
            bool allowUnorderedAccess = false
        );

        void UpdateContext(const HIKARI::GFX::Context& ctx);
        void Finalize();

        void BeginCapture(float r = 0, float g = 0, float b = 0, float a = 0, float depthClear = 1.0f);
        void Rebind();
        bool BeginDepthRead();
        void EndDepthRead();
        bool BeginDepthShaderRead();
        void EndDepthShaderRead();
        void EndCapture();
        void SetDebugName(std::string name);
        const std::string& GetDebugName() const { return debugName_; }
        std::string DumpState() const;

        int GetWidth() const { return width_; }
        int GetHeight() const { return height_; }
        DXGI_FORMAT GetFormat() const { return format_; }
        D3D12_RESOURCE_STATES GetColorState() const { return colorState_; }
        D3D12_RESOURCE_STATES GetDepthState() const { return depthState_; }
        bool HasDepth() const { return hasDepth_; }
        bool AllowsUnorderedAccess() const { return allowUnorderedAccess_; }
        bool IsInitialized() const { return initialized_; }
        void TransitionColor(D3D12_RESOURCE_STATES nextState);

        ID3D12Resource* GetResource() const { return colorTex_.Get(); }
        ID3D12Resource* GetDepthResource() const { return depthTex_.Get(); }

        ID3D12DescriptorHeap* GetSrvHeap() const { return srvHeap_.Get(); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpu() const { return srvGpuHandle_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetDepthSrvGpu() const { return depthSrvGpuHandle_; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const { return rtvHandle_; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const { return dsvHandle_; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetReadOnlyDsvHandle() const { return readOnlyDsvHandle_; }

    private:
        bool CreateResources();
        void TransitionDepth(D3D12_RESOURCE_STATES nextState);

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> colorTex_;
        D3D12_RESOURCE_STATES colorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        Microsoft::WRL::ComPtr<ID3D12Resource> depthTex_;
        D3D12_RESOURCE_STATES depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsvHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE depthSrvGpuHandle_{};
        UINT dsvDescriptorSize_{};

        D3D12_VIEWPORT viewport_{};
        D3D12_RECT scissorRect_{};

        int width_ = 0;
        int height_ = 0;
        DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
        bool initialized_ = false;
        bool hasDepth_ = false;
        bool publishDepthSrv_ = true;
        bool allowUnorderedAccess_ = false;
        std::string debugName_ = "RenderTarget2D";
        HIKARI::GFX::Context context_{};

        std::array<float, 4> optimizedClearColor_ = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

} // namespace HIKARI
