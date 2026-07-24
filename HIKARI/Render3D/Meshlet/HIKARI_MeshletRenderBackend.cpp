#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuPipelineStatsProfiler.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
namespace HIKARI::RENDER3D::MESHLET {

    namespace {
        using ForwardPipelineVariantArray =
            MeshletRenderBackend::ForwardPipelineVariantArray;

        constexpr std::array<const wchar_t*, kForwardOpaquePipelineVariantCount> kForwardOpaqueShaderPaths = {
            L"HIKARI/Shaders/Render3D_GpuDrivenOpaqueFullPS.hlsl",
            L"HIKARI/Shaders/Render3D_GpuDrivenOpaqueAlbedoOnlyPS.hlsl",
            L"HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoNormalMapPS.hlsl",
            L"HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoShadowPS.hlsl",
            L"HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoSsaoPS.hlsl",
            L"HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoMaterialExtrasPS.hlsl",
        };

        constexpr std::array<const wchar_t*, kForwardOpaquePipelineVariantCount> kForwardOpaqueVariantNames = {
            L"Full",
            L"AlbedoOnly",
            L"NoNormalMap",
            L"NoShadow",
            L"NoSSAO",
            L"NoMaterialExtras",
        };

        size_t ForwardPipelineVariantIndex(ForwardShadingCostMode mode) {
            const uint8_t raw = static_cast<uint8_t>(mode);
            const uint8_t maxRaw =
                static_cast<uint8_t>(ForwardShadingCostMode::NoMaterialExtras);
            return raw <= maxRaw ? static_cast<size_t>(raw) : 0u;
        }

        uint32_t PipelineMaskForKind(MeshletPipelineKind kind) {
            switch (kind) {
            case MeshletPipelineKind::ForwardDepthAware:
                return MeshletPipelineMask::ForwardDepthAware;
            case MeshletPipelineKind::ForwardTransparent:
                return MeshletPipelineMask::ForwardTransparent;
            case MeshletPipelineKind::Shadow:
                return MeshletPipelineMask::Shadow;
            case MeshletPipelineKind::GeometryAux:
                return MeshletPipelineMask::GeometryAux;
            case MeshletPipelineKind::DepthPrepass:
                return MeshletPipelineMask::DepthPrepass;
            case MeshletPipelineKind::ForwardOpaque:
            default:
                return MeshletPipelineMask::ForwardOpaque;
            }
        }

        bool IsPipelineRequested(uint32_t pipelineMask, MeshletPipelineKind kind) {
            return (pipelineMask & PipelineMaskForKind(kind)) != 0u;
        }

        std::string NarrowDebugName(const wchar_t* name) {
            if (name == nullptr) {
                return {};
            }
            std::string result;
            while (*name != L'\0') {
                result.push_back(
                    *name >= 0 && *name < 128
                        ? static_cast<char>(*name)
                        : '?');
                ++name;
            }
            return result;
        }

        const char* CullModeToString(D3D12_CULL_MODE cullMode) {
            switch (cullMode) {
            case D3D12_CULL_MODE_NONE: return "NONE";
            case D3D12_CULL_MODE_FRONT: return "FRONT";
            case D3D12_CULL_MODE_BACK: return "BACK";
            default: return "UNKNOWN";
            }
        }

        struct MeshletPipelineStateStream {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_AS AS;
            CD3DX12_PIPELINE_STATE_STREAM_MS MS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
            CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
            CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
            CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK SampleMask;
        };

        bool ArePipelinesReady(const MeshletRenderBackend::PipelineBucketArray& pipelines) {
            for (const auto& pipeline : pipelines) {
                if (pipeline == nullptr) {
                    return false;
                }
            }
            return true;
        }

        bool AreForwardPipelinesReady(const ForwardPipelineVariantArray& pipelines) {
            for (const auto& variant : pipelines) {
                if (!ArePipelinesReady(variant)) {
                    return false;
                }
            }
            return true;
        }

        bool AreRequestedPipelinesReady(
            uint32_t pipelineMask,
            const ForwardPipelineVariantArray& forward,
            const MeshletRenderBackend::PipelineBucketArray& depthAware,
            const MeshletRenderBackend::PipelineBucketArray& transparent,
            const MeshletRenderBackend::PipelineBucketArray& shadow,
            const MeshletRenderBackend::PipelineBucketArray& geometryAux,
            const MeshletRenderBackend::PipelineBucketArray& depthPrepass) {

            bool ready = true;
            if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::ForwardOpaque)) {
                ready = ready && AreForwardPipelinesReady(forward);
            }
            if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::ForwardDepthAware)) {
                ready = ready && ArePipelinesReady(depthAware);
            }
            if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::ForwardTransparent)) {
                ready = ready && ArePipelinesReady(transparent);
            }
            if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::Shadow)) {
                ready = ready && ArePipelinesReady(shadow);
            }
            if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::GeometryAux)) {
                ready = ready && ArePipelinesReady(geometryAux);
            }
            if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::DepthPrepass)) {
                ready = ready && ArePipelinesReady(depthPrepass);
            }
            return ready;
        }

        D3D12_CULL_MODE CullModeForBucket(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? D3D12_CULL_MODE_NONE
                : D3D12_CULL_MODE_BACK;
        }

        bool HasSourceForBucket(
            const GPUDRIVEN::GpuVisibilityResult& visibility,
            GPUDRIVEN::GpuDrivenPassKind pass,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            return visibility.HasSourceForBucket(pass, bucket);
        }

        UINT64 DispatchArgumentOffsetForBucket(
            const GPUDRIVEN::GpuDrivenCommandPassLayout& layout,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            return layout.GetBucket(bucket).meshDispatchArgumentOffset;
        }

        UINT64 CounterOffsetForBucket(
            const GPUDRIVEN::GpuDrivenCommandPassLayout& layout,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            return layout.GetBucket(bucket).counterOffset;
        }

        const wchar_t* ForwardDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet Forward DoubleSided PSO"
                : L"Meshlet Forward BackFace PSO";
        }

        std::wstring ForwardDebugName(
            GPUDRIVEN::GpuDrivenCommandBucket bucket,
            size_t variantIndex) {

            std::wstring result = ForwardDebugName(bucket);
            result += L" ";
            result += variantIndex < kForwardOpaqueVariantNames.size()
                ? kForwardOpaqueVariantNames[variantIndex]
                : L"Unknown";
            return result;
        }

        const wchar_t* DepthAwareDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet DepthAware DoubleSided PSO"
                : L"Meshlet DepthAware BackFace PSO";
        }

        const wchar_t* TransparentDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet Transparent DoubleSided PSO"
                : L"Meshlet Transparent BackFace PSO";
        }

        const wchar_t* ShadowDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet Shadow DoubleSided PSO"
                : L"Meshlet Shadow BackFace PSO";
        }

        const wchar_t* GeometryDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet GeometryAux DoubleSided PSO"
                : L"Meshlet GeometryAux BackFace PSO";
        }

        const wchar_t* DepthPrepassDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet DepthPrepass DoubleSided PSO"
                : L"Meshlet DepthPrepass BackFace PSO";
        }

        const char* PipelineEventName(
            MeshletPipelineKind kind,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            switch (kind) {
            case MeshletPipelineKind::DepthPrepass:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.DepthPrepass.DoubleSided"
                    : "MeshletDraw.DepthPrepass.BackFace";
            case MeshletPipelineKind::GeometryAux:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.GeometryAux.DoubleSided"
                    : "MeshletDraw.GeometryAux.BackFace";
            case MeshletPipelineKind::ForwardDepthAware:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.DepthAware.DoubleSided"
                    : "MeshletDraw.DepthAware.BackFace";
            case MeshletPipelineKind::ForwardTransparent:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.Transparent.DoubleSided"
                    : "MeshletDraw.Transparent.BackFace";
            case MeshletPipelineKind::Shadow:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.Shadow.DoubleSided"
                    : "MeshletDraw.Shadow.BackFace";
            case MeshletPipelineKind::ForwardOpaque:
            default:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.ForwardOpaque.DoubleSided"
                    : "MeshletDraw.ForwardOpaque.BackFace";
            }
        }

        GFX::GPU_PROFILE::Pass ProfilePassForPipelineKind(MeshletPipelineKind kind) {
            switch (kind) {
            case MeshletPipelineKind::GeometryAux:
                return GFX::GPU_PROFILE::Pass::MeshletDrawGeometryAux;
            case MeshletPipelineKind::DepthPrepass:
                return GFX::GPU_PROFILE::Pass::MeshletDrawDepthPrepass;
            case MeshletPipelineKind::ForwardDepthAware:
                return GFX::GPU_PROFILE::Pass::MeshletDrawDepthAware;
            case MeshletPipelineKind::ForwardTransparent:
                return GFX::GPU_PROFILE::Pass::MeshletDrawTransparent;
            case MeshletPipelineKind::Shadow:
                return GFX::GPU_PROFILE::Pass::MeshletDrawShadow;
            case MeshletPipelineKind::ForwardOpaque:
            default:
                return GFX::GPU_PROFILE::Pass::MeshletDrawForward;
            }
        }

        bool QueryMeshShaderCapabilities(
            ID3D12Device* device,
            MeshletRenderBackendStats& stats) {

            if (device == nullptr) {
                return false;
            }

            stats.shaderModel65Supported =
                GFX::SupportsShaderModel(device, D3D_SHADER_MODEL_6_5);

            D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
            HRESULT hr = device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS7,
                &options7,
                sizeof(options7));
            if (SUCCEEDED(hr)) {
                stats.meshShaderTier = static_cast<uint32_t>(options7.MeshShaderTier);
                stats.meshShaderSupported =
                    options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
            }

            D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9{};
            hr = device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS9,
                &options9,
                sizeof(options9));
            if (SUCCEEDED(hr)) {
                stats.meshShaderPipelineStatsSupported =
                    options9.MeshShaderPipelineStatsSupported != FALSE;
            }

            return stats.shaderModel65Supported && stats.meshShaderSupported;
        }

        void LogMeshletPsoFailure(
            HRESULT hr,
            const wchar_t* debugName,
            ID3DBlob* amplificationShader,
            ID3DBlob* meshShader,
            ID3DBlob* pixelShader,
            DXGI_FORMAT renderTargetFormat,
            D3D12_CULL_MODE cullMode,
            bool alphaBlend,
            bool depthWrite,
            bool hasRenderTarget) {

            std::ostringstream oss;
            oss << "[MeshletBackend][ERROR] PSO create failed."
                << " name=\"" << NarrowDebugName(debugName) << "\""
                << " hr=" << GFX::FormatHRESULT(hr)
                << " hasRT=" << (hasRenderTarget ? "true" : "false")
                << " rtv=" << (hasRenderTarget ? GFX::FormatToString(renderTargetFormat) : "NONE")
                << " dsv=" << GFX::FormatToString(DXGI_FORMAT_D32_FLOAT)
                << " cull=" << CullModeToString(cullMode)
                << " alphaBlend=" << (alphaBlend ? "true" : "false")
                << " depthWrite=" << (depthWrite ? "true" : "false")
                << " asBytes=" << (amplificationShader ? amplificationShader->GetBufferSize() : 0u)
                << " msBytes=" << (meshShader ? meshShader->GetBufferSize() : 0u)
                << " psBytes=" << (pixelShader ? pixelShader->GetBufferSize() : 0u);
            DEBUGLOG::PushRenderError(oss.str());
        }

        bool CreateMeshletPipelineState(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            ID3DBlob* amplificationShader,
            ID3DBlob* meshShader,
            ID3DBlob* pixelShader,
            DXGI_FORMAT renderTargetFormat,
            D3D12_CULL_MODE cullMode,
            bool alphaBlend,
            bool depthWrite,
            bool hasRenderTarget,
            const wchar_t* debugName,
            ID3D12PipelineState** outPipelineState) {

            if (device == nullptr ||
                rootSignature == nullptr ||
                amplificationShader == nullptr ||
                meshShader == nullptr ||
                (pixelShader == nullptr && hasRenderTarget) ||
                outPipelineState == nullptr) {
                return false;
            }

            D3D12_RT_FORMAT_ARRAY renderTargets{};
            renderTargets.NumRenderTargets = hasRenderTarget ? 1u : 0u;
            if (hasRenderTarget) {
                renderTargets.RTFormats[0] = renderTargetFormat;
            }

            CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
            rasterizer.CullMode = cullMode;

            CD3DX12_DEPTH_STENCIL_DESC depthStencil(D3D12_DEFAULT);
            depthStencil.DepthEnable = TRUE;
            depthStencil.DepthWriteMask =
                depthWrite
                    ? D3D12_DEPTH_WRITE_MASK_ALL
                    : D3D12_DEPTH_WRITE_MASK_ZERO;
            depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            CD3DX12_BLEND_DESC blend(D3D12_DEFAULT);
            if (alphaBlend && hasRenderTarget) {
                D3D12_RENDER_TARGET_BLEND_DESC& rt0 = blend.RenderTarget[0];
                rt0.BlendEnable = TRUE;
                rt0.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                rt0.BlendOp = D3D12_BLEND_OP_ADD;
                rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }
            if (!hasRenderTarget) {
                blend.AlphaToCoverageEnable = FALSE;
                blend.IndependentBlendEnable = FALSE;
                for (D3D12_RENDER_TARGET_BLEND_DESC& rt : blend.RenderTarget) {
                    rt.BlendEnable = FALSE;
                    rt.LogicOpEnable = FALSE;
                    rt.RenderTargetWriteMask = 0;
                }
            }

            MeshletPipelineStateStream stream{};
            stream.RootSignature = rootSignature;
            stream.AS = CD3DX12_SHADER_BYTECODE(amplificationShader);
            stream.MS = CD3DX12_SHADER_BYTECODE(meshShader);
            const D3D12_SHADER_BYTECODE pixelShaderBytecode =
                pixelShader != nullptr
                    ? CD3DX12_SHADER_BYTECODE(pixelShader)
                    : D3D12_SHADER_BYTECODE{};
            stream.PS = pixelShaderBytecode;
            stream.BlendState = blend;
            stream.DepthStencilState = depthStencil;
            stream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            stream.RasterizerState = rasterizer;
            stream.RTVFormats = renderTargets;
            stream.SampleDesc = DXGI_SAMPLE_DESC{ 1, 0 };
            stream.SampleMask = (std::numeric_limits<UINT>::max)();

            D3D12_PIPELINE_STATE_STREAM_DESC desc{};
            desc.SizeInBytes = sizeof(stream);
            desc.pPipelineStateSubobjectStream = &stream;

            Microsoft::WRL::ComPtr<ID3D12Device2> device2;
            HRESULT hr = device->QueryInterface(IID_PPV_ARGS(device2.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "MeshletRenderBackend::QueryDevice2")) {
                return false;
            }

            GFX::ClearD3D12InfoQueue(device);
            hr = device2->CreatePipelineState(
                &desc,
                IID_PPV_ARGS(outPipelineState));
            if (!HIKARI_DX_CHECK(hr, "MeshletRenderBackend::CreatePipelineState")) {
                LogMeshletPsoFailure(
                    hr,
                    debugName,
                    amplificationShader,
                    meshShader,
                    pixelShader,
                    renderTargetFormat,
                    cullMode,
                    alphaBlend,
                    depthWrite,
                    hasRenderTarget);
                const std::string reason =
                    "MeshletRenderBackend::CreatePipelineState name=" +
                    NarrowDebugName(debugName);
                GFX::DumpD3D12InfoQueue(device, reason.c_str());
                return false;
            }
            GFX::SetD3D12Name(*outPipelineState, debugName);
            return true;
        }
    }

    bool MeshletRenderBackend::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature) {

        return Initialize(device, rootSignature, MeshletPipelineMask::All);
    }

    bool MeshletRenderBackend::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        uint32_t pipelineMask) {

        Reset();
        pipelineMask_ = pipelineMask & MeshletPipelineMask::All;
        stats_.initialized = device != nullptr && rootSignature != nullptr;
        if (!stats_.initialized) {
            return false;
        }
        if (pipelineMask_ == 0u) {
            return true;
        }

        if (!QueryMeshShaderCapabilities(device, stats_)) {
            DEBUGLOG::PushRenderError(
                "[MeshletBackend][WARN] Mesh Shader backend is not supported by this device/runtime.");
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> amplificationShader;
        Microsoft::WRL::ComPtr<ID3DBlob> forwardMeshShader;
        Microsoft::WRL::ComPtr<ID3DBlob> depthMeshShader;
        Microsoft::WRL::ComPtr<ID3DBlob> geometryMeshShader;
        std::array<
            Microsoft::WRL::ComPtr<ID3DBlob>,
            kForwardOpaquePipelineVariantCount> forwardPixelShaders{};
        Microsoft::WRL::ComPtr<ID3DBlob> depthAwarePixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob> transparentPixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob> shadowPixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob> geometryPixelShader;
        const bool needsForwardMeshShader =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) ||
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware) ||
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent);
        const bool needsDepthMeshShader =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow) ||
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::DepthPrepass);
        const bool needsGeometryMeshShader =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux);
        if (!GFX::CompileShaderFileSm6(
            L"HIKARI/Shaders/Render3D_MeshletAS.hlsl",
            "main",
            GFX::ShaderStage::Amplification,
            amplificationShader.GetAddressOf())) {
            return false;
        }
        if (needsForwardMeshShader &&
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_MeshletMS.hlsl",
                "main",
                GFX::ShaderStage::Mesh,
                forwardMeshShader.GetAddressOf())) {
            return false;
        }
        if (needsDepthMeshShader &&
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_MeshletDepthMS.hlsl",
                "main",
                GFX::ShaderStage::Mesh,
                depthMeshShader.GetAddressOf())) {
            return false;
        }
        if (needsGeometryMeshShader &&
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_MeshletGeometryAuxMS.hlsl",
                "main",
                GFX::ShaderStage::Mesh,
                geometryMeshShader.GetAddressOf())) {
            return false;
        }
        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque)) {
            for (size_t variantIndex = 0;
                variantIndex < kForwardOpaquePipelineVariantCount;
                ++variantIndex) {

                if (!GFX::CompileShaderFileSm6(
                    kForwardOpaqueShaderPaths[variantIndex],
                    "main",
                    GFX::ShaderStage::Pixel,
                    forwardPixelShaders[variantIndex].GetAddressOf())) {
                    return false;
                }
            }
        }
        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware) &&
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GpuDrivenOpaqueFullPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                depthAwarePixelShader.GetAddressOf())) {
            return false;
        }
        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent) &&
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GpuDrivenOpaqueFullPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                transparentPixelShader.GetAddressOf())) {
            return false;
        }
        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow) &&
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_MeshletDepthPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                shadowPixelShader.GetAddressOf())) {
            return false;
        }
        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux) &&
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_MeshletGeometryAuxPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                geometryPixelShader.GetAddressOf())) {
            return false;
        }
        stats_.shaderCompileReady = true;

        for (size_t bucketIndex = 0; bucketIndex < GPUDRIVEN::kGpuDrivenCommandBucketCount; ++bucketIndex) {
            const GPUDRIVEN::GpuDrivenCommandBucket bucket =
                static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(bucketIndex);

            if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque)) {
                for (size_t variantIndex = 0;
                    variantIndex < kForwardOpaquePipelineVariantCount;
                    ++variantIndex) {

                    const std::wstring debugName =
                        ForwardDebugName(bucket, variantIndex);
                    ++stats_.pipelineCreateRequestCount;
                    if (CreateMeshletPipelineState(
                        device,
                        rootSignature,
                        amplificationShader.Get(),
                        forwardMeshShader.Get(),
                        forwardPixelShaders[variantIndex].Get(),
                        DXGI_FORMAT_R16G16B16A16_FLOAT,
                        CullModeForBucket(bucket),
                        false,
                        true,
                        true,
                        debugName.c_str(),
                        forwardPipelineStates_[variantIndex][bucketIndex].GetAddressOf())) {
                        ++stats_.pipelineCreateReadyCount;
                    }
                }
            }

            if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware)) {
                ++stats_.pipelineCreateRequestCount;
                if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    forwardMeshShader.Get(),
                    depthAwarePixelShader.Get(),
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket),
                    true,
                    false,
                    true,
                    DepthAwareDebugName(bucket),
                    depthAwarePipelineStates_[bucketIndex].GetAddressOf())) {
                    ++stats_.pipelineCreateReadyCount;
                }
            }

            if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent)) {
                ++stats_.pipelineCreateRequestCount;
                if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    forwardMeshShader.Get(),
                    transparentPixelShader.Get(),
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket),
                    true,
                    false,
                    true,
                    TransparentDebugName(bucket),
                    transparentPipelineStates_[bucketIndex].GetAddressOf())) {
                    ++stats_.pipelineCreateReadyCount;
                }
            }

            if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow)) {
                ++stats_.pipelineCreateRequestCount;
                if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    depthMeshShader.Get(),
                    shadowPixelShader.Get(),
                    DXGI_FORMAT_UNKNOWN,
                    CullModeForBucket(bucket),
                    false,
                    true,
                    false,
                    ShadowDebugName(bucket),
                    shadowPipelineStates_[bucketIndex].GetAddressOf())) {
                    ++stats_.pipelineCreateReadyCount;
                }
            }

            if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux)) {
                ++stats_.pipelineCreateRequestCount;
                if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    geometryMeshShader.Get(),
                    geometryPixelShader.Get(),
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket),
                    false,
                    true,
                    true,
                    GeometryDebugName(bucket),
                    geometryAuxPipelineStates_[bucketIndex].GetAddressOf())) {
                    ++stats_.pipelineCreateReadyCount;
                }
            }

            if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::DepthPrepass)) {
                ++stats_.pipelineCreateRequestCount;
                if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    depthMeshShader.Get(),
                    nullptr,
                    DXGI_FORMAT_UNKNOWN,
                    CullModeForBucket(bucket),
                    false,
                    true,
                    false,
                    DepthPrepassDebugName(bucket),
                    depthPrepassPipelineStates_[bucketIndex].GetAddressOf())) {
                    ++stats_.pipelineCreateReadyCount;
                }
            }
        }

        ResetFrame();
        return stats_.pipelineReady;
    }

    void MeshletRenderBackend::Reset() {
        for (auto& variant : forwardPipelineStates_) {
            for (auto& pipeline : variant) {
                pipeline.Reset();
            }
        }
        for (auto& pipeline : depthAwarePipelineStates_) {
            pipeline.Reset();
        }
        for (auto& pipeline : transparentPipelineStates_) {
            pipeline.Reset();
        }
        for (auto& pipeline : shadowPipelineStates_) {
            pipeline.Reset();
        }
        for (auto& pipeline : geometryAuxPipelineStates_) {
            pipeline.Reset();
        }
        for (auto& pipeline : depthPrepassPipelineStates_) {
            pipeline.Reset();
        }
        pipelineMask_ = MeshletPipelineMask::All;
        stats_ = {};
    }

    void MeshletRenderBackend::ResetFrame() {
        const MeshletRenderBackendStats persistent = stats_;
        stats_ = {};
        stats_.initialized = persistent.initialized;
        stats_.shaderModel65Supported = persistent.shaderModel65Supported;
        stats_.meshShaderSupported = persistent.meshShaderSupported;
        stats_.meshShaderPipelineStatsSupported =
            persistent.meshShaderPipelineStatsSupported;
        stats_.shaderCompileReady = persistent.shaderCompileReady;
        stats_.meshShaderTier = persistent.meshShaderTier;
        stats_.pipelineCreateRequestCount =
            persistent.pipelineCreateRequestCount;
        stats_.pipelineCreateReadyCount =
            persistent.pipelineCreateReadyCount;

        const bool forwardReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) &&
            AreForwardPipelinesReady(forwardPipelineStates_);
        const bool depthAwareReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware) &&
            ArePipelinesReady(depthAwarePipelineStates_);
        const bool transparentReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent) &&
            ArePipelinesReady(transparentPipelineStates_);
        const bool shadowReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow) &&
            ArePipelinesReady(shadowPipelineStates_);
        const bool geometryReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux) &&
            ArePipelinesReady(geometryAuxPipelineStates_);
        const bool depthPrepassReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::DepthPrepass) &&
            ArePipelinesReady(depthPrepassPipelineStates_);
        stats_.forwardPipelineReady = forwardReady;
        stats_.depthAwarePipelineReady = depthAwareReady;
        stats_.transparentPipelineReady = transparentReady;
        stats_.shadowPipelineReady = shadowReady;
        stats_.geometryAuxPipelineReady = geometryReady;
        stats_.depthPrepassPipelineReady = depthPrepassReady;
        stats_.pipelineReady = AreRequestedPipelinesReady(
            pipelineMask_,
            forwardPipelineStates_,
            depthAwarePipelineStates_,
            transparentPipelineStates_,
            shadowPipelineStates_,
            geometryAuxPipelineStates_,
            depthPrepassPipelineStates_);
    }

    bool MeshletRenderBackend::Execute(const MeshletRenderExecutionContext& ctx) {
        if (ctx.visibility == nullptr || ctx.drawCommandRange == nullptr) {
            return false;
        }

        const GPUDRIVEN::GpuVisibilityResult& visibility = *ctx.visibility;
        const GPUDRIVEN::GpuDrivenDrawCommandRange& range =
            *ctx.drawCommandRange;
        if (!range.consumable ||
            range.backend != GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader) {
            return false;
        }
        const size_t requestedDispatchCount = range.commandCount;
        stats_.requestedDispatchCount += requestedDispatchCount;
        stats_.dispatchArgumentBufferReady =
            range.argumentBuffer != nullptr;
        stats_.dispatchCommandSignatureReady =
            range.commandSignature != nullptr;
        stats_.forwardPipelineReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) &&
            AreForwardPipelinesReady(forwardPipelineStates_);
        stats_.depthAwarePipelineReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware) &&
            ArePipelinesReady(depthAwarePipelineStates_);
        stats_.transparentPipelineReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent) &&
            ArePipelinesReady(transparentPipelineStates_);
        stats_.shadowPipelineReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow) &&
            ArePipelinesReady(shadowPipelineStates_);
        stats_.geometryAuxPipelineReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux) &&
            ArePipelinesReady(geometryAuxPipelineStates_);
        stats_.depthPrepassPipelineReady =
            IsPipelineRequested(pipelineMask_, MeshletPipelineKind::DepthPrepass) &&
            ArePipelinesReady(depthPrepassPipelineStates_);
        stats_.pipelineReady = AreRequestedPipelinesReady(
            pipelineMask_,
            forwardPipelineStates_,
            depthAwarePipelineStates_,
            transparentPipelineStates_,
            shadowPipelineStates_,
            geometryAuxPipelineStates_,
            depthPrepassPipelineStates_);
        const bool requestedPipelineReady =
            ctx.pipelineKind == MeshletPipelineKind::GeometryAux
                ? stats_.geometryAuxPipelineReady
                : ctx.pipelineKind == MeshletPipelineKind::DepthPrepass
                    ? stats_.depthPrepassPipelineReady
                    : ctx.pipelineKind == MeshletPipelineKind::ForwardDepthAware
                        ? stats_.depthAwarePipelineReady
                        : ctx.pipelineKind == MeshletPipelineKind::ForwardTransparent
                            ? stats_.transparentPipelineReady
                            : ctx.pipelineKind == MeshletPipelineKind::Shadow
                                ? stats_.shadowPipelineReady
                                : stats_.forwardPipelineReady;

        if (requestedDispatchCount == 0) {
            return false;
        }
        if (ctx.commandList == nullptr ||
            !range.HasGpuCommandLayout() ||
            !stats_.dispatchArgumentBufferReady ||
            !stats_.dispatchCommandSignatureReady ||
            !requestedPipelineReady ||
            visibility.visibleMeshletRangeBuffer == nullptr) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        ID3D12Resource* argumentBuffer = range.argumentBuffer;
        ID3D12Resource* countBuffer = visibility.counterBuffer;
        ID3D12CommandSignature* commandSignature =
            range.commandSignature;
        if (argumentBuffer == nullptr ||
            countBuffer == nullptr ||
            commandSignature == nullptr) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        const GPUDRIVEN::GpuDrivenCommandPassLayout& layout =
            *range.gpuCommandLayout;
        const size_t bucketCapacity = layout.commandBucketCapacity;
        if (bucketCapacity == 0) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }
        constexpr UINT maxCommandCount = 1u;

        bool submittedAnyBucket = false;
        const GFX::GPU_PROFILE::Pass profilePass =
            ctx.profilePass != GFX::GPU_PROFILE::Pass::Count
                ? ctx.profilePass
                : ProfilePassForPipelineKind(ctx.pipelineKind);
        GFX::GPU_PROFILE::ScopedGpuTimer gpuDraw(ctx.commandList, profilePass);
        GFX::GPU_PIPELINE_STATS::ScopedPipelineStats pipelineStats(ctx.commandList, profilePass);
        for (size_t bucketIndex = 0; bucketIndex < GPUDRIVEN::kGpuDrivenCommandBucketCount; ++bucketIndex) {
            const GPUDRIVEN::GpuDrivenCommandBucket bucket =
                static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(bucketIndex);
            if (!HasSourceForBucket(visibility, ctx.pass, bucket)) {
                ++stats_.skippedBucketCount;
                continue;
            }

            ID3D12PipelineState* selectedPipeline =
                GetPipelineState(ctx.pipelineKind, bucket);
            if (selectedPipeline == nullptr) {
                continue;
            }

            ++stats_.submitCallCount;
            if (bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided) {
                ++stats_.doubleSidedSubmitCallCount;
            } else {
                ++stats_.backFaceSubmitCallCount;
            }

            GFX::PIX::ScopedGpuEvent pix(
                ctx.commandList,
                GFX::PIX::kColorRender,
                PipelineEventName(ctx.pipelineKind, bucket));

            ctx.commandList->SetPipelineState(selectedPipeline);
            ctx.commandList->ExecuteIndirect(
                commandSignature,
                maxCommandCount,
                argumentBuffer,
                DispatchArgumentOffsetForBucket(layout, bucket),
                countBuffer,
                CounterOffsetForBucket(layout, bucket));
            submittedAnyBucket = true;
        }

        if (!submittedAnyBucket) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        stats_.submittedDispatchCount += requestedDispatchCount;
        if (ctx.pipelineKind == MeshletPipelineKind::GeometryAux) {
            stats_.geometryAuxSubmittedDispatchCount += requestedDispatchCount;
        } else if (ctx.pipelineKind == MeshletPipelineKind::DepthPrepass) {
            stats_.depthPrepassSubmittedDispatchCount += requestedDispatchCount;
        } else if (ctx.pipelineKind == MeshletPipelineKind::Shadow) {
            stats_.shadowSubmittedDispatchCount += requestedDispatchCount;
        } else {
            stats_.forwardSubmittedDispatchCount += requestedDispatchCount;
        }
        return true;
    }

    const MeshletRenderBackendStats& MeshletRenderBackend::GetStats() const {
        return stats_;
    }

    ID3D12PipelineState* MeshletRenderBackend::GetPipelineState(
        MeshletPipelineKind kind,
        GPUDRIVEN::GpuDrivenCommandBucket bucket) const {

        const size_t index = GPUDRIVEN::ToCommandBucketIndex(bucket);
        if (index >= GPUDRIVEN::kGpuDrivenCommandBucketCount) {
            return nullptr;
        }
        switch (kind) {
        case MeshletPipelineKind::DepthPrepass:
            return depthPrepassPipelineStates_[index].Get();
        case MeshletPipelineKind::GeometryAux:
            return geometryAuxPipelineStates_[index].Get();
        case MeshletPipelineKind::ForwardDepthAware:
            return depthAwarePipelineStates_[index].Get();
        case MeshletPipelineKind::ForwardTransparent:
            return transparentPipelineStates_[index].Get();
        case MeshletPipelineKind::Shadow:
            return shadowPipelineStates_[index].Get();
        case MeshletPipelineKind::ForwardOpaque:
        default:
            return forwardPipelineStates_[
                ForwardPipelineVariantIndex(GetRenderQualitySettings().forwardCostMode)][index].Get();
        }
    }

} // namespace HIKARI::RENDER3D::MESHLET
