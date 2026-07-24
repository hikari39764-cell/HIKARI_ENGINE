#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

namespace HIKARI::GFX {

    class ResourceStateTracker;

    class PresentationSurface {
    public:
        static constexpr uint32_t kBufferCount = 3;
        using SwapChainCreatedCallback =
            std::function<void(IDXGISwapChain4*)>;

        bool Initialize(
            IDXGIFactory7* factory,
            ID3D12CommandQueue* queue,
            ID3D12Device* device,
            HWND window,
            int width,
            int height,
            UINT swapChainFlags,
            const wchar_t* debugName,
            const SwapChainCreatedCallback& onSwapChainCreated,
            ResourceStateTracker& resourceStates);

        bool Resize(
            ID3D12Device* device,
            int width,
            int height,
            UINT swapChainFlags,
            ResourceStateTracker& resourceStates);

        void ReleaseBackBuffers(ResourceStateTracker& resourceStates);
        void ReleaseSwapChain();
        void Shutdown(ResourceStateTracker& resourceStates);

        bool IsValid() const { return swapChain_ != nullptr; }
        HWND Window() const { return window_; }
        int Width() const { return width_; }
        int Height() const { return height_; }
        IDXGISwapChain4* SwapChain() const { return swapChain_.Get(); }
        uint32_t CurrentFrameIndex() const;
        ID3D12Resource* BackBuffer(uint32_t index) const;
        D3D12_CPU_DESCRIPTOR_HANDLE Rtv(uint32_t index) const;

    private:
        bool AcquireBackBuffers(
            ID3D12Device* device,
            ResourceStateTracker& resourceStates);

        HWND window_{};
        int width_ = 0;
        int height_ = 0;
        UINT rtvDescriptorSize_ = 0;
        std::wstring debugName_{};
        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_{};
        std::array<
            Microsoft::WRL::ComPtr<ID3D12Resource>,
            kBufferCount> backBuffers_{};
    };

} // namespace HIKARI::GFX
