#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"

#include <algorithm>
#include <array>
#include <iterator>

#include <d3dx12.h>

#include "Core/HIKARI_TimeService.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        constexpr uint32_t kThreadGroupSize = 64u;
        constexpr size_t kClusterGpuCullingMaxSourceRangeCount = 8u;
        constexpr uint32_t kClusterCullMergeGapIndexLimit = 1536u;
        constexpr uint32_t kClusterCullMergeRunGapIndexBudget = 12288u;
        constexpr uint32_t kClusterCullMergeMaxIndexSpan = 32768u;
        // クラスタ間の空白をまたぐ結合は過剰描画になりやすいので、正式なcompactまで無効化する。
        constexpr uint32_t kClusterCullMergeClusterGapLimit = 3u;
        constexpr float kClusterCullLodTargetErrorNdc = 0.0180f;
        constexpr float kClusterCullLodTransitionRelaxPerLevel = 0.35f;
        constexpr float kClusterCullLodErrorRelaxPerLevel = 0.75f;
        // CullPageTasksCS は 1 group = 1 page task 前提。page 単位で emit する
        constexpr uint32_t kClusterCullPageTaskGroupSize = 1u;
        constexpr uint32_t kClusterCullClusterHzbMinScreenPixels = 4u;
        constexpr DXGI_FORMAT kClusterCullHzbFallbackFormat = DXGI_FORMAT_R32_FLOAT;
        constexpr float kClusterCullHzbDepthBias = 0.0005f;
        constexpr float kClusterCullHzbMaxScreenRadiusPixels = 4096.0f;
        constexpr uint32_t kClusterCullHzbTestBudget = 16000u;
        constexpr size_t kClusterCullOcclusionHistoryMinCapacity = 65536u;
        constexpr size_t kClusterCullOcclusionHistoryMaxCapacity = 1048576u;
        constexpr size_t kClusterCullOcclusionHistoryEntryBytes = sizeof(uint32_t) * 2u;
        constexpr uint32_t kClusterCullHzbOcclusionConfirmFrames = 1u;
        constexpr size_t kClusterCullVisibleClusterListCapacityMultiplier = 64u;
        constexpr uint32_t kClusterMeshletFineCullingOwnerAmplificationShader = 1u;

		// 固定サイズのSRV（Shader Resource View）ディスクリプタを取得します
        RenderResourceView FixedSrvHeapView(ID3D12Device* device, UINT descriptorIndex) {
            RenderResourceView view{};
            ID3D12DescriptorHeap* heap = SERVICES::gCtx.srvHeap;
            if (device == nullptr ||
                heap == nullptr ||
                descriptorIndex >= GFX::DESCRIPTOR::kSrvHeapCapacity) {
                return view;
            }

            const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            view.descriptorIndex = descriptorIndex;
            view.cpu = GFX::DESCRIPTOR::CpuAt(heap, descriptorSize, descriptorIndex);
            view.gpu = GFX::DESCRIPTOR::GpuAt(heap, descriptorSize, descriptorIndex);
            return view;
        }
		// 固定サイズの2DテクスチャのSRV（Shader Resource View）ディスクリプタを作成します
        RenderResourceView CreateFixedTexture2DSrvDescriptor(
            ID3D12Device* device,
            ID3D12Resource* resource,
            DXGI_FORMAT format,
            UINT descriptorIndex) {

            if (device == nullptr || resource == nullptr) {
                return {};
            }

            RenderResourceView view = FixedSrvHeapView(device, descriptorIndex);
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
            device->CreateShaderResourceView(resource, &srvDesc, view.cpu);
            return view;
        }

        size_t NextCapacity(size_t requested, size_t fallback) {
            size_t capacity = (std::max)(requested, fallback);
            size_t pow2 = 1u;
            while (pow2 < capacity) {
                pow2 <<= 1u;
            }
            return pow2;
        }
		// デバッグログに警告メッセージを出力します
        void ReportWarning(const char* message) {
            if (message == nullptr) {
                return;
            }
            DEBUGLOG::PushRenderError(message);
            HIKARI_LOG_WARN(message);
        }

        size_t CullBucketIndex(GeometryCullModeBucket bucket) {
            const size_t index = static_cast<size_t>(bucket);
            return index < kGeometryCullModeBucketCount ? index : 0u;
        }

        size_t CullPassIndex(ClusterGpuCullingPassKind passKind) {
            const size_t index = static_cast<size_t>(passKind);
            return index < kClusterGpuCullingPassKindCount ? index : 0u;
        }
		// ComputePipelineStateの作成を行います
        bool CreateComputePipelineState(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            const wchar_t* shaderPath,
            const char* entryPoint,
            const wchar_t* debugName,
            ID3D12PipelineState** outPipelineState) {

            if (device == nullptr ||
                rootSignature == nullptr ||
                shaderPath == nullptr ||
                entryPoint == nullptr ||
                outPipelineState == nullptr) {
                return false;
            }

            Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
            if (!GFX::CompileShaderFileSm6(
                shaderPath,
                entryPoint,
                GFX::ShaderStage::Compute,
                computeShader.GetAddressOf())) {
                return false;
            }

            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = rootSignature;
            psoDesc.CS = {
                computeShader->GetBufferPointer(),
                computeShader->GetBufferSize()
            };
            const HRESULT hr = device->CreateComputePipelineState(
                &psoDesc,
                IID_PPV_ARGS(outPipelineState));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateComputePipelineState")) {
                return false;
            }
            GFX::SetD3D12Name(*outPipelineState, debugName);
            return true;
        }

    }
	// ClusterGpuCullingPassの初期化を行います   
    bool ClusterGpuCullingPass::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* drawRootSignature,
        UINT rootConstantParameterIndex,
        UINT rootConstantCount,
        size_t pageTaskCapacity,
        size_t visibleRangeCapacity,
        size_t candidateCommandCapacity) {

        Reset();
        if (device == nullptr) {
            return false;
        }
        if (!EnsurePipeline(device)) {
            ReportWarning("[ClusterGpuCulling][WARN] pipeline creation failed.");
            return false;
        }
        if (!EnsureDispatchCommandSignature(device)) {
            ReportWarning("[ClusterGpuCulling][WARN] dispatch command signature creation failed.");
            return false;
        }
        if (!EnsureMeshletDispatchCommandSignature(
            device,
            drawRootSignature,
            rootConstantParameterIndex,
            rootConstantCount)) {
            ReportWarning("[ClusterGpuCulling][WARN] meshlet dispatch command signature creation failed.");
        }
        if (!EnsureCapacity(device, pageTaskCapacity, visibleRangeCapacity, candidateCommandCapacity)) {
            ReportWarning("[ClusterGpuCulling][WARN] resource allocation failed.");
            return false;
        }
        stats_.initialized = true;
        stats_.psoReady =
            expandPageTasksPipelineState_ != nullptr &&
            finalizeDispatchPipelineState_ != nullptr &&
            finalizeMeshletDispatchPipelineState_ != nullptr &&
            cullPageTasksPipelineState_ != nullptr;
        stats_.threadGroupSize = kThreadGroupSize;
        return true;
    }
	// ClusterGpuCullingPassのリソースをリセットし、関連するD3D12オブジェクトを解放します。
    void ClusterGpuCullingPass::Reset() {
        constantsMapped_ = nullptr;
        counterResetMapped_ = nullptr;
        for (FrameResources& frame : frameResources_) {
            GFX::RetireD3D12ObjectForCurrentFrame(frame.constantsUploadBuffer, "ClusterGpuCulling.Frame.ConstantsUpload");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.counterResetUploadBuffer, "ClusterGpuCulling.Frame.CounterResetUpload");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.pageTaskBuffer, "ClusterGpuCulling.Frame.PageTaskBuffer");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.visibleRangeBuffer, "ClusterGpuCulling.Frame.VisibleRangeBuffer");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.visibleClusterListBuffer, "ClusterGpuCulling.Frame.VisibleClusterListBuffer");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.meshletDispatchArgumentBuffer, "ClusterGpuCulling.Frame.MeshletDispatchArgumentBuffer");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.dispatchArgumentBuffer, "ClusterGpuCulling.Frame.DispatchArgumentBuffer");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.counterBuffer, "ClusterGpuCulling.Frame.CounterBuffer");
            frame.constantsMapped = nullptr;
            frame.counterResetMapped = nullptr;
            frame.pageTaskBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.visibleRangeBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.visibleClusterListBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.meshletDispatchArgumentBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.dispatchArgumentBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.counterBufferState = D3D12_RESOURCE_STATE_COMMON;
        }
        activeFrameResourceIndex_ = 0;
        GFX::RetireD3D12ObjectForCurrentFrame(constantsUploadBuffer_, "ClusterGpuCulling.ConstantsUpload");
        GFX::RetireD3D12ObjectForCurrentFrame(counterResetUploadBuffer_, "ClusterGpuCulling.CounterResetUpload");
        GFX::RetireD3D12ObjectForCurrentFrame(pageTaskBuffer_, "ClusterGpuCulling.PageTaskBuffer");
        GFX::RetireD3D12ObjectForCurrentFrame(visibleRangeBuffer_, "ClusterGpuCulling.VisibleRangeBuffer");
        GFX::RetireD3D12ObjectForCurrentFrame(visibleClusterListBuffer_, "ClusterGpuCulling.VisibleClusterListBuffer");
        GFX::RetireD3D12ObjectForCurrentFrame(meshletDispatchArgumentBuffer_, "ClusterGpuCulling.MeshletDispatchArgumentBuffer");
        GFX::RetireD3D12ObjectForCurrentFrame(dispatchArgumentBuffer_, "ClusterGpuCulling.DispatchArgumentBuffer");
        GFX::RetireD3D12ObjectForCurrentFrame(counterBuffer_, "ClusterGpuCulling.CounterBuffer");
        GFX::RetireD3D12ObjectForCurrentFrame(occlusionHistoryBuffer_, "ClusterGpuCulling.OcclusionHistoryBuffer");
        fallbackHzbSrv_ = {};
        GFX::RetireD3D12ObjectForCurrentFrame(fallbackHzb_, "ClusterGpuCulling.FallbackHzb");
        for (CounterReadbackSlot& slot : counterReadbackSlots_) {
            GFX::RetireD3D12ObjectForCurrentFrame(slot.buffer, "ClusterGpuCulling.CounterReadback");
            slot.resolved = false;
        }
        rootSignature_.Reset();
        expandPageTasksPipelineState_.Reset();
        finalizeDispatchPipelineState_.Reset();
        finalizeMeshletDispatchPipelineState_.Reset();
        cullPageTasksPipelineState_.Reset();
        dispatchCommandSignature_.Reset();
        meshletDispatchCommandSignature_.Reset();
        pageTaskCapacity_ = 0;
        visibleRangeCapacity_ = 0;
        visibleClusterListCapacity_ = 0;
        candidateCommandCapacity_ = 0;
        occlusionHistoryCapacity_ = 0;
        counterReadbackWriteIndex_ = 0;
        latestGpuCounters_ = {};
        latestGpuCountersValid_ = false;
        pageTaskBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        visibleRangeBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        visibleClusterListBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        counterBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        occlusionHistoryBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        stats_ = {};
    }

    void ClusterGpuCullingPass::BeginFrame(bool collectCounterReadback) {
        BindFrameResources(static_cast<uint32_t>(TIME::GetFrameContext().frameIndex));
        if (collectCounterReadback && !counterReadbackSlots_.empty()) {
            CollectCounterReadback(
                counterReadbackSlots_[counterReadbackWriteIndex_ % counterReadbackSlots_.size()]);
        } else if (!collectCounterReadback) {
            latestGpuCountersValid_ = false;
        }
        ResetFrameStats();
        if (!collectCounterReadback) {
            stats_.gpuCounterReadbackReady = false;
            stats_.gpuCounterReadbackValid = false;
        }
    }
	// 現在のフレームリソースをバインドします
    void ClusterGpuCullingPass::BindFrameResources(uint32_t frameIndex) {
        activeFrameResourceIndex_ = frameIndex % GFX::kFrameResourceCount;
        FrameResources& frame = frameResources_[activeFrameResourceIndex_];

        constantsUploadBuffer_ = frame.constantsUploadBuffer;
        counterResetUploadBuffer_ = frame.counterResetUploadBuffer;
        pageTaskBuffer_ = frame.pageTaskBuffer;
        visibleRangeBuffer_ = frame.visibleRangeBuffer;
        visibleClusterListBuffer_ = frame.visibleClusterListBuffer;
        meshletDispatchArgumentBuffer_ = frame.meshletDispatchArgumentBuffer;
        dispatchArgumentBuffer_ = frame.dispatchArgumentBuffer;
        counterBuffer_ = frame.counterBuffer;

        constantsMapped_ = frame.constantsMapped;
        counterResetMapped_ = frame.counterResetMapped;
        pageTaskBufferState_ = frame.pageTaskBufferState;
        visibleRangeBufferState_ = frame.visibleRangeBufferState;
        visibleClusterListBufferState_ = frame.visibleClusterListBufferState;
        meshletDispatchArgumentBufferState_ = frame.meshletDispatchArgumentBufferState;
        dispatchArgumentBufferState_ = frame.dispatchArgumentBufferState;
        counterBufferState_ = frame.counterBufferState;
    }
	// 現在のフレームリソースの状態を保存します
    void ClusterGpuCullingPass::StoreActiveFrameResourceStates() {
        FrameResources& frame = frameResources_[activeFrameResourceIndex_ % GFX::kFrameResourceCount];
        frame.pageTaskBufferState = pageTaskBufferState_;
        frame.visibleRangeBufferState = visibleRangeBufferState_;
        frame.visibleClusterListBufferState = visibleClusterListBufferState_;
        frame.meshletDispatchArgumentBufferState = meshletDispatchArgumentBufferState_;
        frame.dispatchArgumentBufferState = dispatchArgumentBufferState_;
        frame.counterBufferState = counterBufferState_;
    }
	// ClusterGpuCullingPassのパイプラインが正しく初
    bool ClusterGpuCullingPass::EnsurePipeline(ID3D12Device* device) {
        if (rootSignature_ != nullptr &&
            expandPageTasksPipelineState_ != nullptr &&
            finalizeDispatchPipelineState_ != nullptr &&
            finalizeMeshletDispatchPipelineState_ != nullptr &&
            cullPageTasksPipelineState_ != nullptr) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }
        if (!GFX::SupportsShaderModel6(device)) {
            ReportWarning("[ClusterGpuCulling][WARN] Shader Model 6.0 is not supported.");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE clusterGeometryPoolRange{};
        clusterGeometryPoolRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        clusterGeometryPoolRange.NumDescriptors = GFX::DESCRIPTOR::kSystemSrvDynamicCount;
        clusterGeometryPoolRange.BaseShaderRegister = 0;
        clusterGeometryPoolRange.RegisterSpace = 1;
        clusterGeometryPoolRange.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE hzbRange{};
        hzbRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        hzbRange.NumDescriptors = 1;
        hzbRange.BaseShaderRegister = 18;
        hzbRange.RegisterSpace = 0;
        hzbRange.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[11]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[0].Descriptor.ShaderRegister = 0;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].Descriptor.ShaderRegister = 17;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[2].Descriptor.ShaderRegister = 0;

        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[3].Descriptor.ShaderRegister = 1;

        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[4].DescriptorTable.NumDescriptorRanges = 1;
        params[4].DescriptorTable.pDescriptorRanges = &clusterGeometryPoolRange;

        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[5].Descriptor.ShaderRegister = 3;

        params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[6].Descriptor.ShaderRegister = 4;

        params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[7].Descriptor.ShaderRegister = 5;

        params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[8].DescriptorTable.NumDescriptorRanges = 1;
        params[8].DescriptorTable.pDescriptorRanges = &hzbRange;

        params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[9].Descriptor.ShaderRegister = 6;

        params[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[10].Descriptor.ShaderRegister = 7;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = static_cast<UINT>(std::size(params));
        rootDesc.pParameters = params;
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        HRESULT hr = D3D12SerializeRootSignature(
            &rootDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            signature.GetAddressOf(),
            errors.GetAddressOf());
        if (FAILED(hr)) {
            if (errors != nullptr) {
                OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            }
            return false;
        }
        hr = device->CreateRootSignature(
            0,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(rootSignature_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateRootSignature")) {
            return false;
        }
        GFX::SetD3D12Name(rootSignature_.Get(), L"Cluster GPU Culling Root Signature");

        if (!EnsureFallbackHzb(device)) {
            return false;
        }

        if (!CreateComputePipelineState(
            device,
            rootSignature_.Get(),
            L"HIKARI/Shaders/Render3D_ClusterWorklistCS.hlsl",
            "ExpandPageTasksCS",
            L"Cluster GPU Worklist Expand Page Tasks PSO",
            expandPageTasksPipelineState_.GetAddressOf())) {
            return false;
        }
        if (!CreateComputePipelineState(
            device,
            rootSignature_.Get(),
            L"HIKARI/Shaders/Render3D_ClusterDispatchFinalizeCS.hlsl",
            "FinalizePageTaskDispatchCS",
            L"Cluster GPU Worklist Finalize Dispatch PSO",
            finalizeDispatchPipelineState_.GetAddressOf())) {
            return false;
        }
        if (!CreateComputePipelineState(
            device,
            rootSignature_.Get(),
            L"HIKARI/Shaders/Render3D_ClusterMeshletDispatchFinalizeCS.hlsl",
            "FinalizeMeshletDispatchCS",
            L"Cluster GPU Meshlet Dispatch Finalize PSO",
            finalizeMeshletDispatchPipelineState_.GetAddressOf())) {
            return false;
        }
        if (!CreateComputePipelineState(
            device,
            rootSignature_.Get(),
            L"HIKARI/Shaders/Render3D_ClusterFineVisibilityCS.hlsl",
            "CullPageTasksCS",
            L"Cluster GPU Fine Visibility Page Tasks PSO",
            cullPageTasksPipelineState_.GetAddressOf())) {
            return false;
        }
        return true;
    }

    bool ClusterGpuCullingPass::EnsureDispatchCommandSignature(ID3D12Device* device) {
        if (dispatchCommandSignature_ != nullptr) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }

        D3D12_INDIRECT_ARGUMENT_DESC argumentDesc{};
        argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
        signatureDesc.ByteStride = static_cast<UINT>(sizeof(D3D12_DISPATCH_ARGUMENTS));
        signatureDesc.NumArgumentDescs = 1;
        signatureDesc.pArgumentDescs = &argumentDesc;

        const HRESULT hr = device->CreateCommandSignature(
            &signatureDesc,
            nullptr,
            IID_PPV_ARGS(dispatchCommandSignature_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateDispatchCommandSignature")) {
            dispatchCommandSignature_.Reset();
            return false;
        }
        GFX::SetD3D12Name(dispatchCommandSignature_.Get(), L"Cluster GPU Cull Dispatch Signature");
        return true;
    }

    bool ClusterGpuCullingPass::EnsureMeshletDispatchCommandSignature(
        ID3D12Device* device,
        ID3D12RootSignature* drawRootSignature,
        UINT rootConstantParameterIndex,
        UINT rootConstantCount) {

        if (meshletDispatchCommandSignature_ != nullptr) {
            return true;
        }
        if (device == nullptr ||
            drawRootSignature == nullptr ||
            rootConstantCount != 4u) {
            return false;
        }

        D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2]{};
        argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        argumentDescs[0].Constant.RootParameterIndex = rootConstantParameterIndex;
        argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
        argumentDescs[0].Constant.Num32BitValuesToSet = rootConstantCount;
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
        signatureDesc.ByteStride = static_cast<UINT>(sizeof(GpuIndirectMeshletDispatchArgument));
        signatureDesc.NumArgumentDescs = static_cast<UINT>(std::size(argumentDescs));
        signatureDesc.pArgumentDescs = argumentDescs;

        const HRESULT hr = device->CreateCommandSignature(
            &signatureDesc,
            drawRootSignature,
            IID_PPV_ARGS(meshletDispatchCommandSignature_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateMeshletDispatchCommandSignature")) {
            meshletDispatchCommandSignature_.Reset();
            return false;
        }
        GFX::SetD3D12Name(
            meshletDispatchCommandSignature_.Get(),
            L"Cluster GPU Meshlet Dispatch Command Signature");
        return true;
    }

    bool ClusterGpuCullingPass::EnsureCapacity(
        ID3D12Device* device,
        size_t pageTaskCapacity,
        size_t visibleRangeCapacity,
        size_t candidateCommandCapacity) {

        if (device == nullptr) {
            return false;
        }

        const size_t requestedPageTaskCapacity =
            NextCapacity(pageTaskCapacity, kDefaultClusterGpuPageTaskCapacity);
        const size_t requestedCandidateCommandCapacity =
            NextCapacity(
                candidateCommandCapacity,
                kDefaultClusterGpuCandidateCommandCapacity);
        const size_t requestedVisibleCapacity =
            NextCapacity(
                (std::max)(visibleRangeCapacity, requestedCandidateCommandCapacity),
                kDefaultClusterGpuCullingVisibleRangeCapacity);
        const size_t requestedVisibleClusterListCapacity =
            NextCapacity(
                requestedVisibleCapacity *
                    kClusterCullVisibleClusterListCapacityMultiplier,
                kDefaultClusterGpuCullingVisibleRangeCapacity *
                    kClusterCullVisibleClusterListCapacityMultiplier);
        const size_t requestedOcclusionHistoryCapacity =
            (std::min)(
                NextCapacity(
                    (std::max)(
                        requestedVisibleCapacity * 2u,
                        requestedPageTaskCapacity * 8u),
                    kClusterCullOcclusionHistoryMinCapacity),
                kClusterCullOcclusionHistoryMaxCapacity);
        if (constantsUploadBuffer_ != nullptr &&
            counterResetUploadBuffer_ != nullptr &&
            pageTaskBuffer_ != nullptr &&
            visibleRangeBuffer_ != nullptr &&
            visibleClusterListBuffer_ != nullptr &&
            meshletDispatchArgumentBuffer_ != nullptr &&
            dispatchArgumentBuffer_ != nullptr &&
            counterBuffer_ != nullptr &&
            occlusionHistoryBuffer_ != nullptr &&
            counterReadbackSlots_[0].buffer != nullptr &&
            pageTaskCapacity_ >= requestedPageTaskCapacity &&
            visibleRangeCapacity_ >= requestedVisibleCapacity &&
            visibleClusterListCapacity_ >= requestedVisibleClusterListCapacity &&
            candidateCommandCapacity_ >= requestedCandidateCommandCapacity &&
            occlusionHistoryCapacity_ >= requestedOcclusionHistoryCapacity) {
            return true;
        }

        constantsMapped_ = nullptr;
        counterResetMapped_ = nullptr;
        for (FrameResources& frame : frameResources_) {
            GFX::RetireD3D12ObjectForCurrentFrame(frame.constantsUploadBuffer, "ClusterGpuCulling.Frame.ConstantsUpload.Resize");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.counterResetUploadBuffer, "ClusterGpuCulling.Frame.CounterResetUpload.Resize");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.pageTaskBuffer, "ClusterGpuCulling.Frame.PageTaskBuffer.Resize");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.visibleRangeBuffer, "ClusterGpuCulling.Frame.VisibleRangeBuffer.Resize");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.visibleClusterListBuffer, "ClusterGpuCulling.Frame.VisibleClusterListBuffer.Resize");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.meshletDispatchArgumentBuffer, "ClusterGpuCulling.Frame.MeshletDispatchArgumentBuffer.Resize");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.dispatchArgumentBuffer, "ClusterGpuCulling.Frame.DispatchArgumentBuffer.Resize");
            GFX::RetireD3D12ObjectForCurrentFrame(frame.counterBuffer, "ClusterGpuCulling.Frame.CounterBuffer.Resize");
            frame.constantsMapped = nullptr;
            frame.counterResetMapped = nullptr;
            frame.pageTaskBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.visibleRangeBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.visibleClusterListBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.meshletDispatchArgumentBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.dispatchArgumentBufferState = D3D12_RESOURCE_STATE_COMMON;
            frame.counterBufferState = D3D12_RESOURCE_STATE_COMMON;
        }
        activeFrameResourceIndex_ = 0;
        GFX::RetireD3D12ObjectForCurrentFrame(constantsUploadBuffer_, "ClusterGpuCulling.ConstantsUpload.Resize");
        GFX::RetireD3D12ObjectForCurrentFrame(counterResetUploadBuffer_, "ClusterGpuCulling.CounterResetUpload.Resize");
        GFX::RetireD3D12ObjectForCurrentFrame(pageTaskBuffer_, "ClusterGpuCulling.PageTaskBuffer.Resize");
        GFX::RetireD3D12ObjectForCurrentFrame(visibleRangeBuffer_, "ClusterGpuCulling.VisibleRangeBuffer.Resize");
        GFX::RetireD3D12ObjectForCurrentFrame(visibleClusterListBuffer_, "ClusterGpuCulling.VisibleClusterListBuffer.Resize");
        GFX::RetireD3D12ObjectForCurrentFrame(meshletDispatchArgumentBuffer_, "ClusterGpuCulling.MeshletDispatchArgumentBuffer.Resize");
        GFX::RetireD3D12ObjectForCurrentFrame(dispatchArgumentBuffer_, "ClusterGpuCulling.DispatchArgumentBuffer.Resize");
        GFX::RetireD3D12ObjectForCurrentFrame(counterBuffer_, "ClusterGpuCulling.CounterBuffer.Resize");
        GFX::RetireD3D12ObjectForCurrentFrame(occlusionHistoryBuffer_, "ClusterGpuCulling.OcclusionHistoryBuffer.Resize");
        for (CounterReadbackSlot& slot : counterReadbackSlots_) {
            GFX::RetireD3D12ObjectForCurrentFrame(slot.buffer, "ClusterGpuCulling.CounterReadback.Resize");
            slot.resolved = false;
        }
        pageTaskCapacity_ = 0;
        visibleRangeCapacity_ = 0;
        visibleClusterListCapacity_ = 0;
        candidateCommandCapacity_ = 0;
        occlusionHistoryCapacity_ = 0;
        counterReadbackWriteIndex_ = 0;
        latestGpuCounters_ = {};
        latestGpuCountersValid_ = false;
        pageTaskBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        visibleRangeBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        visibleClusterListBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        counterBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        occlusionHistoryBufferState_ = D3D12_RESOURCE_STATE_COMMON;

        const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto readbackHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

        const UINT constantsStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(GpuConstants));
        auto constantsDesc =
            CD3DX12_RESOURCE_DESC::Buffer(
                static_cast<UINT64>(constantsStride) *
                static_cast<UINT64>(kClusterGpuCullingMaxSourceRangeCount));
        HRESULT hr = device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &constantsDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(constantsUploadBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateConstantsUpload")) {
            return false;
        }
        if (FAILED(constantsUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantsMapped_)))) {
            constantsMapped_ = nullptr;
            return false;
        }
        GFX::SetD3D12Name(constantsUploadBuffer_.Get(), L"Cluster GPU Culling Constants");

        auto counterUploadDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(GpuCounterBuffer));
        hr = device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterUploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(counterResetUploadBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateCounterResetUpload")) {
            return false;
        }
        if (FAILED(counterResetUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&counterResetMapped_)))) {
            counterResetMapped_ = nullptr;
            return false;
        }
        GFX::SetD3D12Name(counterResetUploadBuffer_.Get(), L"Cluster GPU Culling Counter Reset");

        const UINT64 pageTaskBytes =
            static_cast<UINT64>(sizeof(GpuPageTask)) *
            static_cast<UINT64>(requestedPageTaskCapacity);
        auto pageTaskDesc = CD3DX12_RESOURCE_DESC::Buffer(
            pageTaskBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &pageTaskDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(pageTaskBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreatePageTaskBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(pageTaskBuffer_.Get(), L"Cluster GPU Page Tasks");

        const UINT64 visibleBytes =
            static_cast<UINT64>(sizeof(GpuVisibleRange)) *
            static_cast<UINT64>(requestedVisibleCapacity);
        auto visibleDesc = CD3DX12_RESOURCE_DESC::Buffer(
            visibleBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &visibleDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(visibleRangeBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateVisibleRangeBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(visibleRangeBuffer_.Get(), L"Cluster GPU Culling Visible Ranges");

        const UINT64 visibleClusterListBytes =
            static_cast<UINT64>(sizeof(uint32_t)) *
            static_cast<UINT64>(requestedVisibleClusterListCapacity);
        auto visibleClusterListDesc = CD3DX12_RESOURCE_DESC::Buffer(
            visibleClusterListBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &visibleClusterListDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(visibleClusterListBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateVisibleClusterListBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(
            visibleClusterListBuffer_.Get(),
            L"Cluster GPU Culling Visible Cluster List");

        const UINT64 meshletDispatchArgumentBytes =
            static_cast<UINT64>(sizeof(GpuIndirectMeshletDispatchArgument)) *
            static_cast<UINT64>(requestedCandidateCommandCapacity);
        auto meshletDispatchArgumentDesc = CD3DX12_RESOURCE_DESC::Buffer(
            meshletDispatchArgumentBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &meshletDispatchArgumentDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(meshletDispatchArgumentBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateMeshletDispatchArgumentBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(
            meshletDispatchArgumentBuffer_.Get(),
            L"Cluster GPU Meshlet Dispatch Arguments");

        auto dispatchArgumentDesc = CD3DX12_RESOURCE_DESC::Buffer(
            sizeof(D3D12_DISPATCH_ARGUMENTS),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &dispatchArgumentDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(dispatchArgumentBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateDispatchArguments")) {
            return false;
        }
        GFX::SetD3D12Name(dispatchArgumentBuffer_.Get(), L"Cluster GPU Cull Dispatch Arguments");

        auto counterDesc = CD3DX12_RESOURCE_DESC::Buffer(
            sizeof(GpuCounterBuffer),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(counterBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateCounterBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(counterBuffer_.Get(), L"Cluster GPU Culling Counters");

        FrameResources& frame0 = frameResources_[0];
        frame0.constantsUploadBuffer = constantsUploadBuffer_;
        frame0.counterResetUploadBuffer = counterResetUploadBuffer_;
        frame0.pageTaskBuffer = pageTaskBuffer_;
        frame0.visibleRangeBuffer = visibleRangeBuffer_;
        frame0.visibleClusterListBuffer = visibleClusterListBuffer_;
        frame0.meshletDispatchArgumentBuffer = meshletDispatchArgumentBuffer_;
        frame0.dispatchArgumentBuffer = dispatchArgumentBuffer_;
        frame0.counterBuffer = counterBuffer_;
        frame0.constantsMapped = constantsMapped_;
        frame0.counterResetMapped = counterResetMapped_;
        frame0.pageTaskBufferState = pageTaskBufferState_;
        frame0.visibleRangeBufferState = visibleRangeBufferState_;
        frame0.visibleClusterListBufferState = visibleClusterListBufferState_;
        frame0.meshletDispatchArgumentBufferState = meshletDispatchArgumentBufferState_;
        frame0.dispatchArgumentBufferState = dispatchArgumentBufferState_;
        frame0.counterBufferState = counterBufferState_;

        for (uint32_t frameIndex = 1; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
            FrameResources& frame = frameResources_[frameIndex];

            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &constantsDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(frame.constantsUploadBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateConstantsUpload.Frame")) {
                return false;
            }
            if (FAILED(frame.constantsUploadBuffer->Map(
                0,
                nullptr,
                reinterpret_cast<void**>(&frame.constantsMapped)))) {
                frame.constantsMapped = nullptr;
                return false;
            }
            GFX::SetD3D12Name(frame.constantsUploadBuffer.Get(), L"Cluster GPU Culling Constants");

            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &counterUploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(frame.counterResetUploadBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateCounterResetUpload.Frame")) {
                return false;
            }
            if (FAILED(frame.counterResetUploadBuffer->Map(
                0,
                nullptr,
                reinterpret_cast<void**>(&frame.counterResetMapped)))) {
                frame.counterResetMapped = nullptr;
                return false;
            }
            GFX::SetD3D12Name(frame.counterResetUploadBuffer.Get(), L"Cluster GPU Culling Counter Reset");

            hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &pageTaskDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.pageTaskBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreatePageTaskBuffer.Frame")) {
                return false;
            }
            GFX::SetD3D12Name(frame.pageTaskBuffer.Get(), L"Cluster GPU Page Tasks");

            hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &visibleDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.visibleRangeBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateVisibleRangeBuffer.Frame")) {
                return false;
            }
            GFX::SetD3D12Name(frame.visibleRangeBuffer.Get(), L"Cluster GPU Culling Visible Ranges");

            hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &visibleClusterListDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.visibleClusterListBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateVisibleClusterListBuffer.Frame")) {
                return false;
            }
            GFX::SetD3D12Name(
                frame.visibleClusterListBuffer.Get(),
                L"Cluster GPU Culling Visible Cluster List");

            hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &meshletDispatchArgumentDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.meshletDispatchArgumentBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateMeshletDispatchArgumentBuffer.Frame")) {
                return false;
            }
            GFX::SetD3D12Name(
                frame.meshletDispatchArgumentBuffer.Get(),
                L"Cluster GPU Meshlet Dispatch Arguments");

            hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &dispatchArgumentDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.dispatchArgumentBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateDispatchArguments.Frame")) {
                return false;
            }
            GFX::SetD3D12Name(frame.dispatchArgumentBuffer.Get(), L"Cluster GPU Cull Dispatch Arguments");

            hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &counterDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(frame.counterBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateCounterBuffer.Frame")) {
                return false;
            }
            GFX::SetD3D12Name(frame.counterBuffer.Get(), L"Cluster GPU Culling Counters");
        }

        const UINT64 historyBytes =
            static_cast<UINT64>(kClusterCullOcclusionHistoryEntryBytes) *
            static_cast<UINT64>(requestedOcclusionHistoryCapacity);
        auto historyDesc = CD3DX12_RESOURCE_DESC::Buffer(
            historyBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &historyDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(occlusionHistoryBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateOcclusionHistoryBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(
            occlusionHistoryBuffer_.Get(),
            L"Cluster GPU Occlusion History");

        auto readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(GpuCounterBuffer));
        for (size_t i = 0; i < counterReadbackSlots_.size(); ++i) {
            hr = device->CreateCommittedResource(
                &readbackHeap,
                D3D12_HEAP_FLAG_NONE,
                &readbackDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(counterReadbackSlots_[i].buffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateCounterReadback")) {
                return false;
            }
            GFX::SetD3D12Name(
                counterReadbackSlots_[i].buffer.Get(),
                L"Cluster GPU Culling Counter Readback");
        }

        pageTaskCapacity_ = requestedPageTaskCapacity;
        visibleRangeCapacity_ = requestedVisibleCapacity;
        visibleClusterListCapacity_ = requestedVisibleClusterListCapacity;
        candidateCommandCapacity_ = requestedCandidateCommandCapacity;
        occlusionHistoryCapacity_ = requestedOcclusionHistoryCapacity;
        return true;
    }

    bool ClusterGpuCullingPass::EnsureFallbackHzb(ID3D12Device* device) {
        if (fallbackHzb_ != nullptr && fallbackHzbSrv_.gpu.ptr != 0) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }

        fallbackHzbSrv_ = {};
        GFX::RetireD3D12ObjectForCurrentFrame(fallbackHzb_, "ClusterGpuCulling.FallbackHzb.Resize");

        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            kClusterCullHzbFallbackFormat,
            1,
            1,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_NONE);
        const D3D12_RESOURCE_STATES initialState =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            initialState,
            nullptr,
            IID_PPV_ARGS(fallbackHzb_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ClusterGpuCulling::CreateFallbackHzb")) {
            return false;
        }
        GFX::SetD3D12Name(fallbackHzb_.Get(), L"Cluster GPU Culling Fallback HZB");

        fallbackHzbSrv_ = CreateFixedTexture2DSrvDescriptor(
            device,
            fallbackHzb_.Get(),
            kClusterCullHzbFallbackFormat,
            GFX::DESCRIPTOR::kClusterCullFallbackHzbSrv);
        if (!fallbackHzbSrv_.IsValid() || fallbackHzbSrv_.gpu.ptr == 0) {
            fallbackHzbSrv_ = {};
            fallbackHzb_.Reset();
            return false;
        }
        return true;
    }

    void ClusterGpuCullingPass::ResetFrameStats() {
        const bool initialized = rootSignature_ != nullptr &&
            expandPageTasksPipelineState_ != nullptr &&
            finalizeDispatchPipelineState_ != nullptr &&
            finalizeMeshletDispatchPipelineState_ != nullptr &&
            cullPageTasksPipelineState_ != nullptr &&
            constantsUploadBuffer_ != nullptr &&
            counterResetUploadBuffer_ != nullptr &&
            pageTaskBuffer_ != nullptr &&
            visibleRangeBuffer_ != nullptr &&
            visibleClusterListBuffer_ != nullptr &&
            meshletDispatchArgumentBuffer_ != nullptr &&
            dispatchArgumentBuffer_ != nullptr &&
            counterBuffer_ != nullptr &&
            dispatchCommandSignature_ != nullptr;
        stats_ = {};
        stats_.initialized = initialized;
        stats_.psoReady =
            expandPageTasksPipelineState_ != nullptr &&
            finalizeDispatchPipelineState_ != nullptr &&
            finalizeMeshletDispatchPipelineState_ != nullptr &&
            cullPageTasksPipelineState_ != nullptr;
        stats_.inputBufferReady = true;
        stats_.pageTaskBufferReady = pageTaskBuffer_ != nullptr;
        stats_.visibleRangeBufferReady = visibleRangeBuffer_ != nullptr;
        stats_.visibleClusterListBufferReady = visibleClusterListBuffer_ != nullptr;
        stats_.dispatchArgumentBufferReady = dispatchArgumentBuffer_ != nullptr;
        stats_.counterBufferReady = counterBuffer_ != nullptr;
        stats_.inputCapacity = 0;
        stats_.pageTaskCapacity = pageTaskCapacity_;
        stats_.visibleRangeCapacity = visibleRangeCapacity_;
        stats_.visibleClusterListCapacity = visibleClusterListCapacity_;
        stats_.candidateCommandCapacity = candidateCommandCapacity_;
        stats_.occlusionHistoryCapacity = occlusionHistoryCapacity_;
        stats_.threadGroupSize = kThreadGroupSize;
        stats_.gpuCounterReadbackReady = counterReadbackSlots_[0].buffer != nullptr;
        stats_.gpuCounterReadbackValid = latestGpuCountersValid_;
        stats_.occlusionHistoryReady = occlusionHistoryBuffer_ != nullptr &&
            occlusionHistoryCapacity_ != 0u;
        if (latestGpuCountersValid_) {
            const GpuCounters& globalCounters = latestGpuCounters_.global;
            stats_.gpuInputCount = globalCounters.inputCount;
            stats_.gpuPageTaskCount = globalCounters.pageTaskCount;
            stats_.gpuPageTaskOverflowCount = globalCounters.pageTaskOverflowCount;
            stats_.gpuVisibleRangeCount = globalCounters.visibleRangeCount;
            stats_.gpuVisibleClusterCount = globalCounters.visibleClusterCount;
            stats_.gpuOverflowCount = globalCounters.overflowCount;
            stats_.gpuInputFrustumCulledCount =
                globalCounters.inputFrustumCulledCount;
            stats_.gpuPageTestedCount =
                globalCounters.pageTestedCount;
            stats_.gpuPageFrustumCulledCount =
                globalCounters.pageFrustumCulledCount;
            stats_.gpuPageOcclusionTestedCount =
                globalCounters.pageOcclusionTestedCount;
            stats_.gpuPageOcclusionCulledCount =
                globalCounters.pageOcclusionCulledCount;
            stats_.gpuClusterTestedCount =
                globalCounters.clusterTestedCount;
            stats_.gpuClusterFrustumCulledCount =
                globalCounters.clusterFrustumCulledCount;
            stats_.gpuClusterOcclusionTestedCount =
                globalCounters.clusterOcclusionTestedCount;
            stats_.gpuClusterOcclusionCulledCount =
                globalCounters.clusterOcclusionCulledCount;
            stats_.gpuHzbPassRejectedCount =
                globalCounters.hzbPassRejectedCount;
            stats_.gpuHzbAabbRejectedCount =
                globalCounters.hzbAabbRejectedCount;
            stats_.gpuHzbSphereRejectedCount =
                globalCounters.hzbSphereRejectedCount;
            stats_.gpuHzbQueryAcceptedCount =
                globalCounters.hzbQueryAcceptedCount;
            stats_.gpuHzbTryCount =
                globalCounters.hzbTryCount;
            stats_.gpuHzbAllowedCount =
                globalCounters.hzbAllowedCount;
            stats_.gpuHzbInvalidRejectedCount =
                globalCounters.hzbInvalidRejectedCount;
            stats_.gpuHzbNearPlaneRejectedCount =
                globalCounters.hzbNearPlaneRejectedCount;
            stats_.gpuHzbOffscreenRejectedCount =
                globalCounters.hzbOffscreenRejectedCount;
            stats_.gpuHzbLargeRectCount =
                globalCounters.hzbLargeRectCount;
            stats_.gpuHzbAabbAcceptedCount =
                globalCounters.hzbAabbAcceptedCount;
            stats_.gpuHzbSphereAcceptedCount =
                globalCounters.hzbSphereAcceptedCount;
            stats_.gpuHzbRawOccludedCount =
                globalCounters.hzbRawOccludedCount;
            stats_.gpuHzbTemporalPendingCount =
                globalCounters.hzbTemporalPendingCount;
            stats_.gpuHzbTemporalConfirmedCount =
                globalCounters.hzbTemporalConfirmedCount;
            stats_.gpuHzbTemporalResetCount =
                globalCounters.hzbTemporalResetCount;
            stats_.gpuHzbTemporalCollisionCount =
                globalCounters.hzbTemporalCollisionCount;
            stats_.gpuHzbLargeRectSkippedCount =
                globalCounters.hzbLargeRectSkippedCount;
            stats_.gpuPageHzbSmallScreenSkippedCount =
                globalCounters.pageHzbSmallScreenSkippedCount;
            stats_.gpuClusterHzbSmallScreenSkippedCount =
                globalCounters.clusterHzbSmallScreenSkippedCount;
            stats_.gpuConeSkippedDoubleSidedCount =
                globalCounters.coneSkippedDoubleSidedCount;
            stats_.gpuConeSkippedMaterialCount =
                globalCounters.coneSkippedMaterialCount;
            stats_.gpuClusterHzbLargeScreenSkippedCount =
                globalCounters.clusterHzbLargeScreenSkippedCount;
            stats_.gpuHzbBudgetSkippedCount =
                globalCounters.hzbBudgetSkippedCount;
            stats_.gpuClusterConeCulledCount =
                globalCounters.clusterConeCulledCount;
            stats_.gpuClusterConeTestedCount =
                globalCounters.clusterConeTestedCount;
            stats_.gpuDoubleSidedClusterCount =
                globalCounters.doubleSidedClusterCount;
            stats_.gpuBackFaceDrawCommandCount =
                globalCounters.backFaceDrawCommandCount;
            stats_.gpuDoubleSidedDrawCommandCount =
                globalCounters.doubleSidedDrawCommandCount;
            stats_.gpuDrawCommandCount =
                stats_.gpuBackFaceDrawCommandCount +
                stats_.gpuDoubleSidedDrawCommandCount;
            stats_.gpuBackFaceDrawCommandOverflowCount =
                globalCounters.backFaceDrawCommandOverflowCount;
            stats_.gpuDoubleSidedDrawCommandOverflowCount =
                globalCounters.doubleSidedDrawCommandOverflowCount;
            stats_.gpuDrawCommandOverflowCount =
                stats_.gpuBackFaceDrawCommandOverflowCount +
                stats_.gpuDoubleSidedDrawCommandOverflowCount;
            stats_.gpuMergedGapCount =
                globalCounters.mergedGapCount;
            stats_.gpuMergedGapIndexCount =
                globalCounters.mergedGapIndexCount;
            stats_.gpuPacketRangeCount =
                globalCounters.packetRangeCount;
            stats_.gpuPacketClusterCount =
                globalCounters.packetClusterCount;
            stats_.gpuVisibleClusterListReservedCount =
                globalCounters.visibleClusterListReservedCount;
            stats_.gpuVisibleClusterListOverflowCount =
                globalCounters.visibleClusterListOverflowCount;
            stats_.gpuLod0SelectedCount =
                globalCounters.lod0SelectedCount;
            stats_.gpuLod1SelectedCount =
                globalCounters.lod1SelectedCount;
            stats_.gpuLod2SelectedCount =
                globalCounters.lod2SelectedCount;
            stats_.gpuLod3PlusSelectedCount =
                globalCounters.lod3PlusSelectedCount;

            for (size_t passIndex = 0;
                passIndex < kClusterGpuCullingPassKindCount;
                ++passIndex) {

                const GpuPassCounters& passCounters =
                    latestGpuCounters_.passes[passIndex];
                ClusterGpuCullingPassStats::PassOutputStats& passStats =
                    stats_.passOutputs[passIndex];
                passStats.gpuBackFaceDrawCommandCount =
                    passCounters.backFaceDrawCommandCount;
                passStats.gpuDoubleSidedDrawCommandCount =
                    passCounters.doubleSidedDrawCommandCount;
                passStats.gpuDrawCommandCount =
                    passStats.gpuBackFaceDrawCommandCount +
                    passStats.gpuDoubleSidedDrawCommandCount;
                passStats.gpuBackFaceDrawCommandOverflowCount =
                    passCounters.backFaceDrawCommandOverflowCount;
                passStats.gpuDoubleSidedDrawCommandOverflowCount =
                    passCounters.doubleSidedDrawCommandOverflowCount;
                passStats.gpuDrawCommandOverflowCount =
                    passStats.gpuBackFaceDrawCommandOverflowCount +
                    passStats.gpuDoubleSidedDrawCommandOverflowCount;
                passStats.gpuCounterReadbackValid = true;
            }
        }
    }

    void ClusterGpuCullingPass::CollectCounterReadback(CounterReadbackSlot& slot) {
        if (!slot.resolved || slot.buffer == nullptr) {
            return;
        }

        const D3D12_RANGE readRange{ 0, sizeof(GpuCounterBuffer) };
        void* mapped = nullptr;
        if (FAILED(slot.buffer->Map(0, &readRange, &mapped)) || mapped == nullptr) {
            return;
        }
        latestGpuCounters_ = *static_cast<const GpuCounterBuffer*>(mapped);
        const D3D12_RANGE writeRange{ 0, 0 };
        slot.buffer->Unmap(0, &writeRange);
        slot.resolved = false;
        latestGpuCountersValid_ = true;
    }

    void ClusterGpuCullingPass::QueueCounterReadback(ID3D12GraphicsCommandList* commandList) {
        if (commandList == nullptr || counterBuffer_ == nullptr) {
            return;
        }

        CounterReadbackSlot& slot =
            counterReadbackSlots_[counterReadbackWriteIndex_ % counterReadbackSlots_.size()];
        ++counterReadbackWriteIndex_;
        if (slot.buffer == nullptr) {
            return;
        }

        // GPU 側の可視数は次以降のフレームで読む。ここでは待機せず readback だけ積む。
        commandList->CopyBufferRegion(
            slot.buffer.Get(),
            0,
            counterBuffer_.Get(),
            0,
            sizeof(GpuCounterBuffer));
        slot.resolved = true;
    }

    void ClusterGpuCullingPass::BuildRangeStats(
        const ClusterGpuCullingSourceRange* ranges,
        size_t rangeCount) {

        if (ranges == nullptr || rangeCount == 0) {
            return;
        }

        for (size_t rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex) {
            const ClusterGpuCullingSourceRange& range = ranges[rangeIndex];
            if (range.instanceCount == 0u) {
                continue;
            }

            ClusterGpuCullingPassStats::PassOutputStats& passStats =
                stats_.passOutputs[CullPassIndex(range.passKind)];
            stats_.sourceInstanceCount += range.instanceCount;
            passStats.sourceInstanceCount += range.instanceCount;
            const uint32_t explicitBucketCount =
                range.singleSidedInstanceCount + range.doubleSidedInstanceCount;
            if (explicitBucketCount > 0u) {
                stats_.sourceSingleSidedInstanceCount += range.singleSidedInstanceCount;
                stats_.sourceDoubleSidedInstanceCount += range.doubleSidedInstanceCount;
                passStats.sourceSingleSidedInstanceCount +=
                    range.singleSidedInstanceCount;
                passStats.sourceDoubleSidedInstanceCount +=
                    range.doubleSidedInstanceCount;
            }
            ++passStats.submittedDrawSeedCount;
            ++stats_.sourcePageTaskCount;
            ++stats_.submittedPageTaskCount;
            ++stats_.submittedDrawSeedCount;
        }

        // GPU scene driven の主線では、CPU は候補圧縮を行わない。
        // ここでは dispatch seed 数だけを記録し、実際の candidate/draw 数は GPU counter で読む。
        stats_.candidateInstanceCount = stats_.sourceInstanceCount;
        stats_.submittedInstanceCount = stats_.sourceInstanceCount;
    }

    bool ClusterGpuCullingPass::Dispatch(
        ID3D12GraphicsCommandList* commandList,
        const MATH::Mat4& viewProj,
        const MATH::Vec3& cameraPosition,
        D3D12_GPU_DESCRIPTOR_HANDLE clusterGeometryPoolSrv,
        D3D12_GPU_VIRTUAL_ADDRESS surfaceGpuSceneGpuAddress,
        const ClusterGpuCullingSourceRange* ranges,
        size_t rangeCount,
        const ClusterGpuDepthOcclusionDesc& depthOcclusion,
        bool collectCounterReadback) {

        const auto& debugConfig = GFX::GetGfxDebugConfig();
        const bool counterReadbackEnabled =
            collectCounterReadback &&
            debugConfig.enableClusterGpuCullCounterReadback;
        const bool debugCountersEnabled =
            collectCounterReadback &&
            (counterReadbackEnabled ||
                debugConfig.enableClusterGpuCullDebugCounters);
        BeginFrame(counterReadbackEnabled);

        if (!stats_.initialized ||
            commandList == nullptr ||
            constantsMapped_ == nullptr ||
            counterResetMapped_ == nullptr ||
            clusterGeometryPoolSrv.ptr == 0 ||
            surfaceGpuSceneGpuAddress == 0 ||
            ranges == nullptr ||
            rangeCount == 0 ||
            rangeCount > kClusterGpuCullingMaxSourceRangeCount) {
            return false;
        }

        BuildRangeStats(ranges, rangeCount);
        if (stats_.sourceInstanceCount == 0) {
            return false;
        }

        const bool hzbOcclusionAvailable =
            depthOcclusion.enabled &&
            depthOcclusion.hzbSrv.ptr != 0 &&
            depthOcclusion.hzbWidth != 0 &&
            depthOcclusion.hzbHeight != 0 &&
            depthOcclusion.hzbViewProjValid;
        const bool hzbOcclusionEnabled = hzbOcclusionAvailable;
        const D3D12_GPU_DESCRIPTOR_HANDLE hzbSrv =
            hzbOcclusionEnabled
                ? depthOcclusion.hzbSrv
                : fallbackHzbSrv_.gpu;
        if (hzbSrv.ptr == 0) {
            return false;
        }
        if (occlusionHistoryBuffer_ == nullptr || occlusionHistoryCapacity_ == 0u) {
            return false;
        }
        bool hzbOcclusionThisFrame = hzbOcclusionEnabled;
        stats_.hzbOcclusionEnabled = hzbOcclusionThisFrame;
        stats_.hzbOcclusionWidth = hzbOcclusionThisFrame ? depthOcclusion.hzbWidth : 1u;
        stats_.hzbOcclusionHeight = hzbOcclusionThisFrame ? depthOcclusion.hzbHeight : 1u;
        stats_.hzbOcclusionMipCount =
            hzbOcclusionThisFrame ? (std::max)(1u, depthOcclusion.hzbMipCount) : 1u;

        GpuConstants baseConstants{};
        baseConstants.viewProj = viewProj;
        baseConstants.hzbViewProj =
            hzbOcclusionThisFrame ? depthOcclusion.hzbViewProj : viewProj;
        baseConstants.cameraPosition = {
            cameraPosition.x,
            cameraPosition.y,
            cameraPosition.z,
            0.0f
        };
        baseConstants.visibleRangeCapacity = static_cast<uint32_t>(visibleRangeCapacity_);
        baseConstants.candidateCommandCapacity =
            static_cast<uint32_t>(candidateCommandCapacity_);
        baseConstants.enableFrustumCull = 1u;
        baseConstants.candidateCommandBucketCapacity =
            static_cast<uint32_t>(GetCandidateCommandBucketCapacity());
        baseConstants.clusterSrvPoolBegin = GFX::DESCRIPTOR::kSystemSrvDynamicBegin;
        baseConstants.clusterSrvPoolCount = GFX::DESCRIPTOR::kSystemSrvDynamicCount;
        baseConstants.enableConeCull = 1u;
        baseConstants.enableDebugCounters = debugCountersEnabled ? 1u : 0u;
        baseConstants.pageTaskCapacity = static_cast<uint32_t>(pageTaskCapacity_);
        // GPU 側の draw args 圧縮は小さな index gap だけを吸収し、過剰な overdraw を上限で止める。
        baseConstants.mergeGapIndexLimit = kClusterCullMergeGapIndexLimit;
        baseConstants.mergeRunGapIndexBudget = kClusterCullMergeRunGapIndexBudget;
        baseConstants.mergeMaxIndexSpan = kClusterCullMergeMaxIndexSpan;
        baseConstants.mergeClusterGapLimit = kClusterCullMergeClusterGapLimit;
        baseConstants.lodTargetErrorNdc = kClusterCullLodTargetErrorNdc;
        baseConstants.enableLodErrorSelection = 1u;
        baseConstants.lodTransitionRelaxPerLevel =
            kClusterCullLodTransitionRelaxPerLevel;
        baseConstants.lodErrorRelaxPerLevel =
            kClusterCullLodErrorRelaxPerLevel;
        baseConstants.pageTaskGroupSize = kClusterCullPageTaskGroupSize;
        baseConstants.clusterHzbMinScreenPixels =
            kClusterCullClusterHzbMinScreenPixels;
        baseConstants.enableHzbOcclusion = hzbOcclusionThisFrame ? 1u : 0u;
        baseConstants.hzbWidth = stats_.hzbOcclusionWidth;
        baseConstants.hzbHeight = stats_.hzbOcclusionHeight;
        baseConstants.hzbMipCount = stats_.hzbOcclusionMipCount;
        baseConstants.hzbDepthBias = kClusterCullHzbDepthBias;
        baseConstants.hzbMaxScreenRadiusPixels = kClusterCullHzbMaxScreenRadiusPixels;
        baseConstants.occlusionHistoryCapacity =
            static_cast<uint32_t>(occlusionHistoryCapacity_);
        baseConstants.temporalFrameIndex =
            static_cast<uint32_t>(TIME::GetFrameContext().frameIndex);
        baseConstants.hzbOcclusionConfirmFrames =
            kClusterCullHzbOcclusionConfirmFrames;
        baseConstants.hzbAllowLargeRectOcclusion = 0u;
        baseConstants.hzbTestBudget = kClusterCullHzbTestBudget;
        baseConstants.visibleClusterListCapacity =
            static_cast<uint32_t>((std::min)(
                visibleClusterListCapacity_,
                static_cast<size_t>(UINT32_MAX)));
        // Cluster mainline は page / instance の粗い可視性だけを CS で確定し、
        // meshlet 単位の frustum / cone 精査は AS に所有させる。
        baseConstants.meshletFineCullingOwner =
            kClusterMeshletFineCullingOwnerAmplificationShader;
        stats_.debugCountersEnabled = baseConstants.enableDebugCounters != 0u;
        stats_.meshletFineCullingDeferredToAmplificationShader =
            baseConstants.meshletFineCullingOwner ==
                kClusterMeshletFineCullingOwnerAmplificationShader;
        stats_.lodTargetErrorNdc = baseConstants.lodTargetErrorNdc;
        stats_.lodTransitionRelaxPerLevel =
            baseConstants.lodTransitionRelaxPerLevel;
        stats_.lodErrorRelaxPerLevel =
            baseConstants.lodErrorRelaxPerLevel;

        const UINT constantsStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(GpuConstants));
        std::array<UINT, kClusterGpuCullingMaxSourceRangeCount> expandGroupCounts{};
        size_t activeRangeCount = 0;
        for (size_t rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex) {
            const ClusterGpuCullingSourceRange& range = ranges[rangeIndex];
            if (range.instanceCount == 0u) {
                continue;
            }

            GpuConstants constants = baseConstants;
            constants.inputCount = range.instanceCount;
            constants.surfaceGpuSceneBaseIndex = range.surfaceGpuSceneBaseIndex;
            constants.passKind = static_cast<uint32_t>(range.passKind);
            auto* constantsSlot = reinterpret_cast<GpuConstants*>(
                constantsMapped_ +
                static_cast<size_t>(constantsStride) * activeRangeCount);
            *constantsSlot = constants;
            expandGroupCounts[activeRangeCount] =
                static_cast<UINT>(
                    (static_cast<size_t>(range.instanceCount) +
                        kThreadGroupSize - 1u) /
                    kThreadGroupSize);
            ++activeRangeCount;
        }
        if (activeRangeCount == 0) {
            return false;
        }

        GpuCounterBuffer counters{};
        *counterResetMapped_ = counters;

        GFX::PIX::ScopedGpuEvent pix(
            commandList,
            GFX::PIX::kColorRender,
            "ClusterGpuCulling.BuildMeshletCandidates");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuCull(
            commandList,
            GFX::GPU_PROFILE::Pass::ClusterCull);

        if (pageTaskBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                pageTaskBuffer_.Get(),
                pageTaskBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            pageTaskBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (visibleRangeBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                visibleRangeBuffer_.Get(),
                visibleRangeBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            visibleRangeBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (visibleClusterListBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                visibleClusterListBuffer_.Get(),
                visibleClusterListBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            visibleClusterListBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (meshletDispatchArgumentBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                meshletDispatchArgumentBuffer_.Get(),
                meshletDispatchArgumentBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (dispatchArgumentBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                dispatchArgumentBuffer_.Get(),
                dispatchArgumentBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (counterBufferState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                counterBuffer_.Get(),
                counterBufferState_,
                D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, &barrier);
            counterBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        if (occlusionHistoryBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                occlusionHistoryBuffer_.Get(),
                occlusionHistoryBufferState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &barrier);
            occlusionHistoryBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        commandList->CopyBufferRegion(
            counterBuffer_.Get(),
            0,
            counterResetUploadBuffer_.Get(),
            0,
            sizeof(GpuCounterBuffer));

        auto counterToUav = CD3DX12_RESOURCE_BARRIER::Transition(
            counterBuffer_.Get(),
            counterBufferState_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->ResourceBarrier(1, &counterToUav);
        counterBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        commandList->SetComputeRootSignature(rootSignature_.Get());
        commandList->SetComputeRootShaderResourceView(
            1,
            surfaceGpuSceneGpuAddress);
        commandList->SetComputeRootUnorderedAccessView(
            2,
            visibleRangeBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            3,
            counterBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootDescriptorTable(4, clusterGeometryPoolSrv);
        commandList->SetComputeRootUnorderedAccessView(
            5,
            pageTaskBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            6,
            dispatchArgumentBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            7,
            meshletDispatchArgumentBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootDescriptorTable(8, hzbSrv);
        commandList->SetComputeRootUnorderedAccessView(
            9,
            occlusionHistoryBuffer_->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            10,
            visibleClusterListBuffer_->GetGPUVirtualAddress());

        size_t expandWorkgroupCount = 0;
        {
            GFX::PIX::ScopedGpuEvent expandPix(
                commandList,
                GFX::PIX::kColorRender,
                "ClusterGpuCulling.ExpandPageTasks");
            commandList->SetPipelineState(expandPageTasksPipelineState_.Get());
            for (size_t slot = 0; slot < activeRangeCount; ++slot) {
                commandList->SetComputeRootConstantBufferView(
                    0,
                    constantsUploadBuffer_->GetGPUVirtualAddress() +
                        static_cast<UINT64>(constantsStride) *
                        static_cast<UINT64>(slot));
                commandList->Dispatch(expandGroupCounts[slot], 1u, 1u);
                expandWorkgroupCount += expandGroupCounts[slot];
            }
        }

        D3D12_RESOURCE_BARRIER expandBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(pageTaskBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(counterBuffer_.Get()),
        };
        commandList->ResourceBarrier(
            static_cast<UINT>(std::size(expandBarriers)),
            expandBarriers);

        {
            GFX::PIX::ScopedGpuEvent finalizePix(
                commandList,
                GFX::PIX::kColorRender,
                "ClusterGpuCulling.FinalizePageTaskDispatch");
            commandList->SetPipelineState(finalizeDispatchPipelineState_.Get());
            commandList->Dispatch(1u, 1u, 1u);
        }

        D3D12_RESOURCE_BARRIER finalizeBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(dispatchArgumentBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(counterBuffer_.Get()),
        };
        commandList->ResourceBarrier(
            static_cast<UINT>(std::size(finalizeBarriers)),
            finalizeBarriers);

        auto dispatchArgsToIndirect = CD3DX12_RESOURCE_BARRIER::Transition(
            dispatchArgumentBuffer_.Get(),
            dispatchArgumentBufferState_,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        commandList->ResourceBarrier(1, &dispatchArgsToIndirect);
        dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

        {
            GFX::PIX::ScopedGpuEvent cullPix(
                commandList,
                GFX::PIX::kColorRender,
                "ClusterGpuCulling.CullPageTasks");
            commandList->SetPipelineState(cullPageTasksPipelineState_.Get());
            commandList->ExecuteIndirect(
                dispatchCommandSignature_.Get(),
                1u,
                dispatchArgumentBuffer_.Get(),
                0,
                nullptr,
                0);
        }

        std::array<D3D12_RESOURCE_BARRIER, 7> barriers{};
        UINT barrierCount = 0;
        const auto addUavBarrier = [&](ID3D12Resource* resource) {
            if (resource != nullptr && barrierCount < barriers.size()) {
                barriers[barrierCount++] =
                    CD3DX12_RESOURCE_BARRIER::UAV(resource);
            }
        };
        addUavBarrier(pageTaskBuffer_.Get());
        addUavBarrier(visibleRangeBuffer_.Get());
        addUavBarrier(visibleClusterListBuffer_.Get());
        addUavBarrier(counterBuffer_.Get());
        addUavBarrier(meshletDispatchArgumentBuffer_.Get());
        addUavBarrier(occlusionHistoryBuffer_.Get());
        commandList->ResourceBarrier(barrierCount, barriers.data());

        {
            GFX::PIX::ScopedGpuEvent meshletFinalizePix(
                commandList,
                GFX::PIX::kColorRender,
                "ClusterGpuCulling.FinalizeMeshletDispatch");
            commandList->SetPipelineState(finalizeMeshletDispatchPipelineState_.Get());
            commandList->Dispatch(1u, 1u, 1u);
        }

        D3D12_RESOURCE_BARRIER meshletFinalizeBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(meshletDispatchArgumentBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(counterBuffer_.Get()),
        };
        commandList->ResourceBarrier(
            static_cast<UINT>(std::size(meshletFinalizeBarriers)),
            meshletFinalizeBarriers);

        if (counterReadbackEnabled) {
            auto counterToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
                counterBuffer_.Get(),
                counterBufferState_,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
            commandList->ResourceBarrier(1, &counterToCopy);
            counterBufferState_ = D3D12_RESOURCE_STATE_COPY_SOURCE;
            QueueCounterReadback(commandList);
        }

        std::array<D3D12_RESOURCE_BARRIER, 4> readyBarriers{};
        UINT readyBarrierCount = 0;
        readyBarriers[readyBarrierCount++] =
            CD3DX12_RESOURCE_BARRIER::Transition(
                visibleRangeBuffer_.Get(),
                visibleRangeBufferState_,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        readyBarriers[readyBarrierCount++] =
            CD3DX12_RESOURCE_BARRIER::Transition(
                visibleClusterListBuffer_.Get(),
                visibleClusterListBufferState_,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        readyBarriers[readyBarrierCount++] =
            CD3DX12_RESOURCE_BARRIER::Transition(
                meshletDispatchArgumentBuffer_.Get(),
                meshletDispatchArgumentBufferState_,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        readyBarriers[readyBarrierCount++] =
            CD3DX12_RESOURCE_BARRIER::Transition(
                counterBuffer_.Get(),
                counterBufferState_,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        commandList->ResourceBarrier(readyBarrierCount, readyBarriers.data());
        visibleRangeBufferState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        visibleClusterListBufferState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        counterBufferState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

        stats_.dispatchCount = activeRangeCount + 3u;
        stats_.workgroupCount = expandWorkgroupCount + 2u;
        StoreActiveFrameResourceStates();
        return true;
    }

    const ClusterGpuCullingPassStats& ClusterGpuCullingPass::GetStats() const {
        return stats_;
    }

    const ClusterGpuCullingPassStats::PassOutputStats&
        ClusterGpuCullingPass::GetPassStats(
            ClusterGpuCullingPassKind passKind) const {

        return stats_.passOutputs[CullPassIndex(passKind)];
    }

    ID3D12Resource* ClusterGpuCullingPass::GetVisibleRangeBuffer() const {
        return visibleRangeBuffer_.Get();
    }

    ID3D12Resource* ClusterGpuCullingPass::GetVisibleClusterListBuffer() const {
        return visibleClusterListBuffer_.Get();
    }

    ID3D12Resource* ClusterGpuCullingPass::GetMeshletDispatchArgumentBuffer() const {
        return meshletDispatchArgumentBuffer_.Get();
    }

    ID3D12Resource* ClusterGpuCullingPass::GetCounterBuffer() const {
        return counterBuffer_.Get();
    }

    ID3D12CommandSignature* ClusterGpuCullingPass::GetMeshletDispatchCommandSignature() const {
        return meshletDispatchCommandSignature_.Get();
    }

    size_t ClusterGpuCullingPass::GetCandidateCommandBucketCapacity() const {
        return candidateCommandCapacity_ /
            (kClusterGpuCullingPassKindCount * kGeometryCullModeBucketCount);
    }

    UINT64 ClusterGpuCullingPass::GetMeshletDispatchArgumentBufferOffset(
        ClusterGpuCullingPassKind passKind,
        GeometryCullModeBucket bucket) const {

        const UINT64 passBase =
            static_cast<UINT64>(CullPassIndex(passKind)) *
            static_cast<UINT64>(kGeometryCullModeBucketCount) *
            static_cast<UINT64>(GetCandidateCommandBucketCapacity()) *
            static_cast<UINT64>(sizeof(GpuIndirectMeshletDispatchArgument));
        return
            passBase +
            static_cast<UINT64>(CullBucketIndex(bucket)) *
            static_cast<UINT64>(GetCandidateCommandBucketCapacity()) *
            static_cast<UINT64>(sizeof(GpuIndirectMeshletDispatchArgument));
    }

    UINT64 ClusterGpuCullingPass::GetCandidateCommandCounterOffset(
        ClusterGpuCullingPassKind passKind,
        GeometryCullModeBucket bucket) const {

        const UINT64 passOffset =
            static_cast<UINT64>(offsetof(GpuCounterBuffer, passes)) +
            static_cast<UINT64>(CullPassIndex(passKind)) *
                static_cast<UINT64>(sizeof(GpuPassCounters));
        switch (bucket) {
        case GeometryCullModeBucket::DoubleSided:
            return passOffset + offsetof(
                GpuPassCounters,
                doubleSidedDrawCommandCount);
        case GeometryCullModeBucket::BackFace:
        default:
            return passOffset + offsetof(
                GpuPassCounters,
                backFaceDrawCommandCount);
        }
    }

} // namespace HIKARI::RENDER3D::CLUSTER
