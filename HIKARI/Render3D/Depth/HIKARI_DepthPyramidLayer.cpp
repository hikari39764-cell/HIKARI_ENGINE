#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"

#include <algorithm>
#include <array>
#include <string>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorAllocator.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::DEPTH {

    namespace {
        constexpr DXGI_FORMAT kDepthPyramidFormat = DXGI_FORMAT_R32_FLOAT;
        constexpr uint32_t kDepthPyramidThreadGroupSize = 8u;
        constexpr uint32_t kDepthPyramidMaxMipCount = 16u;
		// 8x8 のスレッドグループで処理するため、幅と高さを 8 の倍数に丸める
        uint32_t ComputeMipCount(uint32_t width, uint32_t height) {
            uint32_t mipCount = 0;
            width = std::max(1u, (std::max(1u, width) + 1u) / 2u);
            height = std::max(1u, (std::max(1u, height) + 1u) / 2u);
            while (mipCount < kDepthPyramidMaxMipCount) {
                ++mipCount;
                if (width == 1u && height == 1u) {
                    break;
                }
                width = std::max(1u, width / 2u);
                height = std::max(1u, height / 2u);
            }
            return std::max(1u, mipCount);
        }
		// 整数の除算を切り上げる
        uint32_t DivRoundUp(uint32_t value, uint32_t divisor) {
            return (value + divisor - 1u) / divisor;
        }
        // 現在のフレームの退避フェンス値を取得する
        uint64_t CurrentRetireFenceValue() {
            return SERVICES::gCtx.currentFrameRetireFenceValue != 0
                ? SERVICES::gCtx.currentFrameRetireFenceValue
                : 0;
        }

		// デスクリプタアロケータの状態を保持する構造体
        struct DescriptorState {
            GFX::Context context{};
            GFX::DescriptorAllocator allocator{};
            UINT descriptorSize = 0;
            bool initialized = false;
        };

        DescriptorState& Descriptors() {
            static DescriptorState state{};
            return state;
        }
		// デスクリプタアロケータを初期化する。すでに初期化済みの場合は何もしない。
        bool EnsureDescriptorAllocator() {
            DescriptorState& state = Descriptors();
            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12DescriptorHeap* heap = SERVICES::gCtx.srvHeap;
            if (device == nullptr || heap == nullptr) {
                return false;
            }

            if (!state.initialized ||
                state.context.device != device ||
                state.context.srvHeap != heap) {
                state.context = SERVICES::gCtx;
                state.descriptorSize = device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                state.allocator.Initialize(
                    GFX::DESCRIPTOR::kDepthPyramidTransientDescriptorBegin,
                    GFX::DESCRIPTOR::kDepthPyramidTransientDescriptorCount);
                state.initialized = true;
            }
            return true;
        }
		// デスクリプタを解放する。すでに解放済みの場合は何もしない。
        void FreeDescriptor(RenderResourceView view) {
            if (view.descriptorIndex == UINT32_MAX) {
                return;
            }

            DescriptorState& state = Descriptors();
            const GFX::DescriptorSlot slot{ view.descriptorIndex };
            if (!state.initialized ||
                !state.allocator.Owns(slot) ||
                !state.allocator.IsAllocated(slot)) {
                return;
            }

            state.allocator.Free(slot);
        }
		// テクスチャ 2D の SRV デスクリプタを作成する。リソースが nullptr または mipLevels が 0 の場合は無効な RenderResourceView を返す。
        RenderResourceView CreateTexture2DSrvDescriptor(
            ID3D12Resource* resource,
            DXGI_FORMAT format,
            UINT mostDetailedMip = 0,
            UINT mipLevels = 1) {

            if (resource == nullptr || mipLevels == 0) {
                return {};
            }

            RenderResourceView view = AllocateDepthPyramidTransientDescriptor();
            if (!view.IsValid() || view.cpu.ptr == 0 || view.gpu.ptr == 0) {
                return {};
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = mostDetailedMip;
            srvDesc.Texture2D.MipLevels = mipLevels;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            SERVICES::gCtx.device->CreateShaderResourceView(resource, &srvDesc, view.cpu);
            return view;
        }
		// テクスチャ 2D の UAV デスクリプタを作成する。リソースが nullptr の場合は無効な RenderResourceView を返す。
        RenderResourceView CreateTexture2DUavDescriptor(
            ID3D12Resource* resource,
            DXGI_FORMAT format,
            UINT mipSlice = 0) {

            if (resource == nullptr) {
                return {};
            }

            RenderResourceView view = AllocateDepthPyramidTransientDescriptor();
            if (!view.IsValid() || view.cpu.ptr == 0 || view.gpu.ptr == 0) {
                return {};
            }

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = mipSlice;
            uavDesc.Texture2D.PlaneSlice = 0;
            SERVICES::gCtx.device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, view.cpu);
            return view;
        }
    }

    RenderResourceView AllocateDepthPyramidTransientDescriptor() {
        RenderResourceView view{};
        if (!EnsureDescriptorAllocator()) {
            return view;
        }

        DescriptorState& state = Descriptors();
        const GFX::DescriptorSlot slot = state.allocator.Allocate();
        if (!slot.IsValid()) {
            HIKARI_LOG_ERROR("[DepthPyramid][ERROR] transient descriptor pool exhausted.");
            return view;
        }

        view.descriptorIndex = slot.index;
        view.cpu = GFX::DESCRIPTOR::CpuAt(
            state.context.srvHeap,
            state.descriptorSize,
            slot.index);
        view.gpu = GFX::DESCRIPTOR::GpuAt(
            state.context.srvHeap,
            state.descriptorSize,
            slot.index);
        return view;
    }
	// デスクリプタを退避キューに登録して、GPU が使用し終わった後に解放する。すでに無効な RenderResourceView の場合は何もしない。
    void RetireDepthPyramidTransientDescriptor(
        RenderResourceView view,
        const char* debugName) {

        if (!view.IsValid()) {
            return;
        }

        GFX::GpuDeferredReleaseQueue* queue = SERVICES::gCtx.deferredReleaseQueue;
        const uint64_t retireFence = CurrentRetireFenceValue();
        if (queue != nullptr && retireFence != 0) {
            queue->Enqueue(
                retireFence,
                [view]() {
                    FreeDescriptor(view);
                },
                debugName != nullptr ? debugName : "DepthPyramid.Descriptor");
            return;
        }

        FreeDescriptor(view);
    }
	// フレームのリセット処理。PSO やリソースの状態を保持する。
    void DepthPyramidLayer::ResetFrame() {
        const bool psoReady = rootSignature_ != nullptr && pipelineState_ != nullptr;
        const bool resourcesReady = texture_ != nullptr && pyramidSrv_.IsValid() && !mips_.empty();
        const DepthPyramidView view = currentView_;
        stats_ = {};
        stats_.psoReady = psoReady;
        stats_.resourcesReady = resourcesReady;
        stats_.currentView = view;
        currentView_ = view;
    }

    void DepthPyramidLayer::Release() {
        ReleaseResources();
        rootSignature_.Reset();
        pipelineState_.Reset();
        currentView_ = {};
        stats_ = {};
    }
	// 深度ピラミッドを構築する。共有sourceDepthSrv から mip0 を構築し、順次 mip1, mip2... を構築する。
    bool DepthPyramidLayer::BuildFromDepthSrv(const DepthPyramidBuildDesc& desc) {
        stats_.buildRequested = true;
        stats_.built = false;
        currentView_ = {};
        stats_.currentView = currentView_;
        if (desc.commandList == nullptr ||
            desc.sourceDepthSrv.ptr == 0 ||
            desc.sourceWidth == 0 ||
            desc.sourceHeight == 0) {
            return false;
        }

        if (!EnsurePipeline() || !EnsureResources(desc.sourceWidth, desc.sourceHeight)) {
            return false;
        }

        ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
        if (srvHeap == nullptr ||
            texture_ == nullptr ||
            pyramidSrv_.gpu.ptr == 0 ||
            mips_.empty()) {
            return false;
        }
        stats_.descriptorPoolReady = true;

        GFX::PIX::ScopedGpuEvent pix(
            desc.commandList,
            GFX::PIX::kColorUpload,
            desc.pixEventName != nullptr ? desc.pixEventName : "DepthPyramid.Build");

        ID3D12DescriptorHeap* heaps[] = { srvHeap };
        desc.commandList->SetDescriptorHeaps(1, heaps);
        desc.commandList->SetComputeRootSignature(rootSignature_.Get());
        desc.commandList->SetPipelineState(pipelineState_.Get());

        D3D12_GPU_DESCRIPTOR_HANDLE sourceSrv = desc.sourceDepthSrv;
        uint32_t sourceWidth = std::max(1u, desc.sourceWidth);
        uint32_t sourceHeight = std::max(1u, desc.sourceHeight);
		// mip0 は sourceDepthSrv から構築するため、mip0 の UAV は使用しない
        for (uint32_t mipIndex = 0; mipIndex < static_cast<uint32_t>(mips_.size()); ++mipIndex) {
            MipView& mip = mips_[mipIndex];
            TransitionMip(
                desc.commandList,
                mipIndex,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            const uint32_t constants[] = {
                sourceWidth,
                sourceHeight,
                mip.width,
                mip.height
            };
            desc.commandList->SetComputeRoot32BitConstants(0, 4u, constants, 0u);
            desc.commandList->SetComputeRootDescriptorTable(1, sourceSrv);
            desc.commandList->SetComputeRootDescriptorTable(2, mip.uav.gpu);
            desc.commandList->Dispatch(
                DivRoundUp(mip.width, kDepthPyramidThreadGroupSize),
                DivRoundUp(mip.height, kDepthPyramidThreadGroupSize),
                1u);

            const D3D12_RESOURCE_BARRIER uavBarrier =
                CD3DX12_RESOURCE_BARRIER::UAV(texture_.Get());
            desc.commandList->ResourceBarrier(1, &uavBarrier);
            TransitionMip(
                desc.commandList,
                mipIndex,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            sourceSrv = mip.srv.gpu;
            sourceWidth = mip.width;
            sourceHeight = mip.height;
        }

        stats_.built = true;
        stats_.resourcesReady = true;
        PublishCurrentView(desc);
        return true;
    }
	// パイプラインステートオブジェクトとルートシグネチャを作成する。すでに作成済みの場合は何もしない。
    bool DepthPyramidLayer::EnsurePipeline() {
        if (rootSignature_ != nullptr && pipelineState_ != nullptr) {
            stats_.psoReady = true;
            return true;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        if (device == nullptr) {
            return false;
        }
        if (!GFX::SupportsShaderModel6(device)) {
            DEBUGLOG::PushRenderError(
                "[DepthPyramid][ERROR] Shader Model 6.0 is required for depth pyramid build.");
            return false;
        }

        std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		// ルートパラメータを定義する。ルートパラメータは、シェーダーで使用する定数バッファやデスクリプタテーブルを指定する。
        std::array<D3D12_ROOT_PARAMETER, 3> params{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = 4;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &ranges[0];
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges = &ranges[1];

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = static_cast<UINT>(params.size());
        rootDesc.pParameters = params.data();
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeRootSignature(
            &rootDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            signature.GetAddressOf(),
            error.GetAddressOf());
        if (FAILED(hr)) {
            if (error != nullptr) {
                OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
            }
            return false;
        }
        hr = device->CreateRootSignature(
            0,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(rootSignature_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "DepthPyramid::CreateRootSignature")) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> cs;
        if (!GFX::CompileShaderFileSm6(
            L"HIKARI/Shaders/DepthPyramid_BuildCS.hlsl",
            "main",
            GFX::ShaderStage::Compute,
            cs.GetAddressOf())) {
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignature_.Get();
        psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        hr = device->CreateComputePipelineState(
            &psoDesc,
            IID_PPV_ARGS(pipelineState_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "DepthPyramid::CreatePso")) {
            return false;
        }
        pipelineState_->SetName(L"HIKARI.DepthPyramid.PSO");
        stats_.psoReady = true;
        return true;
    }
	// 深度ピラミッドのリソースを確保する。すでに確保済みの場合は何もしない。
    bool DepthPyramidLayer::EnsureResources(uint32_t sourceWidth, uint32_t sourceHeight) {
        sourceWidth = std::max(1u, sourceWidth);
        sourceHeight = std::max(1u, sourceHeight);
        if (sourceWidth_ == sourceWidth &&
            sourceHeight_ == sourceHeight &&
            texture_ != nullptr &&
            pyramidSrv_.IsValid() &&
            !mips_.empty()) {
            stats_.resourcesReady = true;
            return true;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        if (device == nullptr) {
            return false;
        }

        ReleaseResources();
        sourceWidth_ = sourceWidth;
        sourceHeight_ = sourceHeight;

        const uint32_t mipCount = ComputeMipCount(sourceWidth, sourceHeight);
        mips_.reserve(mipCount);

        const uint32_t mip0Width = std::max(1u, (sourceWidth + 1u) / 2u);
        const uint32_t mip0Height = std::max(1u, (sourceHeight + 1u) / 2u);
        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            kDepthPyramidFormat,
            mip0Width,
            mip0Height,
            1,
            static_cast<UINT16>(mipCount),
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const D3D12_RESOURCE_STATES initialState =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            initialState,
            nullptr,
            IID_PPV_ARGS(texture_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "DepthPyramid::CreateTexture")) {
            ReleaseResources();
            return false;
        }
        texture_->SetName(L"HIKARI.DepthPyramid.Texture");

        pyramidSrv_ = CreateTexture2DSrvDescriptor(
            texture_.Get(),
            kDepthPyramidFormat,
            0,
            mipCount);
        if (!pyramidSrv_.IsValid() || pyramidSrv_.gpu.ptr == 0) {
            ReleaseResources();
            return false;
        }

        uint32_t mipWidth = mip0Width;
        uint32_t mipHeight = mip0Height;
		// 各 mip レベルの SRV と UAV を作成する
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex) {
            MipView mip{};
            mip.width = mipWidth;
            mip.height = mipHeight;

            mip.srv = CreateTexture2DSrvDescriptor(
                texture_.Get(),
                kDepthPyramidFormat,
                mipIndex,
                1);
            mip.uav = CreateTexture2DUavDescriptor(
                texture_.Get(),
                kDepthPyramidFormat,
                mipIndex);
            if (!mip.srv.IsValid() || !mip.uav.IsValid()) {
                RetireDepthPyramidTransientDescriptor(mip.srv, "DepthPyramid.MipSRV.FailedCreate");
                RetireDepthPyramidTransientDescriptor(mip.uav, "DepthPyramid.MipUAV.FailedCreate");
                ReleaseResources();
                return false;
            }

            mips_.push_back(std::move(mip));
            if (mipWidth == 1u && mipHeight == 1u) {
                break;
            }
            mipWidth = std::max(1u, mipWidth / 2u);
            mipHeight = std::max(1u, mipHeight / 2u);
        }

        stats_.resourcesReady = texture_ != nullptr && pyramidSrv_.IsValid() && !mips_.empty();
        HIKARI_LOG_INFO(
            "[DepthPyramid] resized " +
            std::to_string(mip0Width) +
            "x" +
            std::to_string(mip0Height) +
            " source=" +
            std::to_string(sourceWidth_) +
            "x" +
            std::to_string(sourceHeight_) +
            " mips=" +
            std::to_string(mips_.size()));
        return stats_.resourcesReady;
    }
	// 深度ピラミッドのリソースを解放する。SRV と UAV のデスクリプタも解放する。
    void DepthPyramidLayer::ReleaseResources() {
        RetireDepthPyramidTransientDescriptor(pyramidSrv_, "DepthPyramid.PyramidSRV");
        pyramidSrv_ = {};
        for (MipView& mip : mips_) {
            RetireDepthPyramidTransientDescriptor(mip.srv, "DepthPyramid.MipSRV");
            RetireDepthPyramidTransientDescriptor(mip.uav, "DepthPyramid.MipUAV");
            mip = {};
        }
        mips_.clear();
        GFX::RetireD3D12ObjectForCurrentFrame(texture_, "DepthPyramid.Texture");
        sourceWidth_ = 0;
        sourceHeight_ = 0;
        currentView_ = {};
    }
	// 指定した mip レベルのリソース状態を遷移させる。すでに指定した状態の場合は何もしない。
    void DepthPyramidLayer::TransitionMip(
        ID3D12GraphicsCommandList* cmd,
        uint32_t mipIndex,
        D3D12_RESOURCE_STATES nextState) {

        if (mipIndex >= mips_.size()) {
            return;
        }

        MipView& mip = mips_[mipIndex];
        if (cmd == nullptr || texture_ == nullptr || mip.state == nextState) {
            mip.state = nextState;
            return;
        }

        const D3D12_RESOURCE_BARRIER barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(
                texture_.Get(),
                mip.state,
                nextState,
                mipIndex);
        cmd->ResourceBarrier(1, &barrier);
        mip.state = nextState;
    }
	// 深度ピラミッドの現在のビューを公開する。ビューは、SRV と UAV のデスクリプタ、幅、高さ、mip レベル数などの情報を含む。
    void DepthPyramidLayer::PublishCurrentView(const DepthPyramidBuildDesc& desc) {
        DepthPyramidView view{};
        view.valid =
            pyramidSrv_.gpu.ptr != 0 &&
            !mips_.empty() &&
            desc.sourceWidth != 0 &&
            desc.sourceHeight != 0;
        view.sourceKind = desc.sourceKind;
        view.depthConvention = DepthPyramidDepthConvention::StandardD3DLessMax;
        view.viewKind = desc.viewKind;
        view.pyramidSrv = pyramidSrv_.gpu;
        view.mip0Srv = !mips_.empty() ? mips_.front().srv.gpu : D3D12_GPU_DESCRIPTOR_HANDLE{};
        view.coarsestSrv = !mips_.empty() ? mips_.back().srv.gpu : D3D12_GPU_DESCRIPTOR_HANDLE{};
        view.sourceWidth = desc.sourceWidth;
        view.sourceHeight = desc.sourceHeight;
        view.width = !mips_.empty() ? mips_.front().width : 0u;
        view.height = !mips_.empty() ? mips_.front().height : 0u;
        view.mipCount = static_cast<uint32_t>(mips_.size());
        view.descriptorCount = view.mipCount * 2u + (pyramidSrv_.IsValid() ? 1u : 0u);
        view.frameIndex = desc.frameIndex;
        view.viewProj = desc.viewProj;
        view.viewProjValid = desc.viewProjValid;
        view.jitteredViewProj = desc.jitteredViewProj;

        currentView_ = view;
        stats_.currentView = view;
    }

} // namespace HIKARI::RENDER3D::DEPTH
