#include "Gfx/HIKARI_PresentationSurface.h"

#include <algorithm>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ResourceStateTracker.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI::GFX {

    bool PresentationSurface::Initialize(
        IDXGIFactory7* factory,
        ID3D12CommandQueue* queue,
        ID3D12Device* device,
        HWND window,
        int width,
        int height,
        UINT swapChainFlags,
        const wchar_t* debugName,
        const SwapChainCreatedCallback& onSwapChainCreated,
        ResourceStateTracker& resourceStates) {
        if (factory == nullptr || queue == nullptr || device == nullptr ||
            window == nullptr || width <= 0 || height <= 0 || IsValid()) {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kBufferCount;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Flags = swapChainFlags;

        ComPtr<IDXGISwapChain1> baseSwapChain;
        const HRESULT createResult = factory->CreateSwapChainForHwnd(
            queue,
            window,
            &desc,
            nullptr,
            nullptr,
            &baseSwapChain);
        if (!HIKARI_DX_CHECK(
                createResult,
                "PresentationSurface::Initialize CreateSwapChainForHwnd")) {
            return false;
        }

        ComPtr<IDXGISwapChain4> swapChain;
        const HRESULT castResult = baseSwapChain.As(&swapChain);
        if (!HIKARI_DX_CHECK(
                castResult,
                "PresentationSurface::Initialize Query IDXGISwapChain4")) {
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = kBufferCount;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        const HRESULT heapResult =
            device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap));
        if (!HIKARI_DX_CHECK(
                heapResult,
                "PresentationSurface::Initialize Create RTV heap")) {
            return false;
        }

        window_ = window;
        width_ = width;
        height_ = height;
        debugName_ = debugName != nullptr ? debugName : L"Presentation";
        swapChain_ = std::move(swapChain);
        rtvHeap_ = std::move(rtvHeap);
        rtvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        (void)factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        SetD3D12Name(
            rtvHeap_.Get(),
            (debugName_ + L" RTV Heap").c_str());
        if (onSwapChainCreated) {
            onSwapChainCreated(swapChain_.Get());
        }

        if (!AcquireBackBuffers(device, resourceStates)) {
            Shutdown(resourceStates);
            return false;
        }

        HIKARI_LOG_D3D12(
            std::string("Presentation surface created. size=") +
            std::to_string(width_) + "x" + std::to_string(height_) + ".");
        return true;
    }

    bool PresentationSurface::Resize(
        ID3D12Device* device,
        int width,
        int height,
        UINT swapChainFlags,
        ResourceStateTracker& resourceStates) {
        if (!IsValid() || device == nullptr || width <= 0 || height <= 0) {
            return false;
        }

        ReleaseBackBuffers(resourceStates);
        const HRESULT result = swapChain_->ResizeBuffers(
            kBufferCount,
            static_cast<UINT>(width),
            static_cast<UINT>(height),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            swapChainFlags);
        if (!HIKARI_DX_CHECK(
                result,
                "PresentationSurface::Resize ResizeBuffers")) {
            return false;
        }

        width_ = width;
        height_ = height;
        return AcquireBackBuffers(device, resourceStates);
    }

    void PresentationSurface::ReleaseBackBuffers(
        ResourceStateTracker& resourceStates) {
        for (auto& backBuffer : backBuffers_) {
            if (backBuffer != nullptr) {
                resourceStates.Forget(backBuffer.Get());
                backBuffer.Reset();
            }
        }
    }

    void PresentationSurface::ReleaseSwapChain() {
        swapChain_.Reset();
        window_ = nullptr;
        width_ = 0;
        height_ = 0;
    }

    void PresentationSurface::Shutdown(ResourceStateTracker& resourceStates) {
        ReleaseBackBuffers(resourceStates);
        ReleaseSwapChain();
        rtvHeap_.Reset();
        rtvDescriptorSize_ = 0;
        debugName_.clear();
    }

    uint32_t PresentationSurface::CurrentFrameIndex() const {
        return swapChain_ != nullptr
            ? swapChain_->GetCurrentBackBufferIndex()
            : 0u;
    }

    ID3D12Resource* PresentationSurface::BackBuffer(uint32_t index) const {
        return index < kBufferCount ? backBuffers_[index].Get() : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE PresentationSurface::Rtv(
        uint32_t index) const {
        if (rtvHeap_ == nullptr || index >= kBufferCount) {
            return {};
        }
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(rtvDescriptorSize_) * index;
        return handle;
    }

    bool PresentationSurface::AcquireBackBuffers(
        ID3D12Device* device,
        ResourceStateTracker& resourceStates) {
        if (swapChain_ == nullptr || device == nullptr || rtvHeap_ == nullptr) {
            return false;
        }

        bool complete = true;
        for (uint32_t index = 0; index < kBufferCount; ++index) {
            ComPtr<ID3D12Resource> backBuffer;
            const HRESULT result = swapChain_->GetBuffer(
                index,
                IID_PPV_ARGS(&backBuffer));
            if (!HIKARI_DX_CHECK(
                    result,
                    "PresentationSurface::AcquireBackBuffers GetBuffer")) {
                complete = false;
                continue;
            }

            const std::wstring name = debugName_ + L" BackBuffer[" +
                std::to_wstring(index) + L"]";
            SetD3D12Name(backBuffer.Get(), name.c_str());
            device->CreateRenderTargetView(
                backBuffer.Get(),
                nullptr,
                Rtv(index));
            resourceStates.Track(
                backBuffer.Get(),
                D3D12_RESOURCE_STATE_PRESENT);
            backBuffers_[index] = std::move(backBuffer);
        }
        return complete;
    }

} // namespace HIKARI::GFX
