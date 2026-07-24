#include "HIKARI_Dx12Core.h"
#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuPipelineStatsProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Core/HIKARI_Logger.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI::GFX {

namespace {
	// バックバッファに対するレターボックスの位置とサイズを計算する。座標とサイズはバックバッファに対するピクセル単位。
    struct LetterboxRect {
        float x;
        float y;
        float width;
        float height;
    };
	// 画面サイズとバックバッファサイズから、レターボックスの位置とサイズを計算する。
    static LetterboxRect ComputeLetterboxRect(int backBufferW, int backBufferH) {
        if (backBufferW <= 0 || backBufferH <= 0) {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }

        const float targetAspect =
            static_cast<float>(PRESENTATION::kDefaultLogicalWidth) /
            static_cast<float>(PRESENTATION::kDefaultLogicalHeight);
        const float backBufferAspect = static_cast<float>(backBufferW) / static_cast<float>(backBufferH);

        int vpW = backBufferW;
        int vpH = backBufferH;
        int vpX = 0;
        int vpY = 0;

        if (backBufferAspect > targetAspect) {
            vpW = static_cast<int>(static_cast<float>(backBufferH) * targetAspect + 0.5f);
            vpX = (backBufferW - vpW) / 2;
        } else {
            vpH = static_cast<int>(static_cast<float>(backBufferW) / targetAspect + 0.5f);
            vpY = (backBufferH - vpH) / 2;
        }

        return {
            static_cast<float>(vpX),
            static_cast<float>(vpY),
            static_cast<float>(vpW),
            static_cast<float>(vpH)
        };
    }
}

namespace {
	// エラーコードをログに出力する
void LogHr(const char* stage, HRESULT hr) {
    char buf[256]{};
    std::snprintf(buf, sizeof(buf), "[Dx12Core] %s failed. hr=0x%08lX\n", stage, static_cast<unsigned long>(hr));
    OutputDebugStringA(buf);
    DEBUGLOG::PushRenderError(buf);
    HIKARI_LOG_ERROR(buf);
}

}
// Dx12Core の初期化。失敗した場合は false を返す。
bool Dx12Core::CheckDeviceRemoved(const char* reason, HRESULT hr) {
    if (deviceLost_) {
        return true;
    }

    HRESULT removedReason = S_OK;
    if (device_ != nullptr) {
        removedReason = device_->GetDeviceRemovedReason();
    }

    const bool removed =
        hr == DXGI_ERROR_DEVICE_REMOVED ||
        hr == DXGI_ERROR_DEVICE_RESET ||
        FAILED(removedReason);
    if (!removed) {
        return false;
    }

    deviceLost_ = true;
    frameOpen_ = false;

    std::ostringstream oss;
    oss << "[Dx12Core][FATAL] Device removed. reason="
        << (reason != nullptr ? reason : "")
        << " hr=" << FormatHRESULT(hr)
        << " removedReason=" << FormatHRESULT(removedReason)
        << " text=\"" << HResultToString(removedReason) << "\"";
    DEBUGLOG::PushRenderError(oss.str());
    HIKARI_LOG_ERROR(oss.str());
    DumpD3D12InfoQueue(device_.Get(), reason != nullptr ? reason : "DeviceRemoved");
    return true;
}

bool Dx12Core::Initialize(
    HWND hwnd,
    int w,
    int h,
    bool enableDebugLayer,
    const GraphicsBootstrapCallbacks& callbacks) {
    HIKARI_LOG_D3D12("Dx12Core initialization started.");

    graphicsBootstrapCallbacks_ = callbacks;

    GfxDebugConfig debugConfig = GetGfxDebugConfig();
    debugConfig.enableDebugLayer = enableDebugLayer && debugConfig.enableDebugLayer;
    SetGfxDebugConfig(debugConfig);
	// デバッグレイヤーと GPU ベースのバリデーションを有効にする
    if (debugConfig.enableDebugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            DEBUGLOG::WriteRenderLogLine("[D3D12] Debug Layer enabled.");
            HIKARI_LOG_D3D12("Debug Layer enabled.");

            if (debugConfig.enableGpuBasedValidation) {
                ComPtr<ID3D12Debug3> debug3;
                if (SUCCEEDED(debug.As(&debug3))) {
                    debug3->SetEnableGPUBasedValidation(TRUE);
                    DEBUGLOG::WriteRenderLogLine("[D3D12] GPU-Based Validation enabled.");
                    HIKARI_LOG_D3D12("GPU-Based Validation enabled.");
                }
            }
        }
    }

    UINT factoryFlags = debugConfig.enableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0;
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) {
        // Graphics Tools / DXGI Debug が無い環境では失敗するため、通常 Factory にフォールバックする。
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
        if (FAILED(hr)) {
            LogHr("CreateDXGIFactory2", hr);
            return false;
        }
    }
    HIKARI_LOG_D3D12("DXGI factory created.");

    BOOL allowTearing = FALSE;
    tearingSupported_ = SUCCEEDED(factory_->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING,
        &allowTearing,
        sizeof(allowTearing))) && allowTearing == TRUE;
    swapChainFlags_ = kRequiredSwapChainFlags;
    if (tearingSupported_) {
        swapChainFlags_ |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    for (UINT i = 0; factory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC3 desc{};
        adapter_->GetDesc3(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) break;
    }
    HIKARI_LOG_D3D12("Adapter selected.");

    hr = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) {
        // adapter_ が取れない環境向けフォールバック
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
        if (FAILED(hr)) {
            LogHr("D3D12CreateDevice", hr);
            return false;
        }
    }
    ConfigureD3D12InfoQueue(device_.Get());
    SetD3D12Name(device_.Get(), L"HIKARI D3D12 Device");
    HIKARI_LOG_D3D12("Device created.");
    if (callbacks.deviceCreated) {
        callbacks.deviceCreated(device_.Get());
    }

    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device_->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&queue_));
    if (FAILED(hr)) {
        LogHr("CreateCommandQueue", hr);
        return false;
    }
    SetD3D12Name(queue_.Get(), L"HIKARI Direct Command Queue");
    HIKARI_LOG_D3D12("Command queue created.");

    if (!editorSurface_.Initialize(
            factory_.Get(),
            queue_.Get(),
            device_.Get(),
            hwnd,
            w,
            h,
            swapChainFlags_,
            L"HIKARI Editor Presentation",
            graphicsBootstrapCallbacks_.swapChainCreated,
            resourceStates_)) {
        return false;
    }
    activeSurface_ = &editorSurface_;
    frameIndex_ = activeSurface_->CurrentFrameIndex();

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 2;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hr = device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_));
    if (FAILED(hr)) {
        LogHr("Create DSV Heap", hr);
        return false;
    }
    SetD3D12Name(dsvHeap_.Get(), L"HIKARI Main DSV Heap");
    dsvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    HIKARI_LOG_D3D12("DSV heap created.");

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.NumDescriptors = DESCRIPTOR::kSrvHeapCapacity;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device_->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_));
    if (FAILED(hr)) {
        LogHr("Create SRV Heap", hr);
        return false;
    }
    SetD3D12Name(srvHeap_.Get(), L"HIKARI Global SRV Heap");
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    HIKARI_LOG_D3D12(("SRV heap created. descriptors=" +
        std::to_string(DESCRIPTOR::kSrvHeapCapacity) + ".").c_str());

    if (!CreateDepthBuffer()) {
        HIKARI_LOG_ERROR("Depth buffer creation failed.");
        return false;
    }
    HIKARI_LOG_D3D12("Depth buffer created.");

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators_[i]));
        if (FAILED(hr)) {
            LogHr("CreateCommandAllocator", hr);
            return false;
        }
    }
    HIKARI_LOG_D3D12("Command allocators created.");
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[frameIndex_].Get(), nullptr, IID_PPV_ARGS(&cmdList_));
    if (FAILED(hr)) {
        LogHr("CreateCommandList", hr);
        return false;
    }
    SetD3D12Name(cmdList_.Get(), L"HIKARI Main Command List");
    cmdList_->Close();
    HIKARI_LOG_D3D12("Command list created.");

    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) {
        LogHr("CreateFence", hr);
        return false;
    }
    SetD3D12Name(fence_.Get(), L"HIKARI Frame Fence");
    HIKARI_LOG_D3D12("Fence created.");
    fenceValue_ = 1;
    frameFenceValues_.fill(0);
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        HIKARI_LOG_ERROR("[Dx12Core] CreateEvent failed.");
        return false;
    }
    HIKARI_LOG_D3D12("Dx12Core initialization completed.");
    return true;
}

// スワップチェインのバックバッファを取得し、RTV を作成する
// 深度バッファを作成し、DSV と SRV を作成する
// Game presentation owns a temporary swap chain while the editor surface stays resident.
bool Dx12Core::CreateGamePresentationSurface(HWND hwnd, int w, int h) {
    if (hwnd == nullptr || w <= 0 || h <= 0 || deviceLost_ || frameOpen_ ||
        activeSurface_ != &editorSurface_ || gameSurface_.IsValid()) {
        return false;
    }
    if (!WaitGPU()) {
        return false;
    }
    if (!gameSurface_.Initialize(
            factory_.Get(), queue_.Get(), device_.Get(), hwnd, w, h,
            swapChainFlags_, L"HIKARI Game Presentation",
            graphicsBootstrapCallbacks_.swapChainCreated, resourceStates_)) {
        return false;
    }
    if (ActivateSurface(gameSurface_)) {
        HIKARI_LOG_INFO("Game presentation surface activated.");
        return true;
    }

    gameSurface_.Shutdown(resourceStates_);
    (void)ActivateSurface(editorSurface_);
    return false;
}

bool Dx12Core::ReleaseGamePresentationSurface() {
    if (deviceLost_ || frameOpen_ || activeSurface_ != &gameSurface_) {
        return false;
    }
    if (!WaitGPU()) {
        return false;
    }

    ReleaseDepthBuffer();
    gameSurface_.Shutdown(resourceStates_);
    activeSurface_ = nullptr;
    frameIndex_ = 0;
    frameFenceValues_.fill(0);
    HIKARI_LOG_INFO("Game presentation surface released.");
    return true;
}

bool Dx12Core::ActivateEditorPresentationSurface() {
    if (deviceLost_ || frameOpen_ || !editorSurface_.IsValid() ||
        gameSurface_.IsValid()) {
        return false;
    }
    if (!WaitGPU()) {
        return false;
    }
    if (!ActivateSurface(editorSurface_)) {
        return false;
    }
    HIKARI_LOG_INFO("Editor presentation surface reactivated.");
    return true;
}

bool Dx12Core::ActivateSurface(PresentationSurface& surface) {
    if (!surface.IsValid()) {
        return false;
    }
    ReleaseDepthBuffer();
    activeSurface_ = &surface;
    frameIndex_ = surface.CurrentFrameIndex();
    frameFenceValues_.fill(0);
    if (CreateDepthBuffer()) {
        return true;
    }
    activeSurface_ = nullptr;
    frameIndex_ = 0;
    return false;
}

void Dx12Core::ReleaseDepthBuffer() {
    if (depthBuffer_ != nullptr) {
        resourceStates_.Forget(depthBuffer_.Get());
        depthBuffer_.Reset();
    }
    sceneDepthSrvCpu_ = {};
    sceneDepthSrvGpu_ = {};
}

bool Dx12Core::CreateDepthBuffer() {
    sceneDepthSrvCpu_ = {};
    sceneDepthSrvGpu_ = {};
    if (deviceLost_ || device_ == nullptr || dsvHeap_ == nullptr || srvHeap_ == nullptr) {
        return false;
    }

    if (activeSurface_ == nullptr) {
        return false;
    }
    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_TYPELESS,
        static_cast<UINT64>(activeSurface_->Width()),
        static_cast<UINT>(activeSurface_->Height()),
        1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depthBuffer_));
    if (!HIKARI_DX_CHECK(hr, "Dx12Core::CreateDepthBuffer")) {
        CheckDeviceRemoved("Dx12Core::CreateDepthBuffer", hr);
        depthBuffer_.Reset();
        return false;
    }
    SetD3D12Name(depthBuffer_.Get(), L"HIKARI Main Depth Buffer");
    resourceStates_.Track(depthBuffer_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depthBuffer_.Get(), &dsv, dsvHeap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    readOnlyDsvHandle.ptr += dsvDescriptorSize_;

    D3D12_DEPTH_STENCIL_VIEW_DESC readOnlyDsv{};
    readOnlyDsv.Format = DXGI_FORMAT_D32_FLOAT;
    readOnlyDsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    readOnlyDsv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    device_->CreateDepthStencilView(depthBuffer_.Get(), &readOnlyDsv, readOnlyDsvHandle);

    const UINT sceneDepthSrvIndex =
        DESCRIPTOR::ToIndex(DESCRIPTOR::SystemSrv::SceneDepth);

    sceneDepthSrvCpu_ =
        DESCRIPTOR::CpuAt(srvHeap_.Get(), srvDescriptorSize_, sceneDepthSrvIndex);
    sceneDepthSrvGpu_ =
        DESCRIPTOR::GpuAt(srvHeap_.Get(), srvDescriptorSize_, sceneDepthSrvIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;

    device_->CreateShaderResourceView(depthBuffer_.Get(), &srv, sceneDepthSrvCpu_);
    return true;
}
// Dx12Core の終了処理。GPU の完了を待ち、リソースを解放する。
void Dx12Core::Shutdown() {
    WaitGPU();
    GPU_PIPELINE_STATS::Shutdown();
    GPU_PROFILE::Shutdown();
    deferredReleaseQueue_.FlushAll();
    ReleaseDepthBuffer();
    gameSurface_.Shutdown(resourceStates_);
    editorSurface_.Shutdown(resourceStates_);
    activeSurface_ = nullptr;
    if (fenceEvent_) CloseHandle(fenceEvent_);
    fenceEvent_ = nullptr;
}
// フレームの開始処理
bool Dx12Core::BeginFrame(float clearR, float clearG, float clearB, float clearA) {
    frameOpen_ = false;
    if (deviceLost_ ||
        cmdList_ == nullptr ||
        frameIndex_ >= kFrameCount ||
        allocators_[frameIndex_] == nullptr ||
        CurrentBackBuffer() == nullptr) {
        return false;
    }

    HRESULT hr = allocators_[frameIndex_]->Reset();
    if (FAILED(hr)) {
        HIKARI_DX_CHECK(hr, "Dx12Core::BeginFrame ResetAllocator");
        CheckDeviceRemoved("Dx12Core::BeginFrame ResetAllocator", hr);
        return false;
    }
    hr = cmdList_->Reset(allocators_[frameIndex_].Get(), nullptr);
    if (FAILED(hr)) {
        HIKARI_DX_CHECK(hr, "Dx12Core::BeginFrame ResetCommandList");
        CheckDeviceRemoved("Dx12Core::BeginFrame ResetCommandList", hr);
        return false;
    }
    const GfxDebugConfig& debugConfig = GetGfxDebugConfig();
    const bool allowGpuProfiler =
        debugConfig.enableGpuFrameProfiler &&
        (!debugConfig.enableDebugLayer || debugConfig.enableGpuFrameProfilerWithDebugLayer);
    // Debug Layer 中は timestamp query を切り離し、検証ログを純化する。
    GPU_PROFILE::SetEnabled(
        allowGpuProfiler,
        debugConfig.enableDebugLayer ?
            "GPU profiler is disabled while D3D12 Debug Layer is enabled." :
            "GPU profiler is disabled by GfxDebugConfig.");
    GPU_PIPELINE_STATS::SetEnabled(
        allowGpuProfiler,
        debugConfig.enableDebugLayer ?
            "Pipeline stats profiler is disabled while D3D12 Debug Layer is enabled." :
            "Pipeline stats profiler is disabled by GfxDebugConfig.");
    GPU_PROFILE::BeginFrame(device_.Get(), queue_.Get(), cmdList_.Get(), frameIndex_);
    GPU_PIPELINE_STATS::BeginFrame(device_.Get(), cmdList_.Get(), frameIndex_);
    PIX::BeginGpuEvent(cmdList_.Get(), PIX::kColorFrame, "Frame");

    resourceStates_.Transition(
        cmdList_.Get(),
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto rtv = CurrentRTV();
    if (rtv.ptr == 0) {
        return false;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    if (depthBuffer_ != nullptr) {
        dsv = DSV();
    }
    cmdList_->OMSetRenderTargets(1, &rtv, FALSE, depthBuffer_ != nullptr ? &dsv : nullptr);

    const int presentationWidth = activeSurface_->Width();
    const int presentationHeight = activeSurface_->Height();
    const auto letterbox = ComputeLetterboxRect(
        presentationWidth,
        presentationHeight);

    const float black[] = { 0.0f, 0.0f, 0.0f, clearA };
    cmdList_->ClearRenderTargetView(rtv, black, 0, nullptr);

    const float color[] = { clearR, clearG, clearB, clearA };
    const bool hasBlackBars =
        (letterbox.x > 0.0f) || (letterbox.y > 0.0f) ||
        (letterbox.width < static_cast<float>(presentationWidth)) ||
        (letterbox.height < static_cast<float>(presentationHeight));

    if (!hasBlackBars) {
        cmdList_->ClearRenderTargetView(rtv, color, 0, nullptr);
    }

    if (depthBuffer_ != nullptr) {
        cmdList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }

    D3D12_VIEWPORT vp{ letterbox.x, letterbox.y, letterbox.width, letterbox.height, 0, 1 };
    D3D12_RECT sc{
        static_cast<LONG>(letterbox.x),
        static_cast<LONG>(letterbox.y),
        static_cast<LONG>(letterbox.x + letterbox.width),
        static_cast<LONG>(letterbox.y + letterbox.height)
    };
    cmdList_->RSSetViewports(1, &vp);
    cmdList_->RSSetScissorRects(1, &sc);
    frameOpen_ = true;
    return true;
}
// フレームの終了処理
bool Dx12Core::EndFrame() {
    if (!frameOpen_ || deviceLost_ || cmdList_ == nullptr || CurrentBackBuffer() == nullptr) {
        frameOpen_ = false;
        return false;
    }

    GPU_PIPELINE_STATS::EndFrame(cmdList_.Get());
    GPU_PROFILE::EndFrame(cmdList_.Get());
    resourceStates_.Transition(
        cmdList_.Get(),
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT);
    PIX::EndGpuEvent(cmdList_.Get());

    HRESULT hr = S_OK;
    {
        CPU_PROFILE::ScopedCpuTimer cpuSubmit(
            CPU_PROFILE::Pass::CommandSubmit);
        hr = cmdList_->Close();
        if (FAILED(hr)) {
            frameOpen_ = false;
            HIKARI_DX_CHECK(hr, "Dx12Core::EndFrame CloseCommandList");
            CheckDeviceRemoved("Dx12Core::EndFrame CloseCommandList", hr);
            return false;
        }
        ID3D12CommandList* lists[] = { cmdList_.Get() };
        if (frameSubmissionCallbacks_.renderSubmitStart) {
            frameSubmissionCallbacks_.renderSubmitStart();
        }
        queue_->ExecuteCommandLists(1, lists);
        if (frameSubmissionCallbacks_.renderSubmitEnd) {
            frameSubmissionCallbacks_.renderSubmitEnd();
        }
    }
    {
        CPU_PROFILE::ScopedCpuTimer cpuPresent(
            CPU_PROFILE::Pass::Present);
        if (frameSubmissionCallbacks_.presentStart) {
            frameSubmissionCallbacks_.presentStart();
        }
        const UINT syncInterval = vSyncEnabled_ ? 1u : 0u;
        const UINT presentFlags =
            !vSyncEnabled_ && tearingSupported_
            ? DXGI_PRESENT_ALLOW_TEARING
            : 0u;
        hr = activeSurface_->SwapChain()->Present(syncInterval, presentFlags);
        if (frameSubmissionCallbacks_.presentEnd) {
            frameSubmissionCallbacks_.presentEnd();
        }
    }
    if (FAILED(hr)) {
        frameOpen_ = false;
        HIKARI_DX_CHECK(hr, "Dx12Core::EndFrame Present");
        DumpDxgiInfoQueue("Dx12Core::EndFrame Present");
        DumpD3D12InfoQueue(device_.Get(), "Dx12Core::EndFrame Present");
        CheckDeviceRemoved("Dx12Core::EndFrame Present", hr);
        return false;
    }

    if (GetGfxDebugConfig().dumpInfoQueueOnFrameEnd) {
        DumpD3D12InfoQueue(device_.Get(), "EndFrame");
        ClearD3D12InfoQueue(device_.Get());
    }

    frameOpen_ = false;
    return MoveToNextFrame();
}
// GPU の完了を待ち、GPU が使用しているリソースの解放を行う
bool Dx12Core::WaitGPU() {
    if (deviceLost_ || queue_ == nullptr || fence_ == nullptr) {
        return false;
    }

    const uint64_t signal = fenceValue_;
    const HRESULT signalHr = queue_->Signal(fence_.Get(), signal);
    if (FAILED(signalHr)) {
        HIKARI_DX_CHECK(signalHr, "Dx12Core::WaitGPU Signal");
        CheckDeviceRemoved("Dx12Core::WaitGPU Signal", signalHr);
        return false;
    }
    fenceValue_++;
    if (fence_->GetCompletedValue() < signal) {
        const HRESULT eventHr = fence_->SetEventOnCompletion(signal, fenceEvent_);
        if (FAILED(eventHr)) {
            HIKARI_DX_CHECK(eventHr, "Dx12Core::WaitGPU SetEventOnCompletion");
            CheckDeviceRemoved("Dx12Core::WaitGPU SetEventOnCompletion", eventHr);
            return false;
        }
        CPU_PROFILE::ScopedCpuTimer cpuWait(
            CPU_PROFILE::Pass::FenceWait);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    deferredReleaseQueue_.Collect(fence_->GetCompletedValue());
    return true;
}
// フレームを進める。現在のフレームの完了を待ち、次のフレームのバックバッファを取得する。
bool Dx12Core::MoveToNextFrame() {
    if (deviceLost_ || queue_ == nullptr || fence_ == nullptr ||
        activeSurface_ == nullptr || !activeSurface_->IsValid()) {
        return false;
    }

    const uint32_t submittedFrameIndex = frameIndex_;
    const uint64_t signal = fenceValue_;

    const HRESULT signalHr = queue_->Signal(fence_.Get(), signal);
    if (FAILED(signalHr)) {
        HIKARI_DX_CHECK(signalHr, "Dx12Core::MoveToNextFrame Signal");
        CheckDeviceRemoved("Dx12Core::MoveToNextFrame Signal", signalHr);
        return false;
    }

    frameFenceValues_[submittedFrameIndex] = signal;
    fenceValue_++;

    frameIndex_ = activeSurface_->CurrentFrameIndex();

    // Frame resources are triple-buffered; only wait when the next swapchain
    // back buffer would reuse resources whose submitted work is still pending.
    const uint64_t frameFenceToWait = frameFenceValues_[frameIndex_];
    if (frameFenceToWait != 0 &&
        fence_->GetCompletedValue() < frameFenceToWait) {
        const HRESULT eventHr =
            fence_->SetEventOnCompletion(frameFenceToWait, fenceEvent_);
        if (FAILED(eventHr)) {
            HIKARI_DX_CHECK(eventHr, "Dx12Core::MoveToNextFrame SetEventOnCompletion");
            CheckDeviceRemoved("Dx12Core::MoveToNextFrame SetEventOnCompletion", eventHr);
            return false;
        }

        CPU_PROFILE::ScopedCpuTimer cpuWait(
            CPU_PROFILE::Pass::FenceWait);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    deferredReleaseQueue_.Collect(fence_->GetCompletedValue());
    return true;
}// ウィンドウサイズの変更に伴うリソースの再作成。GPU の完了を待ち、古いリソースを解放してから、新しいスワップチェインのバッファと深度バッファを作成する。
bool Dx12Core::Resize(int w, int h) {
    if (w <= 0 || h <= 0) return true;
    if (deviceLost_ || activeSurface_ == nullptr || !activeSurface_->IsValid()) {
        return false;
    }
    if (frameOpen_) {
        HIKARI_LOG_ERROR("Dx12Core::Resize was requested while a GPU frame is open. Resize must be deferred to a frame boundary.");
        return false;
    }
    if (!WaitGPU()) {
        return false;
    }

    ReleaseDepthBuffer();
    if (!activeSurface_->Resize(
            device_.Get(),
            w,
            h,
            swapChainFlags_,
            resourceStates_)) {
        return false;
    }
    frameIndex_ = activeSurface_->CurrentFrameIndex();
    frameFenceValues_.fill(0);
    return CreateDepthBuffer();
}

bool Dx12Core::WaitForIdle() {
    return WaitGPU();
}

HWND Dx12Core::PresentationWindow() const {
    return activeSurface_ != nullptr ? activeSurface_->Window() : nullptr;
}
// 現在のフレームの RTV ハンドルを取得する
D3D12_CPU_DESCRIPTOR_HANDLE Dx12Core::CurrentRTV() const {
    if (activeSurface_ == nullptr || frameIndex_ >= kFrameCount) {
        return {};
    }
    return activeSurface_->Rtv(frameIndex_);
}
// DSV ハンドルを取得する
D3D12_CPU_DESCRIPTOR_HANDLE Dx12Core::DSV() const {
    if (depthBuffer_ == nullptr || dsvHeap_ == nullptr) {
        return {};
    }
    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}
// 読み取り専用 DSV ハンドルを取得する
D3D12_CPU_DESCRIPTOR_HANDLE Dx12Core::ReadOnlyDSV() const {
    if (depthBuffer_ == nullptr || dsvHeap_ == nullptr) {
        return {};
    }
    auto handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += dsvDescriptorSize_;
    return handle;
}
// 深度バッファの SRV ハンドルを取得する
D3D12_GPU_DESCRIPTOR_HANDLE Dx12Core::SceneDepthSrv() const {
    return sceneDepthSrvGpu_;
}
// 深度バッファのリソースを取得する
ID3D12Resource* Dx12Core::SceneDepthResource() const {
    return depthBuffer_.Get();
}
// 現在のフレームのバックバッファリソースを取得する
ID3D12Resource* Dx12Core::CurrentBackBuffer() {
    if (activeSurface_ == nullptr || frameIndex_ >= kFrameCount) {
        return nullptr;
    }
    return activeSurface_->BackBuffer(frameIndex_);
}
// GPU による遅延解放の保留数を取得する
size_t Dx12Core::GetPendingDeferredReleaseCount() const {
    return deferredReleaseQueue_.GetPendingCount();
}
// GPU fence の完了状態をポーリングする。
bool Dx12Core::IsFenceComplete(uint64_t fenceValue) const {
    return fence_ != nullptr && fence_->GetCompletedValue() >= fenceValue;
}
// 現在のコンテキストを構築して返す。Context には、コマンドリストやリソースのハンドルなど、描画に必要な情報が含まれる。
Context Dx12Core::BuildContext() const {
    Context ctx{};
    if (deviceLost_) {
        return ctx;
    }
    ctx.device = device_.Get();
    ctx.cmdList = cmdList_.Get();
    ctx.queue = queue_.Get();
    ctx.srvHeap = srvHeap_.Get();
    ctx.rtv = CurrentRTV();
    ctx.dsv = DSV();
    ctx.readOnlyDsv = ReadOnlyDSV();
    ctx.sceneDepthSrvCpu = sceneDepthSrvCpu_;
    ctx.sceneDepthSrv = SceneDepthSrv();
    ctx.sceneDepthResource = SceneDepthResource();
    ctx.resourceStates = const_cast<ResourceStateTracker*>(&resourceStates_);
    ctx.deferredReleaseQueue = const_cast<GpuDeferredReleaseQueue*>(&deferredReleaseQueue_);
    ctx.currentFrameRetireFenceValue = fenceValue_;
    ctx.frameIndex = frameIndex_;
    ctx.backBufferWidth = activeSurface_ != nullptr
        ? static_cast<uint32_t>(activeSurface_->Width())
        : 0u;
    ctx.backBufferHeight = activeSurface_ != nullptr
        ? static_cast<uint32_t>(activeSurface_->Height())
        : 0u;
    return ctx;
}

} // namespace HIKARI::GFX
