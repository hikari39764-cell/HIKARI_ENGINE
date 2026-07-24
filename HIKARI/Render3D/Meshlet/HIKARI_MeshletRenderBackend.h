#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenCommandBucket.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::RENDER3D::MESHLET {

    constexpr size_t kForwardOpaquePipelineVariantCount =
        static_cast<size_t>(ForwardShadingCostMode::NoMaterialExtras) + 1u;

    enum class MeshletPipelineKind : uint32_t {
        ForwardOpaque,
        ForwardDepthAware,
        ForwardTransparent,
        Shadow,
        GeometryAux,
        DepthPrepass,
    };

    namespace MeshletPipelineMask {
        constexpr uint32_t ForwardOpaque = 1u << 0;
        constexpr uint32_t ForwardDepthAware = 1u << 1;
        constexpr uint32_t ForwardTransparent = 1u << 2;
        constexpr uint32_t Shadow = 1u << 3;
        constexpr uint32_t GeometryAux = 1u << 4;
        constexpr uint32_t DepthPrepass = 1u << 5;
        constexpr uint32_t MainRenderer =
            ForwardOpaque |
            ForwardDepthAware |
            ForwardTransparent |
            GeometryAux |
            DepthPrepass;
        constexpr uint32_t ShadowRenderer = Shadow;
        constexpr uint32_t All = MainRenderer | ShadowRenderer;
    }

    struct MeshletRenderBackendStats {
        bool initialized = false;
        bool shaderModel65Supported = false;
        bool meshShaderSupported = false;
        bool meshShaderPipelineStatsSupported = false;
        bool shaderCompileReady = false;
        bool dispatchArgumentBufferReady = false;
        bool dispatchCommandSignatureReady = false;
        bool forwardPipelineReady = false;
        bool depthAwarePipelineReady = false;
        bool transparentPipelineReady = false;
        bool shadowPipelineReady = false;
        bool geometryAuxPipelineReady = false;
        bool depthPrepassPipelineReady = false;
        bool pipelineReady = false;
        uint32_t meshShaderTier = 0;
        size_t pipelineCreateRequestCount = 0;
        size_t pipelineCreateReadyCount = 0;
        size_t requestedDispatchCount = 0;
        size_t submittedDispatchCount = 0;
        size_t skippedDispatchCount = 0;
        size_t submitCallCount = 0;
        size_t skippedBucketCount = 0;
        size_t forwardSubmittedDispatchCount = 0;
        size_t geometryAuxSubmittedDispatchCount = 0;
        size_t depthPrepassSubmittedDispatchCount = 0;
        size_t shadowSubmittedDispatchCount = 0;
        size_t backFaceSubmitCallCount = 0;
        size_t doubleSidedSubmitCallCount = 0;
    };

    struct MeshletRenderExecutionContext {
        ID3D12GraphicsCommandList* commandList = nullptr;
        GPUDRIVEN::GpuDrivenPassKind pass = GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque;
        const GPUDRIVEN::GpuVisibilityResult* visibility = nullptr;
        const GPUDRIVEN::GpuDrivenDrawCommandRange* drawCommandRange = nullptr;
        MeshletPipelineKind pipelineKind = MeshletPipelineKind::ForwardOpaque;
        GFX::GPU_PROFILE::Pass profilePass = GFX::GPU_PROFILE::Pass::Count;
    };

    class MeshletRenderBackend final {
    public:
        using PipelineBucketArray =
            std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, GPUDRIVEN::kGpuDrivenCommandBucketCount>;
        using ForwardPipelineVariantArray =
            std::array<PipelineBucketArray, kForwardOpaquePipelineVariantCount>;

        bool Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature);
        bool Initialize(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            uint32_t pipelineMask);
        void Reset();
        void ResetFrame();
        bool Execute(const MeshletRenderExecutionContext& ctx);

        const MeshletRenderBackendStats& GetStats() const;
        ID3D12PipelineState* GetPipelineState(
            MeshletPipelineKind kind,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) const;

    private:
        ForwardPipelineVariantArray forwardPipelineStates_{};
        PipelineBucketArray depthAwarePipelineStates_{};
        PipelineBucketArray transparentPipelineStates_{};
        PipelineBucketArray shadowPipelineStates_{};
        PipelineBucketArray geometryAuxPipelineStates_{};
        PipelineBucketArray depthPrepassPipelineStates_{};
        uint32_t pipelineMask_ = MeshletPipelineMask::All;
        MeshletRenderBackendStats stats_{};
    };

} // namespace HIKARI::RENDER3D::MESHLET
