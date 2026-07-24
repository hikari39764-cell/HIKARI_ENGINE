#include "Render3D/GpuDriven/HIKARI_GpuDepthVisibilityLayer.h"

#include <algorithm>
#include <string>

#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
		// 深度バッファの SRV デスクリプタを作成する。リソースが nullptr の場合は無効な RenderResourceView を返す。
        RenderResourceView CreateVisibilityDepthSrvDescriptor(
            ID3D12Resource* resource,
            DXGI_FORMAT format) {

            if (resource == nullptr) {
                return {};
            }

            RenderResourceView view = DEPTH::AllocateDepthPyramidTransientDescriptor();
            if (!view.IsValid() || view.cpu.ptr == 0 || view.gpu.ptr == 0) {
                return {};
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            SERVICES::gCtx.device->CreateShaderResourceView(resource, &srvDesc, view.cpu);
            return view;
        }
    }
	//  フレームのリセット処理。PSO やリソースの状態を保持する。
    void GpuDepthVisibilityLayer::ResetFrame() {
        const D3D12_GPU_DESCRIPTOR_HANDLE visibilityDepthSrv = visibilityDepthSrv_.gpu;
        depthPyramid_.ResetFrame();
        const DEPTH::DepthPyramidStats& pyramidStats = depthPyramid_.GetStats();
        const DEPTH::DepthPyramidView& pyramidView = pyramidStats.currentView;

        stats_ = {};
        stats_.psoReady = pyramidStats.psoReady;
        stats_.resourcesReady =
            pyramidStats.resourcesReady && visibilityDepth_ != nullptr;
        stats_.width =
            pyramidView.sourceWidth != 0 ? pyramidView.sourceWidth : depthWidth_;
        stats_.height =
            pyramidView.sourceHeight != 0 ? pyramidView.sourceHeight : depthHeight_;
        stats_.visibilityDepthReady =
            visibilityDepth_ != nullptr && visibilityDepthSrv.ptr != 0;
        stats_.visibilityDepthSrv = visibilityDepthSrv;
        PublishDepthPyramidStats(pyramidView);
        stats_.hzbBuilt = false;
    }
	// 深度リソースを解放する。SRV デスクリプタと DSV デスクリプタも解放する。
    void GpuDepthVisibilityLayer::Release() {
        depthPyramid_.Release();
        ReleaseDepthResource();
        depthWidth_ = 0;
        depthHeight_ = 0;
        stats_ = {};
    }
	// 深度プリパスの書き込み状態を記録する。書き込まれた場合は true、そうでない場合は false を設定する。
    void GpuDepthVisibilityLayer::RecordDepthPrepass(bool written) {
        stats_.depthPrepassWritten = written;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GpuDepthVisibilityLayer::BeginDepthPrepass(
        ID3D12GraphicsCommandList* cmd,
        uint32_t width,
        uint32_t height) {

        if (cmd == nullptr || !EnsureDepthResource(width, height)) {
            return {};
        }

        TransitionDepth(cmd, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmd->ClearDepthStencilView(
            visibilityDsv_,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            0,
            nullptr);

        stats_.visibilityDepthReady = visibilityDepthSrv_.gpu.ptr != 0;
        stats_.visibilityDepthSrv = visibilityDepthSrv_.gpu;
        return visibilityDsv_;
    }
	// 深度プリパスの結果から深度ピラミッドを構築する。視錐台行列を指定することで、深度ピラミッドのビュー行列を設定する、共有リリースキューを使用して、GPU による遅延解放を行う。
    bool GpuDepthVisibilityLayer::BuildDepthPyramidFromVisibilityPrepass(
        ID3D12GraphicsCommandList* cmd,
        uint32_t width,
        uint32_t height,
        const MATH::Mat4& viewProj) {

        stats_.visibilityDepthReady =
            visibilityDepth_ != nullptr &&
            visibilityDepthSrv_.gpu.ptr != 0 &&
            width != 0 &&
            height != 0;
        stats_.visibilityDepthSrv = visibilityDepthSrv_.gpu;
        if (cmd == nullptr || !stats_.visibilityDepthReady) {
            PublishDepthPyramidStats({});
            stats_.hzbBuilt = false;
            return false;
        }

        TransitionDepth(
            cmd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        DEPTH::DepthPyramidBuildDesc desc{};
        desc.commandList = cmd;
        desc.sourceWidth = width;
        desc.sourceHeight = height;
        desc.sourceDepthSrv = visibilityDepthSrv_.gpu;
        desc.sourceKind = DEPTH::DepthPyramidSourceKind::VisibilityPrepass;
        desc.viewKind = DEPTH::DepthPyramidViewKind::CurrentFrame;
        desc.viewProj = viewProj;
        desc.viewProjValid = true;
        desc.jitteredViewProj = false;
        desc.frameIndex = SERVICES::gCtx.frameIndex;
        desc.pixEventName = "DepthPyramid.Build.VisibilityPrepass";

        const bool built = depthPyramid_.BuildFromDepthSrv(desc);
        stats_.hzbBuildRequested = depthPyramid_.GetStats().buildRequested;
        stats_.hzbBuilt = built;
        PublishDepthPyramidStats(depthPyramid_.GetCurrentView());
        return built;
    }
	// 深度 SRV から深度ピラミッドを構築する。視錐台行列を指定することで、深度ピラミッドのビュー行列を設定する。
    bool GpuDepthVisibilityLayer::BuildDepthPyramidFromDepthSrv(
        ID3D12GraphicsCommandList* cmd,
        uint32_t width,
        uint32_t height,
        D3D12_GPU_DESCRIPTOR_HANDLE sourceDepthSrv,
        const MATH::Mat4& viewProj) {

        stats_.visibilityDepthReady =
            sourceDepthSrv.ptr != 0 &&
            width != 0 &&
            height != 0;
        stats_.visibilityDepthSrv = sourceDepthSrv;
        DEPTH::DepthPyramidBuildDesc desc{};
        desc.commandList = cmd;
        desc.sourceWidth = width;
        desc.sourceHeight = height;
        desc.sourceDepthSrv = sourceDepthSrv;
        desc.sourceKind = DEPTH::DepthPyramidSourceKind::SceneDepth;
        desc.viewKind = DEPTH::DepthPyramidViewKind::CurrentFrame;
        desc.viewProj = viewProj;
        desc.viewProjValid = true;
        desc.jitteredViewProj = false;
        desc.frameIndex = SERVICES::gCtx.frameIndex;
        desc.pixEventName = "DepthPyramid.Build.SceneDepth";

        if (!depthPyramid_.BuildFromDepthSrv(desc)) {
            stats_.hzbBuildRequested = depthPyramid_.GetStats().buildRequested;
            PublishDepthPyramidStats(depthPyramid_.GetCurrentView());
            stats_.hzbBuilt = false;
            return false;
        }

        stats_.hzbBuildRequested = depthPyramid_.GetStats().buildRequested;
        stats_.hzbBuilt = true;
        PublishDepthPyramidStats(depthPyramid_.GetCurrentView());
        return true;
    }
	//  深度リソースを確保する。指定された幅と高さに基づいて、深度テクスチャ、DSV ヒープ、SRV デスクリプタを作成する。すでに同じサイズのリソースが存在する場合は、再利用する。
    bool GpuDepthVisibilityLayer::EnsureDepthResource(uint32_t width, uint32_t height) {
        width = std::max(1u, width);
        height = std::max(1u, height);
        if (depthWidth_ == width &&
            depthHeight_ == height &&
            visibilityDepth_ != nullptr &&
            visibilityDepthSrv_.IsValid() &&
            visibilityDsv_.ptr != 0) {
            stats_.visibilityDepthReady = true;
            stats_.visibilityDepthSrv = visibilityDepthSrv_.gpu;
            return true;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        if (device == nullptr) {
            return false;
        }

        ReleaseDepthResource();
        depthWidth_ = width;
        depthHeight_ = height;

        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS,
            static_cast<UINT64>(width),
            height,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_D32_FLOAT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(visibilityDepth_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "GpuDepthVisibility::CreateVisibilityDepth")) {
            ReleaseDepthResource();
            return false;
        }
        visibilityDepth_->SetName(L"HIKARI.DepthVisibility.Depth");
        visibilityDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = device->CreateDescriptorHeap(
            &dsvHeapDesc,
            IID_PPV_ARGS(visibilityDsvHeap_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "GpuDepthVisibility::CreateVisibilityDsvHeap")) {
            ReleaseDepthResource();
            return false;
        }
        visibilityDsvHeap_->SetName(L"HIKARI.DepthVisibility.DSVHeap");
        visibilityDsv_ = visibilityDsvHeap_->GetCPUDescriptorHandleForHeapStart();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        device->CreateDepthStencilView(visibilityDepth_.Get(), &dsvDesc, visibilityDsv_);

        visibilityDepthSrv_ = CreateVisibilityDepthSrvDescriptor(
            visibilityDepth_.Get(),
            DXGI_FORMAT_R32_FLOAT);
        if (!visibilityDepthSrv_.IsValid()) {
            ReleaseDepthResource();
            return false;
        }

        stats_.visibilityDepthReady = true;
        stats_.visibilityDepthSrv = visibilityDepthSrv_.gpu;
        stats_.width = depthWidth_;
        stats_.height = depthHeight_;
        HIKARI_LOG_INFO(
            "[GpuDepthVisibility] visibility depth resized " +
            std::to_string(depthWidth_) +
            "x" +
            std::to_string(depthHeight_));
        return true;
    }
	// 深度リソースを解放する。SRV デスクリプタと DSV デスクリプタも解放する。
    void GpuDepthVisibilityLayer::ReleaseDepthResource() {
        DEPTH::RetireDepthPyramidTransientDescriptor(
            visibilityDepthSrv_,
            "GpuDepthVisibility.VisibilityDepth.SRV");
        visibilityDepthSrv_ = {};
        visibilityDsv_ = {};
        GFX::RetireD3D12ObjectForCurrentFrame(visibilityDsvHeap_, "GpuDepthVisibility.VisibilityDepth.DSVHeap");
        GFX::RetireD3D12ObjectForCurrentFrame(visibilityDepth_, "GpuDepthVisibility.VisibilityDepth.Texture");
        visibilityDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthWidth_ = 0;
        depthHeight_ = 0;
    }
	// 深度リソースの状態を遷移させる。指定された次の状態に遷移する。すでに同じ状態の場合は何もしない。
    void GpuDepthVisibilityLayer::TransitionDepth(
        ID3D12GraphicsCommandList* cmd,
        D3D12_RESOURCE_STATES nextState) {

        if (cmd == nullptr ||
            visibilityDepth_ == nullptr ||
            visibilityDepthState_ == nextState) {
            visibilityDepthState_ = nextState;
            return;
        }

        const D3D12_RESOURCE_BARRIER barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(
                visibilityDepth_.Get(),
                visibilityDepthState_,
                nextState);
        cmd->ResourceBarrier(1, &barrier);
        visibilityDepthState_ = nextState;
    }
	// 深度ピラミッドの統計情報を公開する。深度ピラミッドのビューを指定することで、統計情報を更新する。
    void GpuDepthVisibilityLayer::PublishDepthPyramidStats(
        const DEPTH::DepthPyramidView& view) {
		// 深度ピラミッドの統計情報を更新する。ビューが有効でない場合は、統計情報をリセットする。
        const DEPTH::DepthPyramidStats& pyramidStats = depthPyramid_.GetStats();
        stats_.depthPyramid = view;
        stats_.hzbBuilt = view.valid;
        stats_.psoReady = pyramidStats.psoReady;
        stats_.descriptorPoolReady = pyramidStats.descriptorPoolReady;
        stats_.resourcesReady = stats_.resourcesReady || pyramidStats.resourcesReady;
        stats_.width = view.sourceWidth != 0 ? view.sourceWidth : stats_.width;
        stats_.height = view.sourceHeight != 0 ? view.sourceHeight : stats_.height;
        stats_.hzbWidth = view.width;
        stats_.hzbHeight = view.height;
        stats_.hzbMipCount = view.mipCount;
        stats_.hzbDescriptorCount = view.descriptorCount;
        stats_.hzbFinestSrv = view.pyramidSrv;
        stats_.hzbCoarsestSrv = view.coarsestSrv;
        stats_.hzbViewProj = view.viewProj;
        stats_.hzbViewProjValid = view.viewProjValid;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
