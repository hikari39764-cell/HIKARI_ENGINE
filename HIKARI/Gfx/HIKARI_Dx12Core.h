#pragma once

#include "HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_PresentationSurface.h"
#include "Gfx/HIKARI_ResourceStateTracker.h"

#include <array>
#include <functional>
#include <cstddef>
#include <utility>
#include <wrl.h>
#include <dxgi1_6.h>

namespace HIKARI::GFX {

struct FrameSubmissionCallbacks {
    std::function<void()> renderSubmitStart{};
    std::function<void()> renderSubmitEnd{};
    std::function<void()> presentStart{};
    std::function<void()> presentEnd{};
};

struct GraphicsBootstrapCallbacks {
    std::function<void(ID3D12Device*)> deviceCreated{};
    std::function<void(IDXGISwapChain4*)> swapChainCreated{};
};

class Dx12Core {
public:
    static constexpr uint32_t kFrameCount = 3;
    static constexpr UINT kRequiredSwapChainFlags =
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    bool Initialize(
        HWND hwnd,
        int w,
        int h,
        bool enableDebugLayer,
        const GraphicsBootstrapCallbacks& callbacks = {});
    void Shutdown();

    bool BeginFrame(float clearR, float clearG, float clearB, float clearA);
    bool EndFrame();
    bool Resize(int w, int h);
    bool CreateGamePresentationSurface(HWND hwnd, int w, int h);
    bool ReleaseGamePresentationSurface();
    bool ActivateEditorPresentationSurface();
    bool WaitForIdle();

    uint32_t FrameIndex() const { return frameIndex_; }
    ID3D12Device* Device() { return device_.Get(); }
    ID3D12GraphicsCommandList* CmdList() { return cmdList_.Get(); }
    ID3D12CommandQueue* Queue() { return queue_.Get(); }
    ID3D12DescriptorHeap* SrvHeap() { return srvHeap_.Get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DSV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE ReadOnlyDSV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE SceneDepthSrv() const;
    ID3D12Resource* SceneDepthResource() const;
    ID3D12Resource* CurrentBackBuffer();
    size_t GetPendingDeferredReleaseCount() const;
    bool IsFenceComplete(uint64_t fenceValue) const;
    bool IsDeviceLost() const { return deviceLost_; }
    void SetVSyncEnabled(bool enabled) { vSyncEnabled_ = enabled; }
    bool IsVSyncEnabled() const { return vSyncEnabled_; }
    HWND PresentationWindow() const;
    bool IsGamePresentationSurfaceActive() const {
        return activeSurface_ == &gameSurface_;
    }
    bool HasGamePresentationSurface() const { return gameSurface_.IsValid(); }
    void SetFrameSubmissionCallbacks(FrameSubmissionCallbacks callbacks) {
        frameSubmissionCallbacks_ = std::move(callbacks);
    }

    Context BuildContext() const;

private:
    bool WaitGPU();
    bool MoveToNextFrame();
    bool ActivateSurface(PresentationSurface& surface);
    void ReleaseDepthBuffer();
    bool CreateDepthBuffer();
    bool CheckDeviceRemoved(const char* reason, HRESULT hr);

private:
    Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> allocators_{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList_;
    PresentationSurface editorSurface_{};
    PresentationSurface gameSurface_{};
    PresentationSurface* activeSurface_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    UINT dsvDescriptorSize_{};
    UINT srvDescriptorSize_{};
    D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSrvCpu_{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrvGpu_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
    ResourceStateTracker resourceStates_;
    GpuDeferredReleaseQueue deferredReleaseQueue_;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	std::array<uint64_t, kFrameCount> frameFenceValues_{};
    uint64_t fenceValue_{};
    HANDLE fenceEvent_{};
    uint32_t frameIndex_{};
    bool deviceLost_ = false;
    bool frameOpen_ = false;
    bool vSyncEnabled_ = false;
    bool tearingSupported_ = false;
    UINT swapChainFlags_ = kRequiredSwapChainFlags;
    FrameSubmissionCallbacks frameSubmissionCallbacks_{};
    GraphicsBootstrapCallbacks graphicsBootstrapCallbacks_{};
};

} // namespace HIKARI::GFX
