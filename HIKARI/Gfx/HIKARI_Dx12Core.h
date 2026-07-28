#pragma once

#include "HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_PresentationSurface.h"
#include "Gfx/HIKARI_ResourceStateTracker.h"

#include <array>
#include <functional>
#include <utility>
#include <wrl.h>
#include <dxgi1_6.h>

namespace HIKARI::GFX {

// フレーム送出の各段階で呼ばれるコールバック群。
// Editor やプロファイラがフレームの開始/終了、Present の前後に処理を挿入するために使う。
struct FrameSubmissionCallbacks {
    std::function<void()> renderSubmitStart{};
    std::function<void()> renderSubmitEnd{};
    std::function<void()> presentStart{};
    std::function<void()> presentEnd{};
};

// Dx12Core 初期化時に Device / SwapChain の生成完了を外部へ通知するコールバック。
struct GraphicsBootstrapCallbacks {
    std::function<void(ID3D12Device*)> deviceCreated{};
    std::function<void(IDXGISwapChain4*)> swapChainCreated{};
};

// DX12 デバイス・スワップチェーン・フレームループを統括する中核クラス。
// 3 重バッファリング（kFrameCount=3）で CPU/GPU の並行動作を実現し、
// Editor 用と Game 用の二つの PresentationSurface の切り替えを管理する。

class Dx12Core {
public:
    static constexpr uint32_t kFrameCount = 3;
    static constexpr UINT kRequiredSwapChainFlags =
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    // ── ライフサイクル ──────────────────────────────────────────

    bool Initialize(
        HWND hwnd,
        int w,
        int h,
        bool enableDebugLayer,
        const GraphicsBootstrapCallbacks& callbacks = {});
    void Shutdown();

    // ── フレームループ ──────────────────────────────────────────

    bool BeginFrame(float clearR, float clearG, float clearB, float clearA);
    bool EndFrame();

    // ── リサイズ ─────────────────────────────────────────────────

    bool Resize(int w, int h);

    // ── 表示サーフェス切替（Editor / Game） ─────────────────────

    bool CreateGamePresentationSurface(HWND hwnd, int w, int h);
    bool ReleaseGamePresentationSurface();
    bool ActivateEditorPresentationSurface();

    // ── 同期 ────────────────────────────────────────────────────

    bool WaitForIdle();

    // ── 基本アクセサ ───────────────────────────────────────────

    uint32_t FrameIndex() const { return frameIndex_; }
    ID3D12Device* Device() { return device_.Get(); }
    ID3D12GraphicsCommandList* CmdList() { return cmdList_.Get(); }
    ID3D12CommandQueue* Queue() { return queue_.Get(); }
    ID3D12DescriptorHeap* SrvHeap() { return srvHeap_.Get(); }

    // ── 深度バッファ / RTV ハンドル ────────────────────────────

    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DSV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE ReadOnlyDSV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE SceneDepthSrv() const;
    ID3D12Resource* SceneDepthResource() const;
    ID3D12Resource* CurrentBackBuffer();

    // ── 状態 / 設定 ─────────────────────────────────────────────

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

    // ── 他モジュール向けコンテキスト構築 ──────────────────────

    Context BuildContext() const;

private:
    // ── 内部メソッド ──────────────────────────────────────────────
    bool WaitGPU();
    bool MoveToNextFrame();
    bool ActivateSurface(PresentationSurface& surface);
    void ReleaseDepthBuffer();
    bool CreateDepthBuffer();
    bool CheckDeviceRemoved(const char* reason, HRESULT hr);

    // ── DXGI / Device 基盤 ───────────────────────────────────────
    Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;

    // ── コマンドリスト・フレーム管理 ─────────────────────────────
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> allocators_{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    std::array<uint64_t, kFrameCount> frameFenceValues_{};
    uint64_t fenceValue_{};
    HANDLE fenceEvent_{};
    uint32_t frameIndex_{};

    // ── 表示サーフェス ────────────────────────────────────────────
    PresentationSurface editorSurface_{};
    PresentationSurface gameSurface_{};
    PresentationSurface* activeSurface_ = nullptr;

    // ── デスクリプタヒープ ────────────────────────────────────────
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    UINT dsvDescriptorSize_{};
    UINT srvDescriptorSize_{};
    D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSrvCpu_{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrvGpu_{};

    // ── 深度バッファ ──────────────────────────────────────────────
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;

    // ── 状態追跡 ──────────────────────────────────────────────────
    ResourceStateTracker resourceStates_;
    GpuDeferredReleaseQueue deferredReleaseQueue_;

    // ── フラグ類 ──────────────────────────────────────────────────
    bool deviceLost_ = false;
    bool frameOpen_ = false;
    bool vSyncEnabled_ = false;
    bool tearingSupported_ = false;
    UINT swapChainFlags_ = kRequiredSwapChainFlags;

    // ── コールバック保持 ──────────────────────────────────────────
    FrameSubmissionCallbacks frameSubmissionCallbacks_{};
    GraphicsBootstrapCallbacks graphicsBootstrapCallbacks_{};
};

} // namespace HIKARI::GFX
