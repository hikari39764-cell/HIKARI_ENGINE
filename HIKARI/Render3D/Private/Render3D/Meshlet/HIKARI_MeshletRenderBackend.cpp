#include "Core/HIKARI_MathConfig.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <utility>

#include <d3dx12.h>

#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_StartupTimer.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuPipelineStatsProfiler.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
namespace HIKARI::RENDER3D::MESHLET {

namespace {
using ForwardPipelineVariantArray = MeshletRenderBackend::ForwardPipelineVariantArray;

constexpr const char* kProgramForwardProgramId = "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueFullPS";

// 前向き不透明の PS（ForwardShadingCostMode の順）。[0] は標準 PBR の面だけを描く通常の PS（材質 program の面は
// kProgramForwardProgramId で描く）。それ以外は一般面すべてを描く診断用で、GeneralFull は材質 program の PS と同じ。
constexpr std::array<const char*, kForwardOpaquePipelineVariantCount> kForwardOpaqueProgramIds = {
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueStandardPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueAlbedoOnlyPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoNormalMapPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoShadowPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoSsaoPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoMaterialExtrasPS",
    kProgramForwardProgramId,
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoSunShadowPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoContactShadowPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoLightProbesPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoEnvReflectionPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoLocalLightsPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoCloudShadowPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueNoDecalsPS",
    "HIKARI.EngineShader.Render3D_GpuDrivenOpaqueSkeletonPS",
};

D3D12_SHADER_BYTECODE ShaderBytecode(const ASSETS::SHADER::ShaderAssetEntryLease& shader) noexcept {
    const std::span<const uint8_t> bytes = shader.GetDxil();
    return {bytes.data(), bytes.size()};
}

bool ResolveMeshletShader(const GFX::ShaderDeviceCapabilities& capabilities, const char* programId,
                          ASSETS::SHADER::ShaderStage stage, const char* profile,
                          ASSETS::SHADER::ShaderAssetEntryLease& outShader) {
    ASSETS::SHADER::RenderGeometryRootLayout layout{};
    if (!ASSETS::SHADER::TryResolveRenderGeometryProgramRootLayout(programId, layout))
        return false;
    const auto shaderContract = ASSETS::SHADER::MakeRenderGeometryShaderContract(
        layout, {capabilities.shaderModel, capabilities.rootSignatureVersion, capabilities.featureMask});
    ASSETS::SHADER::ShaderAssetResolveResult resolved =
        ASSETS::SHADER::ResolveBuiltinRenderGeometryShaderEntry(programId, {"Main", stage, profile}, shaderContract);
    if (!resolved) {
        HIKARI_LOG_ERROR(std::string("[MeshletBackend][Shader] ") + programId + ": " + resolved.diagnostic);
        return false;
    }
    outShader = std::move(resolved.lease);
    return true;
}

// 前向き不透明が実際に描いたモード。診断の PSO が用意できず Full に戻った時は Full を出す。
void PublishForwardCostMode(ForwardShadingCostMode mode) {
    static const METRIC::MetricHandle handle = METRIC::Register(
        {METRIC::kForwardCostMode, "Render3D.MeshletRenderBackend", "Render3D", "mode", METRIC::MetricCategory::Count,
         "Forward cost mode the opaque forward pass drew with (ForwardShadingCostMode index)."});
    METRIC::Publish(handle, static_cast<double>(mode), HIKARI::TIME::GetFrameContext().frameIndex);
}

size_t ForwardPipelineVariantIndex(ForwardShadingCostMode mode) {
    const size_t raw = static_cast<size_t>(mode);
    return raw < kForwardOpaquePipelineVariantCount ? raw : 0u;
}

uint32_t PipelineMaskForKind(MeshletPipelineKind kind) {
    switch (kind) {
    case MeshletPipelineKind::ForwardDepthAware: return MeshletPipelineMask::ForwardDepthAware;
    case MeshletPipelineKind::ForwardTransparent: return MeshletPipelineMask::ForwardTransparent;
    case MeshletPipelineKind::Shadow: return MeshletPipelineMask::Shadow;
    case MeshletPipelineKind::GeometryAux: return MeshletPipelineMask::GeometryAux;
    case MeshletPipelineKind::GeometryAuxFeatureMask: return MeshletPipelineMask::GeometryAuxFeatureMask;
    case MeshletPipelineKind::GeometryOutline: return MeshletPipelineMask::GeometryOutline;
    case MeshletPipelineKind::VisibilityOverlay: return MeshletPipelineMask::VisibilityOverlay;
    case MeshletPipelineKind::DepthPrepass: return MeshletPipelineMask::DepthPrepass;
    case MeshletPipelineKind::WaterVisibility: return MeshletPipelineMask::WaterVisibility;
    case MeshletPipelineKind::ForwardOpaque:
    default: return MeshletPipelineMask::ForwardOpaque;
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
        result.push_back(*name >= 0 && *name < 128 ? static_cast<char>(*name) : '?');
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

bool AreBaseForwardPipelinesReady(const ForwardPipelineVariantArray& pipelines) {
    // 診断 variant の未作成/失敗を通常描画の readiness に含めない。
    return ArePipelinesReady(pipelines[0]);
}

bool AreRequestedPipelinesReady(uint32_t pipelineMask, const ForwardPipelineVariantArray& forward,
                                const MeshletRenderBackend::PipelineBucketArray& depthAware,
                                const MeshletRenderBackend::PipelineBucketArray& transparent,
                                const MeshletRenderBackend::PipelineBucketArray& shadow,
                                const MeshletRenderBackend::PipelineBucketArray& geometryAux,
                                const MeshletRenderBackend::PipelineBucketArray& geometryFeatureMask,
                                const MeshletRenderBackend::PipelineBucketArray& geometryOutline,
                                const MeshletRenderBackend::PipelineBucketArray& visibilityOverlay,
                                const MeshletRenderBackend::PipelineBucketArray& depthPrepass,
                                const MeshletRenderBackend::PipelineBucketArray& waterVisibility) {
    bool ready = true;
    if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::ForwardOpaque)) {
        ready = ready && AreBaseForwardPipelinesReady(forward);
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
    if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::GeometryAuxFeatureMask)) {
        ready = ready && ArePipelinesReady(geometryFeatureMask);
    }
    if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::GeometryOutline)) {
        ready = ready && ArePipelinesReady(geometryOutline);
    }
    if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::VisibilityOverlay)) {
        ready = ready && ArePipelinesReady(visibilityOverlay);
    }
    if (IsPipelineRequested(pipelineMask, MeshletPipelineKind::DepthPrepass)) {
        ready = ready && ArePipelinesReady(depthPrepass);
    }
    // Water visibility is an optional auxiliary producer.  Its absence
    // must never make the main mesh-shader renderer unavailable; the
    // caller falls back to the authoritative opaque SceneDepth when
    // this bucket is not ready.
    (void)waterVisibility;
    return ready;
}

D3D12_CULL_MODE CullModeForBucket(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
}

bool HasSourceForBucket(const GPUDRIVEN::GpuVisibilityResult& visibility, GPUDRIVEN::GpuDrivenPassKind pass,
                        GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return visibility.HasSourceForBucket(pass, bucket);
}

UINT64 DispatchArgumentOffsetForBucket(const GPUDRIVEN::GpuDrivenCommandPassLayout& layout,
                                       GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return layout.GetBucket(bucket).meshDispatchArgumentOffset;
}

UINT64 CounterOffsetForBucket(const GPUDRIVEN::GpuDrivenCommandPassLayout& layout,
                              GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return layout.GetBucket(bucket).counterOffset;
}

const wchar_t* ForwardDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? L"Meshlet Forward DoubleSided PSO"
                                                                    : L"Meshlet Forward BackFace PSO";
}

std::wstring ForwardDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket, size_t variantIndex) {
    const std::string_view variant =
        TEXT::EnumToName(kForwardShadingCostModeNames, static_cast<ForwardShadingCostMode>(variantIndex), "Unknown");
    return std::wstring(ForwardDebugName(bucket)) + L" " + std::wstring(variant.begin(), variant.end());
}

const wchar_t* DepthAwareDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? L"Meshlet DepthAware DoubleSided PSO"
                                                                    : L"Meshlet DepthAware BackFace PSO";
}

const wchar_t* TransparentDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? L"Meshlet Transparent DoubleSided PSO"
                                                                    : L"Meshlet Transparent BackFace PSO";
}

const wchar_t* ShadowDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? L"Meshlet Shadow DoubleSided PSO"
                                                                    : L"Meshlet Shadow BackFace PSO";
}

const wchar_t* GeometryDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? L"Meshlet GeometryAux DoubleSided PSO"
                                                                    : L"Meshlet GeometryAux BackFace PSO";
}

const wchar_t* GeometryFeatureMaskDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? L"Meshlet GeometryFeatureMask DoubleSided PSO"
                                                                    : L"Meshlet GeometryFeatureMask BackFace PSO";
}

const wchar_t* GeometryOutlineDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? L"Meshlet GeometryOutline DoubleSided PSO"
                                                                    : L"Meshlet GeometryOutline BackFace PSO";
}

const wchar_t* VisibilityOverlayDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? L"Meshlet VisibilityOverlay DoubleSided PSO"
                                                                    : L"Meshlet VisibilityOverlay BackFace PSO";
}

const wchar_t* DepthPrepassDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? L"Meshlet DepthPrepass DoubleSided PSO"
                                                                    : L"Meshlet DepthPrepass BackFace PSO";
}

const char* PipelineEventName(MeshletPipelineKind kind, GPUDRIVEN::GpuDrivenCommandBucket bucket) {
    switch (kind) {
    case MeshletPipelineKind::DepthPrepass:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.DepthPrepass.DoubleSided"
                                                                        : "MeshletDraw.DepthPrepass.BackFace";
    case MeshletPipelineKind::WaterVisibility:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.WaterVisibility.DoubleSided"
                                                                        : "MeshletDraw.WaterVisibility.BackFace";
    case MeshletPipelineKind::GeometryAux:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.GeometryAux.DoubleSided"
                                                                        : "MeshletDraw.GeometryAux.BackFace";
    case MeshletPipelineKind::GeometryAuxFeatureMask:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.GeometryFeatureMask.DoubleSided"
                                                                        : "MeshletDraw.GeometryFeatureMask.BackFace";
    case MeshletPipelineKind::GeometryOutline:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.GeometryOutline.DoubleSided"
                                                                        : "MeshletDraw.GeometryOutline.BackFace";
    case MeshletPipelineKind::VisibilityOverlay:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.VisibilityOverlay.DoubleSided"
                                                                        : "MeshletDraw.VisibilityOverlay.BackFace";
    case MeshletPipelineKind::ForwardDepthAware:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.DepthAware.DoubleSided"
                                                                        : "MeshletDraw.DepthAware.BackFace";
    case MeshletPipelineKind::ForwardTransparent:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.Transparent.DoubleSided"
                                                                        : "MeshletDraw.Transparent.BackFace";
    case MeshletPipelineKind::Shadow:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.Shadow.DoubleSided"
                                                                        : "MeshletDraw.Shadow.BackFace";
    case MeshletPipelineKind::ForwardOpaque:
    default:
        return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided ? "MeshletDraw.ForwardOpaque.DoubleSided"
                                                                        : "MeshletDraw.ForwardOpaque.BackFace";
    }
}

GFX::GPU_PROFILE::Pass ProfilePassForPipelineKind(MeshletPipelineKind kind) {
    switch (kind) {
    case MeshletPipelineKind::GeometryAux:
    case MeshletPipelineKind::GeometryAuxFeatureMask: return GFX::GPU_PROFILE::Pass::MeshletDrawGeometryAux;
    case MeshletPipelineKind::GeometryOutline: return GFX::GPU_PROFILE::Pass::MeshletDrawGeometryOutline;
    case MeshletPipelineKind::VisibilityOverlay: return GFX::GPU_PROFILE::Pass::MeshletDrawVisibilityOverlay;
    case MeshletPipelineKind::DepthPrepass: return GFX::GPU_PROFILE::Pass::MeshletDrawDepthPrepass;
    case MeshletPipelineKind::WaterVisibility: return GFX::GPU_PROFILE::Pass::MeshletDrawDepthPrepass;
    case MeshletPipelineKind::ForwardDepthAware: return GFX::GPU_PROFILE::Pass::MeshletDrawDepthAware;
    case MeshletPipelineKind::ForwardTransparent: return GFX::GPU_PROFILE::Pass::MeshletDrawTransparent;
    case MeshletPipelineKind::Shadow: return GFX::GPU_PROFILE::Pass::MeshletDrawShadow;
    case MeshletPipelineKind::ForwardOpaque:
    default: return GFX::GPU_PROFILE::Pass::MeshletDrawForward;
    }
}

bool QueryMeshShaderCapabilities(ID3D12Device* device, MeshletRenderBackendStats& stats) {
    if (device == nullptr) {
        return false;
    }

    stats.shaderModel65Supported = GFX::QueryShaderDeviceCapabilities(device).shaderModel >= 65u;

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
    HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    if (SUCCEEDED(hr)) {
        stats.meshShaderTier = static_cast<uint32_t>(options7.MeshShaderTier);
        stats.meshShaderSupported = options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9{};
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));
    if (SUCCEEDED(hr)) {
        stats.meshShaderPipelineStatsSupported = options9.MeshShaderPipelineStatsSupported != FALSE;
    }

    return stats.shaderModel65Supported && stats.meshShaderSupported;
}

void LogMeshletPsoFailure(HRESULT hr, const wchar_t* debugName, D3D12_SHADER_BYTECODE amplificationShader,
                          D3D12_SHADER_BYTECODE meshShader, D3D12_SHADER_BYTECODE pixelShader,
                          DXGI_FORMAT renderTargetFormat, D3D12_CULL_MODE cullMode, bool alphaBlend, bool depthWrite,
                          bool hasRenderTarget) {
    std::ostringstream oss;
    oss << "[MeshletBackend][ERROR] PSO create failed."
        << " name=\"" << NarrowDebugName(debugName) << "\""
        << " hr=" << GFX::FormatHRESULT(hr) << " hasRT=" << (hasRenderTarget ? "true" : "false")
        << " rtv=" << (hasRenderTarget ? GFX::FormatToString(renderTargetFormat) : "NONE")
        << " dsv=" << GFX::FormatToString(DXGI_FORMAT_D32_FLOAT) << " cull=" << CullModeToString(cullMode)
        << " alphaBlend=" << (alphaBlend ? "true" : "false") << " depthWrite=" << (depthWrite ? "true" : "false")
        << " asBytes=" << amplificationShader.BytecodeLength << " msBytes=" << meshShader.BytecodeLength
        << " psBytes=" << pixelShader.BytecodeLength;
    DEBUGLOG::PushRenderError(oss.str());
}

bool CreateMeshletPipelineState(ID3D12Device* device, ID3D12RootSignature* rootSignature,
                                D3D12_SHADER_BYTECODE amplificationShader, D3D12_SHADER_BYTECODE meshShader,
                                D3D12_SHADER_BYTECODE pixelShader, DXGI_FORMAT renderTargetFormat,
                                D3D12_CULL_MODE cullMode, bool alphaBlend, bool depthWrite, bool hasRenderTarget,
                                const wchar_t* debugName, ID3D12PipelineState** outPipelineState,
                                DXGI_FORMAT secondaryRenderTargetFormat = DXGI_FORMAT_UNKNOWN,
                                D3D12_COMPARISON_FUNC depthFunction = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                                DXGI_FORMAT tertiaryRenderTargetFormat = DXGI_FORMAT_UNKNOWN,
                                std::optional<DXGI_FORMAT> fourthRenderTargetFormat = std::nullopt,
                                std::optional<DXGI_FORMAT> fifthRenderTargetFormat = std::nullopt) {
    if (device == nullptr || rootSignature == nullptr || amplificationShader.pShaderBytecode == nullptr ||
        meshShader.pShaderBytecode == nullptr || (pixelShader.pShaderBytecode == nullptr && hasRenderTarget) ||
        (tertiaryRenderTargetFormat != DXGI_FORMAT_UNKNOWN && secondaryRenderTargetFormat == DXGI_FORMAT_UNKNOWN) ||
        (fourthRenderTargetFormat.has_value() && tertiaryRenderTargetFormat == DXGI_FORMAT_UNKNOWN) ||
        (fifthRenderTargetFormat.has_value() && !fourthRenderTargetFormat.has_value()) || outPipelineState == nullptr) {
        return false;
    }

    D3D12_RT_FORMAT_ARRAY renderTargets{};
    renderTargets.NumRenderTargets = hasRenderTarget ? (fifthRenderTargetFormat.has_value()                  ? 5u
                                                        : fourthRenderTargetFormat.has_value()               ? 4u
                                                        : tertiaryRenderTargetFormat != DXGI_FORMAT_UNKNOWN  ? 3u
                                                        : secondaryRenderTargetFormat != DXGI_FORMAT_UNKNOWN ? 2u
                                                                                                             : 1u)
                                                     : 0u;
    if (hasRenderTarget) {
        renderTargets.RTFormats[0] = renderTargetFormat;
        renderTargets.RTFormats[1] = secondaryRenderTargetFormat;
        renderTargets.RTFormats[2] = tertiaryRenderTargetFormat;
        renderTargets.RTFormats[3] = fourthRenderTargetFormat.value_or(DXGI_FORMAT_UNKNOWN);
        renderTargets.RTFormats[4] = fifthRenderTargetFormat.value_or(DXGI_FORMAT_UNKNOWN);
    }

    CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
    rasterizer.FrontCounterClockwise = MATH::kFrontFaceCounterClockwise ? TRUE : FALSE;
    rasterizer.CullMode = cullMode;

    CD3DX12_DEPTH_STENCIL_DESC depthStencil(D3D12_DEFAULT);
    depthStencil.DepthEnable = TRUE;
    depthStencil.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = depthFunction;
    CD3DX12_BLEND_DESC blend(D3D12_DEFAULT);
    if (alphaBlend && hasRenderTarget) {
        D3D12_RENDER_TARGET_BLEND_DESC& rt0 = blend.RenderTarget[0];
        rt0.BlendEnable = TRUE;
        // Every forward transparent shader returns premultiplied RGB.
        rt0.SrcBlend = D3D12_BLEND_ONE;
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
    stream.AS = amplificationShader;
    stream.MS = meshShader;
    stream.PS = pixelShader;
    stream.BlendState = blend;
    stream.DepthStencilState = depthStencil;
    stream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    stream.RasterizerState = rasterizer;
    stream.RTVFormats = renderTargets;
    stream.SampleDesc = DXGI_SAMPLE_DESC{1, 0};
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
    const std::string pipelineStage = "PSO." + NarrowDebugName(debugName);
    CORE::StartupTimer pipelineTimer(pipelineStage);
    hr = device2->CreatePipelineState(&desc, IID_PPV_ARGS(outPipelineState));
    if (!HIKARI_DX_CHECK(hr, "MeshletRenderBackend::CreatePipelineState")) {
        LogMeshletPsoFailure(hr, debugName, amplificationShader, meshShader, pixelShader, renderTargetFormat, cullMode,
                             alphaBlend, depthWrite, hasRenderTarget);
        const std::string reason = "MeshletRenderBackend::CreatePipelineState name=" + NarrowDebugName(debugName);
        GFX::DumpD3D12InfoQueue(device, reason.c_str());
        return false;
    }
    GFX::SetD3D12Name(*outPipelineState, debugName);
    return true;
}
bool CreateForwardVariantPipelines(ID3D12Device* device, ID3D12RootSignature* root,
                                   const ASSETS::SHADER::ShaderAssetEntryLease& amplification,
                                   const ASSETS::SHADER::ShaderAssetEntryLease& mesh,
                                   const ASSETS::SHADER::ShaderAssetEntryLease& pixel, size_t variantIndex,
                                   MeshletRenderBackend::PipelineBucketArray& pipelines,
                                   MeshletRenderBackendStats& stats) {
    MeshletRenderBackend::PipelineBucketArray replacements;
    for (size_t i = 0; i < replacements.size(); ++i) {
        const auto bucket = static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(i);
        const auto name = ForwardDebugName(bucket, variantIndex);
        ++stats.pipelineCreateRequestCount;
        if (!CreateMeshletPipelineState(device, root, ShaderBytecode(amplification), ShaderBytecode(mesh),
                                        ShaderBytecode(pixel), DXGI_FORMAT_R16G16B16A16_FLOAT,
                                        CullModeForBucket(bucket), false, true, true, name.c_str(),
                                        replacements[i].GetAddressOf()))
            return false;
        ++stats.pipelineCreateReadyCount;
    }
    pipelines.swap(replacements);
    ++stats.preparedForwardVariantCount;
    return true;
}
} // namespace

bool MeshletRenderBackend::Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature) {
    return Initialize(device, rootSignature, MeshletPipelineMask::All);
}

bool MeshletRenderBackend::Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature, uint32_t pipelineMask) {
    Reset();
    device_ = device;
    rootSignature_ = rootSignature;
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

    const GFX::ShaderDeviceCapabilities capabilities = GFX::QueryShaderDeviceCapabilities(device);
    const auto resolveShader = [&capabilities](const char* programId, ASSETS::SHADER::ShaderStage stage,
                                               const char* profile, ASSETS::SHADER::ShaderAssetEntryLease& shader) {
        return ResolveMeshletShader(capabilities, programId, stage, profile, shader);
    };

    ASSETS::SHADER::ShaderAssetEntryLease amplificationShader{};
    ASSETS::SHADER::ShaderAssetEntryLease generalAmplificationShader{};
    ASSETS::SHADER::ShaderAssetEntryLease terrainAmplificationShader{};
    ASSETS::SHADER::ShaderAssetEntryLease standardAmplificationShader{};
    ASSETS::SHADER::ShaderAssetEntryLease programAmplificationShader{};
    ASSETS::SHADER::ShaderAssetEntryLease shadowAmplificationShader{};
    ASSETS::SHADER::ShaderAssetEntryLease forwardMeshShader{};
    ASSETS::SHADER::ShaderAssetEntryLease shadowMeshShader{};
    ASSETS::SHADER::ShaderAssetEntryLease depthMeshShader{};
    ASSETS::SHADER::ShaderAssetEntryLease waterVisibilityMeshShader{};
    ASSETS::SHADER::ShaderAssetEntryLease geometryMeshShader{};
    ASSETS::SHADER::ShaderAssetEntryLease geometryOutlineMeshShader{};
    ASSETS::SHADER::ShaderAssetEntryLease forwardPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease programForwardPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease terrainForwardPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease surfacePixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease reflectionPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease programReflectionPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease terrainReflectionPixelShader{};
    // 深度を読む Forward を光照贴图の有無で分ける AS と、光照贴图の面の PS（揃わなければ分けない）。
    struct LightingSplitShaders {
        ASSETS::SHADER::ShaderAssetEntryLease standardProbeAs{}, standardLightmapAs{}, programProbeAs{},
            programLightmapAs{};
        ASSETS::SHADER::ShaderAssetEntryLease standardLightmapPs{}, programLightmapPs{}, standardLightmapReflectionPs{},
            programLightmapReflectionPs{};
    } lightingShaders{};
    ASSETS::SHADER::ShaderAssetEntryLease shadowPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease waterVisibilityPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease geometryPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease terrainGeometryPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease geometryFeatureMaskPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease terrainGeometryFeatureMaskPixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease geometryOutlinePixelShader{};
    ASSETS::SHADER::ShaderAssetEntryLease visibilityOverlayPixelShader{};
    const bool needsForwardMeshShader = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) ||
                                        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware) ||
                                        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent) ||
                                        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::VisibilityOverlay);
    const bool needsShadowMeshShader = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow);
    const bool needsDepthMeshShader = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::DepthPrepass);
    const bool needsWaterVisibilityMeshShader =
        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::WaterVisibility);
    const bool needsGeometryMeshShader =
        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux) ||
        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAuxFeatureMask);
    const bool needsGeometryOutlineMeshShader =
        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryOutline);
    bool waterVisibilityShadersReady = true;
    bool geometryFeatureMaskShadersReady = true;
    bool geometryOutlineShadersReady = true;
    bool visibilityOverlayShaderReady = true;
    if ((pipelineMask_ & MeshletPipelineMask::MainRenderer) != 0u &&
        !resolveShader("HIKARI.EngineShader.Render3D_MeshletAS", ASSETS::SHADER::ShaderStage::Amplification, "as_6_5",
                       amplificationShader)) {
        return false;
    }
    // Forward と GeometryAux は同じ可視範囲を一般面用と地形用の AS で 2 回 dispatch する。
    const bool needsSurfacePartition = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) ||
                                       IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux) ||
                                       IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAuxFeatureMask);
    if (needsSurfacePartition &&
        (!resolveShader("HIKARI.EngineShader.Render3D_MeshletGeneralAS", ASSETS::SHADER::ShaderStage::Amplification,
                        "as_6_5", generalAmplificationShader) ||
         !resolveShader("HIKARI.EngineShader.Render3D_MeshletTerrainAS", ASSETS::SHADER::ShaderStage::Amplification,
                        "as_6_5", terrainAmplificationShader))) {
        return false;
    }
    if (needsShadowMeshShader &&
        !resolveShader("HIKARI.EngineShader.Render3D_MeshletShadowAS", ASSETS::SHADER::ShaderStage::Amplification,
                       "as_6_5", shadowAmplificationShader)) {
        return false;
    }
    if (needsForwardMeshShader && !resolveShader("HIKARI.EngineShader.Render3D_MeshletMS",
                                                 ASSETS::SHADER::ShaderStage::Mesh, "ms_6_5", forwardMeshShader)) {
        return false;
    }
    if (needsShadowMeshShader && !resolveShader("HIKARI.EngineShader.Render3D_MeshletShadowMS",
                                                ASSETS::SHADER::ShaderStage::Mesh, "ms_6_5", shadowMeshShader)) {
        return false;
    }
    if (needsDepthMeshShader && !resolveShader("HIKARI.EngineShader.Render3D_MeshletDepthMS",
                                               ASSETS::SHADER::ShaderStage::Mesh, "ms_6_5", depthMeshShader)) {
        return false;
    }
    if (needsWaterVisibilityMeshShader &&
        !resolveShader("HIKARI.EngineShader.Render3D_MeshletWaterVisibilityMS", ASSETS::SHADER::ShaderStage::Mesh,
                       "ms_6_5", waterVisibilityMeshShader)) {
        waterVisibilityShadersReady = false;
        DEBUGLOG::PushRenderError("[MeshletBackend][WARN] Optional cooked water visibility "
                                  "mesh shader is unavailable; opaque SceneDepth fallback "
                                  "remains active.");
    }
    if (needsGeometryMeshShader && !resolveShader("HIKARI.EngineShader.Render3D_MeshletGeometryAuxMS",
                                                  ASSETS::SHADER::ShaderStage::Mesh, "ms_6_5", geometryMeshShader)) {
        return false;
    }
    if (needsGeometryOutlineMeshShader &&
        !resolveShader("HIKARI.EngineShader.Render3D_MeshletGeometryOutlineMS", ASSETS::SHADER::ShaderStage::Mesh,
                       "ms_6_5", geometryOutlineMeshShader)) {
        geometryOutlineShadersReady = false;
        pipelineMask_ &= ~MeshletPipelineMask::GeometryOutline;
        DEBUGLOG::PushRenderError("[MeshletBackend][WARN] Optional geometry-outline mesh "
                                  "shader is unavailable; core Mesh rendering remains active.");
    }
    // Forward は同じ可視範囲を標準 PBR の面と材質 program の面の AS で 2 回 dispatch する。
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) &&
        (!resolveShader("HIKARI.EngineShader.Render3D_MeshletStandardAS", ASSETS::SHADER::ShaderStage::Amplification,
                        "as_6_5", standardAmplificationShader) ||
         !resolveShader("HIKARI.EngineShader.Render3D_MeshletProgramAS", ASSETS::SHADER::ShaderStage::Amplification,
                        "as_6_5", programAmplificationShader) ||
         !resolveShader(kForwardOpaqueProgramIds[0], ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                        forwardPixelShader) ||
         !resolveShader(kProgramForwardProgramId, ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                        programForwardPixelShader) ||
         !resolveShader("HIKARI.EngineShader.Render3D_GpuDrivenTerrainPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                        terrainForwardPixelShader))) {
        return false;
    }
    // 水面を含む全経路は depth-aware/transparent だけが使う。
    if ((IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware) ||
         IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent)) &&
        !resolveShader("HIKARI.EngineShader.Render3D_GpuDrivenSurfacePS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                       surfacePixelShader)) {
        return false;
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow) &&
        !resolveShader("HIKARI.EngineShader.Render3D_MeshletShadowPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                       shadowPixelShader)) {
        return false;
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::WaterVisibility) &&
        !resolveShader("HIKARI.EngineShader.Render3D_MeshletWaterVisibilityPS", ASSETS::SHADER::ShaderStage::Pixel,
                       "ps_6_0", waterVisibilityPixelShader)) {
        waterVisibilityShadersReady = false;
        DEBUGLOG::PushRenderError("[MeshletBackend][WARN] Optional cooked water visibility "
                                  "pixel shader is unavailable; opaque SceneDepth fallback "
                                  "remains active.");
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux) &&
        (!resolveShader("HIKARI.EngineShader.Render3D_MeshletGeometryAuxPS", ASSETS::SHADER::ShaderStage::Pixel,
                        "ps_6_0", geometryPixelShader) ||
         !resolveShader("HIKARI.EngineShader.Render3D_MeshletGeometryAuxTerrainPS", ASSETS::SHADER::ShaderStage::Pixel,
                        "ps_6_0", terrainGeometryPixelShader))) {
        return false;
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAuxFeatureMask) &&
        (!resolveShader("HIKARI.EngineShader.Render3D_MeshletGeometryAuxFeaturePS", ASSETS::SHADER::ShaderStage::Pixel,
                        "ps_6_0", geometryFeatureMaskPixelShader) ||
         !resolveShader("HIKARI.EngineShader.Render3D_MeshletGeometryAuxFeatureTerrainPS",
                        ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0", terrainGeometryFeatureMaskPixelShader))) {
        geometryFeatureMaskShadersReady = false;
        pipelineMask_ &= ~MeshletPipelineMask::GeometryAuxFeatureMask;
        DEBUGLOG::PushRenderError("[MeshletBackend][WARN] Optional feature-mask pixel shader "
                                  "is unavailable; core Mesh rendering remains active.");
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryOutline) && geometryOutlineShadersReady &&
        !resolveShader("HIKARI.EngineShader.Render3D_GeometryOutlinePS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                       geometryOutlinePixelShader)) {
        geometryOutlineShadersReady = false;
        pipelineMask_ &= ~MeshletPipelineMask::GeometryOutline;
        DEBUGLOG::PushRenderError("[MeshletBackend][WARN] Optional geometry-outline pixel "
                                  "shader is unavailable; core Mesh rendering remains active.");
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::VisibilityOverlay) &&
        !resolveShader("HIKARI.EngineShader.Render3D_VisibilityOverlayPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                       visibilityOverlayPixelShader)) {
        visibilityOverlayShaderReady = false;
        pipelineMask_ &= ~MeshletPipelineMask::VisibilityOverlay;
        DEBUGLOG::PushRenderError("[MeshletBackend][WARN] Optional visibility-overlay shader "
                                  "is unavailable; core Mesh rendering remains active.");
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) &&
        (!resolveShader("HIKARI.EngineShader.Render3D_GpuDrivenOpaqueStandardReflectionPS",
                        ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0", reflectionPixelShader) ||
         !resolveShader("HIKARI.EngineShader.Render3D_GpuDrivenOpaqueReflectionPS", ASSETS::SHADER::ShaderStage::Pixel,
                        "ps_6_0", programReflectionPixelShader) ||
         !resolveShader("HIKARI.EngineShader.Render3D_GpuDrivenTerrainReflectionPS", ASSETS::SHADER::ShaderStage::Pixel,
                        "ps_6_0", terrainReflectionPixelShader))) {
        reflectionPixelShader = {};
        DEBUGLOG::PushRenderError("[MeshletBackend][SSR] Capture shaders unavailable; environment fallback retained.");
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque)) {
        using ASSETS::SHADER::ShaderStage;
        const struct {
            const char* id;
            ShaderStage stage;
            const char* profile;
            ASSETS::SHADER::ShaderAssetEntryLease* out;
        } lightingPrograms[]{
            {"HIKARI.EngineShader.Render3D_MeshletStandardProbeAS", ShaderStage::Amplification, "as_6_5",
             &lightingShaders.standardProbeAs},
            {"HIKARI.EngineShader.Render3D_MeshletStandardLightmapAS", ShaderStage::Amplification, "as_6_5",
             &lightingShaders.standardLightmapAs},
            {"HIKARI.EngineShader.Render3D_MeshletProgramProbeAS", ShaderStage::Amplification, "as_6_5",
             &lightingShaders.programProbeAs},
            {"HIKARI.EngineShader.Render3D_MeshletProgramLightmapAS", ShaderStage::Amplification, "as_6_5",
             &lightingShaders.programLightmapAs},
            {"HIKARI.EngineShader.Render3D_GpuDrivenOpaqueStandardLightmapPS", ShaderStage::Pixel, "ps_6_0",
             &lightingShaders.standardLightmapPs},
            {"HIKARI.EngineShader.Render3D_GpuDrivenOpaqueLightmapPS", ShaderStage::Pixel, "ps_6_0",
             &lightingShaders.programLightmapPs},
            {"HIKARI.EngineShader.Render3D_GpuDrivenOpaqueStandardReflectionLightmapPS", ShaderStage::Pixel, "ps_6_0",
             &lightingShaders.standardLightmapReflectionPs},
            {"HIKARI.EngineShader.Render3D_GpuDrivenOpaqueReflectionLightmapPS", ShaderStage::Pixel, "ps_6_0",
             &lightingShaders.programLightmapReflectionPs},
        };
        for (const auto& program : lightingPrograms) {
            if (!resolveShader(program.id, program.stage, program.profile, *program.out)) {
                lightingShaders = {};
                DEBUGLOG::PushRenderError("[MeshletBackend][WARN] Lightmapped-surface Forward shaders unavailable; "
                                          "Forward keeps one PSO per material class.");
                break;
            }
        }
    }
    stats_.shaderCompileReady = true;

    // 全経路の表面（depth-aware/transparent）の pixel shader は大きく、shader 更新後の初回起動では driver の compile に
    // PSO 一つあたり数秒かかる。SSR capture の PSO も含めて別スレッドで作り、このスレッドの一般面 Forward と
    // 重ねる（D3D12 の PSO 生成は free-threaded）。capture は描画に必須ではないので Initialize では待たない。
    if (reflectionPixelShader) {
        reflectionCaptureTask_ = std::async(
            std::launch::async,
            [device = device_, rootSignature = rootSignature_, standardAs = standardAmplificationShader,
             programAs = programAmplificationShader, terrainAs = terrainAmplificationShader, ms = forwardMeshShader,
             standardPs = reflectionPixelShader, programPs = programReflectionPixelShader,
             terrainPs = terrainReflectionPixelShader, lighting = lightingShaders]() {
                ReflectionCapturePipelines pipelines{};
                const auto create = [&](const ASSETS::SHADER::ShaderAssetEntryLease& as,
                                        const ASSETS::SHADER::ShaderAssetEntryLease& ps,
                                        std::array<PipelineBucketArray, kCaptureVariantCount>& out) {
                    for (size_t variant = 0; variant < out.size(); ++variant) {
                        const bool captureSubsurface = (variant & 1u) != 0u;
                        const bool readDepth = variant >= 2u;
                        for (size_t i = 0; i < GPUDRIVEN::kGpuDrivenCommandBucketCount; ++i) {
                            if (!CreateMeshletPipelineState(
                                    device.Get(), rootSignature.Get(), ShaderBytecode(as), ShaderBytecode(ms),
                                    ShaderBytecode(ps), DXGI_FORMAT_R16G16B16A16_FLOAT,
                                    CullModeForBucket(static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(i)), false,
                                    !readDepth, true, L"Meshlet.ForwardReflectionCapture",
                                    out[variant][i].GetAddressOf(), DXGI_FORMAT_R16G16B16A16_FLOAT,
                                    D3D12_COMPARISON_FUNC_LESS_EQUAL, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                    captureSubsurface ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_UNKNOWN)) {
                                out[variant] = {};
                                break;
                            }
                        }
                    }
                };
                create(standardAs, standardPs, pipelines.standard);
                create(programAs, programPs, pipelines.program);
                create(terrainAs, terrainPs, pipelines.terrain);
                // 深度を読む capture の光照贴图での分割（探針の面は分けない PS を使う）。
                const auto createRead = [&](const ASSETS::SHADER::ShaderAssetEntryLease& as,
                                            const ASSETS::SHADER::ShaderAssetEntryLease& ps, bool captureSubsurface,
                                            PipelineBucketArray& out) {
                    for (size_t i = 0; i < out.size(); ++i) {
                        if (!CreateMeshletPipelineState(
                                device.Get(), rootSignature.Get(), ShaderBytecode(as), ShaderBytecode(ms),
                                ShaderBytecode(ps), DXGI_FORMAT_R16G16B16A16_FLOAT,
                                CullModeForBucket(static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(i)), false, false,
                                true, L"Meshlet.ForwardReflectionCapture", out[i].GetAddressOf(),
                                DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_COMPARISON_FUNC_LESS_EQUAL,
                                DXGI_FORMAT_R16G16B16A16_FLOAT,
                                captureSubsurface ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_UNKNOWN)) {
                            out = {};
                            return;
                        }
                    }
                };
                if (lighting.standardLightmapReflectionPs) {
                    for (size_t subsurface = 0; subsurface < pipelines.lightingRead.size(); ++subsurface) {
                        LightingSplitPipelines& split = pipelines.lightingRead[subsurface];
                        createRead(lighting.standardProbeAs, standardPs, subsurface != 0u, split.standardProbe);
                        createRead(lighting.standardLightmapAs, lighting.standardLightmapReflectionPs, subsurface != 0u,
                                   split.standardLightmap);
                        createRead(lighting.programProbeAs, programPs, subsurface != 0u, split.programProbe);
                        createRead(lighting.programLightmapAs, lighting.programLightmapReflectionPs, subsurface != 0u,
                                   split.programLightmap);
                    }
                }
                return pipelines;
            });
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) && forwardPixelShader &&
        programForwardPixelShader && terrainForwardPixelShader) {
        readDepthTask_ = std::async(
            std::launch::async,
            [device = device_, rootSignature = rootSignature_, standardAs = standardAmplificationShader,
             programAs = programAmplificationShader, terrainAs = terrainAmplificationShader, ms = forwardMeshShader,
             standardPs = forwardPixelShader, programPs = programForwardPixelShader,
             terrainPs = terrainForwardPixelShader, lighting = lightingShaders]() {
                ReadDepthForwardPipelines pipelines{};
                const auto create = [&](const ASSETS::SHADER::ShaderAssetEntryLease& as,
                                        const ASSETS::SHADER::ShaderAssetEntryLease& ps, const wchar_t* name,
                                        PipelineBucketArray& out) {
                    for (size_t i = 0; i < out.size(); ++i) {
                        if (!CreateMeshletPipelineState(
                                device.Get(), rootSignature.Get(), ShaderBytecode(as), ShaderBytecode(ms),
                                ShaderBytecode(ps), DXGI_FORMAT_R16G16B16A16_FLOAT,
                                CullModeForBucket(static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(i)), false, false,
                                true, name, out[i].GetAddressOf())) {
                            out = {};
                            return;
                        }
                    }
                };
                create(standardAs, standardPs, L"Meshlet Forward ReadDepth PSO", pipelines.standard);
                create(programAs, programPs, L"Meshlet Forward MaterialProgram ReadDepth PSO", pipelines.program);
                create(terrainAs, terrainPs, L"Meshlet Forward Terrain ReadDepth PSO", pipelines.terrain);
                if (lighting.standardLightmapPs) {
                    LightingSplitPipelines& split = pipelines.lighting;
                    create(lighting.standardProbeAs, standardPs, L"Meshlet Forward Probe ReadDepth PSO",
                           split.standardProbe);
                    create(lighting.standardLightmapAs, lighting.standardLightmapPs,
                           L"Meshlet Forward Lightmap ReadDepth PSO", split.standardLightmap);
                    create(lighting.programProbeAs, programPs, L"Meshlet Forward MaterialProgram Probe ReadDepth PSO",
                           split.programProbe);
                    create(lighting.programLightmapAs, lighting.programLightmapPs,
                           L"Meshlet Forward MaterialProgram Lightmap ReadDepth PSO", split.programLightmap);
                }
                return pipelines;
            });
    }
    const bool depthAwareRequested = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware);
    const bool transparentRequested = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent);
    std::future<uint32_t> surfacePipelines{};
    if (depthAwareRequested || transparentRequested) {
        stats_.pipelineCreateRequestCount += static_cast<uint32_t>(GPUDRIVEN::kGpuDrivenCommandBucketCount) *
                                             ((depthAwareRequested ? 1u : 0u) + (transparentRequested ? 1u : 0u));
        surfacePipelines = std::async(std::launch::async, [&]() {
            uint32_t ready = 0u;
            for (size_t bucketIndex = 0; bucketIndex < GPUDRIVEN::kGpuDrivenCommandBucketCount; ++bucketIndex) {
                const auto bucket = static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(bucketIndex);
                if (depthAwareRequested &&
                    CreateMeshletPipelineState(device, rootSignature, ShaderBytecode(amplificationShader),
                                               ShaderBytecode(forwardMeshShader), ShaderBytecode(surfacePixelShader),
                                               DXGI_FORMAT_R16G16B16A16_FLOAT, CullModeForBucket(bucket), true, false,
                                               true, DepthAwareDebugName(bucket),
                                               depthAwarePipelineStates_[bucketIndex].GetAddressOf())) {
                    ++ready;
                }
                if (transparentRequested &&
                    CreateMeshletPipelineState(device, rootSignature, ShaderBytecode(amplificationShader),
                                               ShaderBytecode(forwardMeshShader), ShaderBytecode(surfacePixelShader),
                                               DXGI_FORMAT_R16G16B16A16_FLOAT, CullModeForBucket(bucket), true, false,
                                               true, TransparentDebugName(bucket),
                                               transparentPipelineStates_[bucketIndex].GetAddressOf())) {
                    ++ready;
                }
            }
            return ready;
        });
    }
    if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque)) {
        forwardVariantAttempted_[0] = true;
        if (!CreateForwardVariantPipelines(device, rootSignature, standardAmplificationShader, forwardMeshShader,
                                           forwardPixelShader, 0, forwardPipelineStates_[0], stats_))
            return false;
    }

    for (size_t bucketIndex = 0; bucketIndex < GPUDRIVEN::kGpuDrivenCommandBucketCount; ++bucketIndex) {
        const GPUDRIVEN::GpuDrivenCommandBucket bucket = static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(bucketIndex);

        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow)) {
            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(
                    device, rootSignature, ShaderBytecode(shadowAmplificationShader), ShaderBytecode(shadowMeshShader),
                    ShaderBytecode(shadowPixelShader), DXGI_FORMAT_UNKNOWN, CullModeForBucket(bucket), false, true,
                    false, ShadowDebugName(bucket), shadowPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }
        }

        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque)) {
            stats_.pipelineCreateRequestCount += 2u;
            if (CreateMeshletPipelineState(device, rootSignature, ShaderBytecode(programAmplificationShader),
                                           ShaderBytecode(forwardMeshShader), ShaderBytecode(programForwardPixelShader),
                                           DXGI_FORMAT_R16G16B16A16_FLOAT, CullModeForBucket(bucket), false, true, true,
                                           L"Meshlet Forward MaterialProgram PSO",
                                           programForwardPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }
            if (CreateMeshletPipelineState(device, rootSignature, ShaderBytecode(terrainAmplificationShader),
                                           ShaderBytecode(forwardMeshShader), ShaderBytecode(terrainForwardPixelShader),
                                           DXGI_FORMAT_R16G16B16A16_FLOAT, CullModeForBucket(bucket), false, true, true,
                                           L"Meshlet Forward Terrain PSO",
                                           terrainForwardPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }
        }

        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux)) {
            stats_.pipelineCreateRequestCount += 2u;
            if (CreateMeshletPipelineState(device, rootSignature, ShaderBytecode(generalAmplificationShader),
                                           ShaderBytecode(geometryMeshShader), ShaderBytecode(geometryPixelShader),
                                           DXGI_FORMAT_R16G16B16A16_FLOAT, CullModeForBucket(bucket), false, true, true,
                                           GeometryDebugName(bucket),
                                           geometryAuxPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }
            if (CreateMeshletPipelineState(device, rootSignature, ShaderBytecode(terrainAmplificationShader),
                                           ShaderBytecode(geometryMeshShader),
                                           ShaderBytecode(terrainGeometryPixelShader), DXGI_FORMAT_R16G16B16A16_FLOAT,
                                           CullModeForBucket(bucket), false, true, true,
                                           L"Meshlet GeometryAux Terrain PSO",
                                           terrainGeometryAuxPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }
        }

        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAuxFeatureMask) &&
            geometryFeatureMaskShadersReady) {
            stats_.pipelineCreateRequestCount += 2u;
            if (CreateMeshletPipelineState(
                    device, rootSignature, ShaderBytecode(generalAmplificationShader),
                    ShaderBytecode(geometryMeshShader), ShaderBytecode(geometryFeatureMaskPixelShader),
                    DXGI_FORMAT_R16G16B16A16_FLOAT, CullModeForBucket(bucket), false, true, true,
                    GeometryFeatureMaskDebugName(bucket),
                    geometryFeatureMaskPipelineStates_[bucketIndex].GetAddressOf(), DXGI_FORMAT_R32G32_UINT,
                    D3D12_COMPARISON_FUNC_LESS_EQUAL, DXGI_FORMAT_R8G8B8A8_UNORM)) {
                ++stats_.pipelineCreateReadyCount;
            }
            if (CreateMeshletPipelineState(
                    device, rootSignature, ShaderBytecode(terrainAmplificationShader),
                    ShaderBytecode(geometryMeshShader), ShaderBytecode(terrainGeometryFeatureMaskPixelShader),
                    DXGI_FORMAT_R16G16B16A16_FLOAT, CullModeForBucket(bucket), false, true, true,
                    L"Meshlet GeometryAux FeatureMask Terrain PSO",
                    terrainGeometryFeatureMaskPipelineStates_[bucketIndex].GetAddressOf(), DXGI_FORMAT_R32G32_UINT,
                    D3D12_COMPARISON_FUNC_LESS_EQUAL, DXGI_FORMAT_R8G8B8A8_UNORM)) {
                ++stats_.pipelineCreateReadyCount;
            }
        }

        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryOutline) && geometryOutlineShadersReady) {
            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(device, rootSignature, ShaderBytecode(amplificationShader),
                                           ShaderBytecode(geometryOutlineMeshShader),
                                           ShaderBytecode(geometryOutlinePixelShader), DXGI_FORMAT_R16G16B16A16_FLOAT,
                                           D3D12_CULL_MODE_FRONT, false, false, true, GeometryOutlineDebugName(bucket),
                                           geometryOutlinePipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }
        }

        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::VisibilityOverlay) &&
            visibilityOverlayShaderReady) {
            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(
                    device, rootSignature, ShaderBytecode(amplificationShader), ShaderBytecode(forwardMeshShader),
                    ShaderBytecode(visibilityOverlayPixelShader), DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket), true, false, true, VisibilityOverlayDebugName(bucket),
                    visibilityOverlayPipelineStates_[bucketIndex].GetAddressOf(), DXGI_FORMAT_UNKNOWN,
                    D3D12_COMPARISON_FUNC_GREATER)) {
                ++stats_.pipelineCreateReadyCount;
            }
        }

        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::DepthPrepass)) {
            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(device, rootSignature, ShaderBytecode(amplificationShader),
                                           ShaderBytecode(depthMeshShader), {}, DXGI_FORMAT_UNKNOWN,
                                           CullModeForBucket(bucket), false, true, false, DepthPrepassDebugName(bucket),
                                           depthPrepassPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }
        }

        if (IsPipelineRequested(pipelineMask_, MeshletPipelineKind::WaterVisibility) && waterVisibilityShadersReady) {
            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(
                    device, rootSignature, ShaderBytecode(amplificationShader),
                    ShaderBytecode(waterVisibilityMeshShader), ShaderBytecode(waterVisibilityPixelShader),
                    DXGI_FORMAT_UNKNOWN, CullModeForBucket(bucket), false, true, false, L"Meshlet WaterVisibility PSO",
                    waterVisibilityPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }
        }
    }
    if (surfacePipelines.valid())
        stats_.pipelineCreateReadyCount += surfacePipelines.get();

    const auto disableOptionalPipeline = [this](uint32_t mask, PipelineBucketArray& pipelines, const char* diagnostic) {
        if ((pipelineMask_ & mask) == 0u || ArePipelinesReady(pipelines)) {
            return;
        }
        for (auto& pipeline : pipelines) {
            pipeline.Reset();
        }
        pipelineMask_ &= ~mask;
        DEBUGLOG::PushRenderError(diagnostic);
    };
    disableOptionalPipeline(MeshletPipelineMask::GeometryAuxFeatureMask, geometryFeatureMaskPipelineStates_,
                            "[MeshletBackend][WARN] Optional feature-mask PSO creation "
                            "failed; core Mesh rendering remains active.");
    disableOptionalPipeline(MeshletPipelineMask::GeometryAuxFeatureMask, terrainGeometryFeatureMaskPipelineStates_,
                            "[MeshletBackend][WARN] Optional terrain feature-mask PSO creation "
                            "failed; core Mesh rendering remains active.");
    disableOptionalPipeline(MeshletPipelineMask::GeometryOutline, geometryOutlinePipelineStates_,
                            "[MeshletBackend][WARN] Optional geometry-outline PSO creation "
                            "failed; core Mesh rendering remains active.");
    disableOptionalPipeline(MeshletPipelineMask::VisibilityOverlay, visibilityOverlayPipelineStates_,
                            "[MeshletBackend][WARN] Optional visibility-overlay PSO creation "
                            "failed; core Mesh rendering remains active.");

    ResetFrame();
    return stats_.pipelineReady;
}

bool MeshletRenderBackend::PrepareReflectionCapture(bool captureSubsurface) {
    // capture PSO は Initialize が別スレッドで作る。完成までは false を返し、呼び出し側は capture なしで描く。
    if (reflectionCaptureTask_.valid()) {
        if (reflectionCaptureTask_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return false;
        reflectionPipelines_ = reflectionCaptureTask_.get();
    }
    return stats_.forwardPipelineReady && AreReflectionPipelinesReady(captureSubsurface);
}

bool MeshletRenderBackend::AreReflectionPipelinesReady(bool captureSubsurface) const {
    const size_t variant = CaptureVariantIndex(captureSubsurface, false);
    return ArePipelinesReady(reflectionPipelines_.standard[variant]) &&
           ArePipelinesReady(reflectionPipelines_.program[variant]) &&
           ArePipelinesReady(reflectionPipelines_.terrain[variant]);
}

void MeshletRenderBackend::CollectReadDepthPipelines() {
    if (readDepthTask_.valid() && readDepthTask_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        readDepthPipelines_ = readDepthTask_.get();
}

bool MeshletRenderBackend::PrepareForwardVariant(ForwardShadingCostMode mode) {
    if (!device_ || !rootSignature_ || !IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque))
        return false;
    const size_t index = ForwardPipelineVariantIndex(mode);
    if (ArePipelinesReady(forwardPipelineStates_[index]))
        return true;
    if (forwardVariantAttempted_[index])
        return false;
    forwardVariantAttempted_[index] = true;
    const auto caps = GFX::QueryShaderDeviceCapabilities(device_.Get());
    ASSETS::SHADER::ShaderAssetEntryLease amplification, mesh, pixel;
    using Stage = ASSETS::SHADER::ShaderStage;
    if (!ResolveMeshletShader(caps, "HIKARI.EngineShader.Render3D_MeshletGeneralAS", Stage::Amplification, "as_6_5",
                              amplification) ||
        !ResolveMeshletShader(caps, "HIKARI.EngineShader.Render3D_MeshletMS", Stage::Mesh, "ms_6_5", mesh) ||
        !ResolveMeshletShader(caps, kForwardOpaqueProgramIds[index], Stage::Pixel, "ps_6_0", pixel) ||
        !CreateForwardVariantPipelines(device_.Get(), rootSignature_.Get(), amplification, mesh, pixel, index,
                                       forwardPipelineStates_[index], stats_)) {
        ++stats_.failedForwardVariantCount;
        DEBUGLOG::PushRenderError(
            "[MeshletBackend] Diagnostic Forward variant unavailable; using full Forward shading.");
        return false;
    }
    return true;
}

bool MeshletRenderBackend::CreateForwardBucketPipelines(const char* amplificationId, const char* pixelId,
                                                        const wchar_t* debugName, PipelineBucketArray& out) {
    const auto caps = GFX::QueryShaderDeviceCapabilities(device_.Get());
    ASSETS::SHADER::ShaderAssetEntryLease amplification, mesh, pixel;
    using Stage = ASSETS::SHADER::ShaderStage;
    if (!ResolveMeshletShader(caps, amplificationId, Stage::Amplification, "as_6_5", amplification) ||
        !ResolveMeshletShader(caps, "HIKARI.EngineShader.Render3D_MeshletMS", Stage::Mesh, "ms_6_5", mesh) ||
        !ResolveMeshletShader(caps, pixelId, Stage::Pixel, "ps_6_0", pixel))
        return false;
    PipelineBucketArray pipelines{};
    for (size_t i = 0; i < pipelines.size(); ++i) {
        if (!CreateMeshletPipelineState(device_.Get(), rootSignature_.Get(), ShaderBytecode(amplification),
                                        ShaderBytecode(mesh), ShaderBytecode(pixel), DXGI_FORMAT_R16G16B16A16_FLOAT,
                                        CullModeForBucket(static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(i)), false,
                                        true, true, debugName, pipelines[i].GetAddressOf()))
            return false;
    }
    out.swap(pipelines);
    return true;
}

bool MeshletRenderBackend::PrepareForwardDebugPipelines() {
    if (ArePipelinesReady(forwardDebugPipelineStates_))
        return true;
    if (forwardDebugAttempted_ || !device_ || !rootSignature_ ||
        !IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque))
        return false;
    forwardDebugAttempted_ = true;
    if (!CreateForwardBucketPipelines("HIKARI.EngineShader.Render3D_MeshletAS",
                                      "HIKARI.EngineShader.Render3D_GpuDrivenSurfacePS",
                                      L"Meshlet Forward DebugView PSO", forwardDebugPipelineStates_)) {
        DEBUGLOG::PushRenderError("[MeshletBackend] Debug-view Forward pipelines unavailable; using split Forward.");
        return false;
    }
    return true;
}

bool MeshletRenderBackend::PrepareTerrainDiagnosticPipelines() {
    if (ArePipelinesReady(terrainDiagnosticPipelineStates_))
        return true;
    if (terrainDiagnosticAttempted_ || !device_ || !rootSignature_)
        return false;
    terrainDiagnosticAttempted_ = true;
    if (!CreateForwardBucketPipelines("HIKARI.EngineShader.Render3D_MeshletTerrainAS",
                                      "HIKARI.EngineShader.Render3D_GpuDrivenTerrainDiagnosticPS",
                                      L"Meshlet Forward Terrain Diagnostic PSO", terrainDiagnosticPipelineStates_)) {
        DEBUGLOG::PushRenderError(
            "[MeshletBackend] Diagnostic terrain Forward unavailable; using full Forward shading.");
        return false;
    }
    return true;
}

void MeshletRenderBackend::ApplySurfacePartitionReadiness() {
    const auto ready = [this](MeshletPipelineKind kind, const PipelineBucketArray& terrain) {
        return !IsPipelineRequested(pipelineMask_, kind) || ArePipelinesReady(terrain);
    };
    const bool forwardTerrain = ready(MeshletPipelineKind::ForwardOpaque, terrainForwardPipelineStates_) &&
                                ready(MeshletPipelineKind::ForwardOpaque, programForwardPipelineStates_);
    const bool geometryTerrain = ready(MeshletPipelineKind::GeometryAux, terrainGeometryAuxPipelineStates_);
    const bool featureMaskTerrain =
        ready(MeshletPipelineKind::GeometryAuxFeatureMask, terrainGeometryFeatureMaskPipelineStates_);
    stats_.forwardPipelineReady = stats_.forwardPipelineReady && forwardTerrain;
    stats_.geometryAuxPipelineReady = stats_.geometryAuxPipelineReady && geometryTerrain;
    stats_.geometryFeatureMaskPipelineReady = stats_.geometryFeatureMaskPipelineReady && featureMaskTerrain;
    stats_.pipelineReady = stats_.pipelineReady && forwardTerrain && geometryTerrain && featureMaskTerrain;
}

ID3D12PipelineState* MeshletRenderBackend::GetTerrainPipelineState(MeshletPipelineKind kind,
                                                                   GPUDRIVEN::GpuDrivenCommandBucket bucket) const {
    const size_t index = GPUDRIVEN::ToCommandBucketIndex(bucket);
    if (index >= GPUDRIVEN::kGpuDrivenCommandBucketCount) {
        return nullptr;
    }
    switch (kind) {
    case MeshletPipelineKind::ForwardOpaque: return terrainForwardPipelineStates_[index].Get();
    case MeshletPipelineKind::GeometryAux: return terrainGeometryAuxPipelineStates_[index].Get();
    case MeshletPipelineKind::GeometryAuxFeatureMask: return terrainGeometryFeatureMaskPipelineStates_[index].Get();
    default: return nullptr;
    }
}

void MeshletRenderBackend::Reset() {
    ResetVisibility();
    // 作成中の capture PSO は GPU に渡っていないので、完成を待ってそのまま捨てる。
    if (reflectionCaptureTask_.valid())
        (void)reflectionCaptureTask_.get();
    for (auto* captures :
         {&reflectionPipelines_.standard, &reflectionPipelines_.program, &reflectionPipelines_.terrain})
        for (auto& capture : *captures)
            for (auto& pipeline : capture)
                GFX::RetireD3D12ObjectForCurrentFrame(pipeline, "Meshlet.ReflectionCapturePSO");
    const auto retireSplit = [](LightingSplitPipelines& split, const char* name) {
        for (PipelineBucketArray* pipelines :
             {&split.standardProbe, &split.standardLightmap, &split.programProbe, &split.programLightmap})
            for (auto& pipeline : *pipelines)
                GFX::RetireD3D12ObjectForCurrentFrame(pipeline, name);
    };
    for (auto& split : reflectionPipelines_.lightingRead)
        retireSplit(split, "Meshlet.ReflectionCapturePSO");
    retireSplit(readDepthPipelines_.lighting, "Meshlet.ForwardReadDepthPSO");
    if (readDepthTask_.valid())
        (void)readDepthTask_.get();
    for (PipelineBucketArray* pipelines :
         {&readDepthPipelines_.standard, &readDepthPipelines_.program, &readDepthPipelines_.terrain})
        for (auto& pipeline : *pipelines)
            GFX::RetireD3D12ObjectForCurrentFrame(pipeline, "Meshlet.ForwardReadDepthPSO");
    device_.Reset();
    rootSignature_.Reset();
    forwardVariantAttempted_.fill(false);
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
    for (auto& pipeline : geometryFeatureMaskPipelineStates_) {
        pipeline.Reset();
    }
    for (auto& pipeline : geometryOutlinePipelineStates_) {
        pipeline.Reset();
    }
    for (auto& pipeline : visibilityOverlayPipelineStates_) {
        pipeline.Reset();
    }
    for (auto& pipeline : depthPrepassPipelineStates_) {
        pipeline.Reset();
    }
    for (auto& pipeline : waterVisibilityPipelineStates_) {
        pipeline.Reset();
    }
    for (PipelineBucketArray* pipelines :
         {&programForwardPipelineStates_, &terrainForwardPipelineStates_, &terrainGeometryAuxPipelineStates_,
          &terrainGeometryFeatureMaskPipelineStates_}) {
        for (auto& pipeline : *pipelines) {
            pipeline.Reset();
        }
    }
    for (auto& pipeline : forwardDebugPipelineStates_) {
        GFX::RetireD3D12ObjectForCurrentFrame(pipeline, "Meshlet.ForwardDebugPSO");
    }
    for (auto& pipeline : terrainDiagnosticPipelineStates_) {
        GFX::RetireD3D12ObjectForCurrentFrame(pipeline, "Meshlet.TerrainDiagnosticPSO");
    }
    forwardDebugAttempted_ = false;
    terrainDiagnosticAttempted_ = false;
    pipelineMask_ = MeshletPipelineMask::All;
    stats_ = {};
}

void MeshletRenderBackend::ResetFrame() {
    visibility_.ResetFrame();
    const MeshletRenderBackendStats persistent = stats_;
    stats_ = {};
    stats_.visibility = visibility_.GetStats();
    stats_.initialized = persistent.initialized;
    stats_.shaderModel65Supported = persistent.shaderModel65Supported;
    stats_.meshShaderSupported = persistent.meshShaderSupported;
    stats_.meshShaderPipelineStatsSupported = persistent.meshShaderPipelineStatsSupported;
    stats_.shaderCompileReady = persistent.shaderCompileReady;
    stats_.meshShaderTier = persistent.meshShaderTier;
    stats_.pipelineCreateRequestCount = persistent.pipelineCreateRequestCount;
    stats_.pipelineCreateReadyCount = persistent.pipelineCreateReadyCount;
    stats_.preparedForwardVariantCount = persistent.preparedForwardVariantCount;
    stats_.failedForwardVariantCount = persistent.failedForwardVariantCount;

    const bool forwardReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) &&
                              AreBaseForwardPipelinesReady(forwardPipelineStates_);
    const bool depthAwareReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware) &&
                                 ArePipelinesReady(depthAwarePipelineStates_);
    const bool transparentReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent) &&
                                  ArePipelinesReady(transparentPipelineStates_);
    const bool shadowReady =
        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow) && ArePipelinesReady(shadowPipelineStates_);
    const bool geometryReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux) &&
                               ArePipelinesReady(geometryAuxPipelineStates_);
    const bool geometryFeatureMaskReady =
        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAuxFeatureMask) &&
        ArePipelinesReady(geometryFeatureMaskPipelineStates_);
    const bool geometryOutlineReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryOutline) &&
                                      ArePipelinesReady(geometryOutlinePipelineStates_);
    const bool visibilityOverlayReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::VisibilityOverlay) &&
                                        ArePipelinesReady(visibilityOverlayPipelineStates_);
    const bool depthPrepassReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::DepthPrepass) &&
                                   ArePipelinesReady(depthPrepassPipelineStates_);
    const bool waterVisibilityReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::WaterVisibility) &&
                                      ArePipelinesReady(waterVisibilityPipelineStates_);
    stats_.forwardPipelineReady = forwardReady;
    stats_.depthAwarePipelineReady = depthAwareReady;
    stats_.transparentPipelineReady = transparentReady;
    stats_.shadowPipelineReady = shadowReady;
    stats_.geometryAuxPipelineReady = geometryReady;
    stats_.geometryFeatureMaskPipelineReady = geometryFeatureMaskReady;
    stats_.geometryOutlinePipelineReady = geometryOutlineReady;
    stats_.visibilityOverlayPipelineReady = visibilityOverlayReady;
    stats_.depthPrepassPipelineReady = depthPrepassReady;
    stats_.waterVisibilityPipelineReady = waterVisibilityReady;
    stats_.pipelineReady = AreRequestedPipelinesReady(pipelineMask_, forwardPipelineStates_, depthAwarePipelineStates_,
                                                      transparentPipelineStates_, shadowPipelineStates_,
                                                      geometryAuxPipelineStates_, geometryFeatureMaskPipelineStates_,
                                                      geometryOutlinePipelineStates_, visibilityOverlayPipelineStates_,
                                                      depthPrepassPipelineStates_, waterVisibilityPipelineStates_);
    ApplySurfacePartitionReadiness();
}

bool MeshletRenderBackend::Execute(const MeshletRenderExecutionContext& ctx) {
    if (ctx.subsurfaceCapture && !ctx.reflectionCapture)
        return false;
    if (ctx.reflectionCapture &&
        (ctx.pipelineKind != MeshletPipelineKind::ForwardOpaque || !AreReflectionPipelinesReady(ctx.subsurfaceCapture)))
        return false;
    if (ctx.visibility == nullptr || ctx.drawCommandRange == nullptr) {
        return false;
    }

    const GPUDRIVEN::GpuVisibilityResult& visibility = *ctx.visibility;
    const GPUDRIVEN::GpuDrivenDrawCommandRange& range = *ctx.drawCommandRange;
    if (!range.consumable || range.backend != GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader) {
        return false;
    }
    const size_t requestedDispatchCount = range.commandCount;
    stats_.requestedDispatchCount += requestedDispatchCount;
    stats_.dispatchArgumentBufferReady = range.argumentBuffer != nullptr;
    stats_.dispatchCommandSignatureReady = range.commandSignature != nullptr;
    stats_.forwardPipelineReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardOpaque) &&
                                  AreBaseForwardPipelinesReady(forwardPipelineStates_);
    stats_.depthAwarePipelineReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardDepthAware) &&
                                     ArePipelinesReady(depthAwarePipelineStates_);
    stats_.transparentPipelineReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::ForwardTransparent) &&
                                      ArePipelinesReady(transparentPipelineStates_);
    stats_.shadowPipelineReady =
        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::Shadow) && ArePipelinesReady(shadowPipelineStates_);
    stats_.geometryAuxPipelineReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAux) &&
                                      ArePipelinesReady(geometryAuxPipelineStates_);
    stats_.geometryFeatureMaskPipelineReady =
        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryAuxFeatureMask) &&
        ArePipelinesReady(geometryFeatureMaskPipelineStates_);
    stats_.geometryOutlinePipelineReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::GeometryOutline) &&
                                          ArePipelinesReady(geometryOutlinePipelineStates_);
    stats_.visibilityOverlayPipelineReady =
        IsPipelineRequested(pipelineMask_, MeshletPipelineKind::VisibilityOverlay) &&
        ArePipelinesReady(visibilityOverlayPipelineStates_);
    stats_.depthPrepassPipelineReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::DepthPrepass) &&
                                       ArePipelinesReady(depthPrepassPipelineStates_);
    stats_.waterVisibilityPipelineReady = IsPipelineRequested(pipelineMask_, MeshletPipelineKind::WaterVisibility) &&
                                          ArePipelinesReady(waterVisibilityPipelineStates_);
    stats_.pipelineReady = AreRequestedPipelinesReady(pipelineMask_, forwardPipelineStates_, depthAwarePipelineStates_,
                                                      transparentPipelineStates_, shadowPipelineStates_,
                                                      geometryAuxPipelineStates_, geometryFeatureMaskPipelineStates_,
                                                      geometryOutlinePipelineStates_, visibilityOverlayPipelineStates_,
                                                      depthPrepassPipelineStates_, waterVisibilityPipelineStates_);
    ApplySurfacePartitionReadiness();
    const bool requestedPipelineReady =
        ctx.pipelineKind == MeshletPipelineKind::GeometryAuxFeatureMask ? stats_.geometryFeatureMaskPipelineReady
        : ctx.pipelineKind == MeshletPipelineKind::GeometryAux          ? stats_.geometryAuxPipelineReady
        : ctx.pipelineKind == MeshletPipelineKind::GeometryOutline      ? stats_.geometryOutlinePipelineReady
        : ctx.pipelineKind == MeshletPipelineKind::VisibilityOverlay    ? stats_.visibilityOverlayPipelineReady
        : ctx.pipelineKind == MeshletPipelineKind::DepthPrepass         ? stats_.depthPrepassPipelineReady
        : ctx.pipelineKind == MeshletPipelineKind::WaterVisibility      ? stats_.waterVisibilityPipelineReady
        : ctx.pipelineKind == MeshletPipelineKind::ForwardDepthAware    ? stats_.depthAwarePipelineReady
        : ctx.pipelineKind == MeshletPipelineKind::ForwardTransparent   ? stats_.transparentPipelineReady
        : ctx.pipelineKind == MeshletPipelineKind::Shadow               ? stats_.shadowPipelineReady
                                                                        : stats_.forwardPipelineReady;

    if (requestedDispatchCount == 0) {
        return false;
    }
    if (ctx.commandList == nullptr || !range.HasGpuCommandLayout() || !stats_.dispatchArgumentBufferReady ||
        !stats_.dispatchCommandSignatureReady || !requestedPipelineReady ||
        visibility.visibleMeshletRangeBuffer == nullptr) {
        stats_.skippedDispatchCount += requestedDispatchCount;
        return false;
    }

    // capture の PSO は Full だけを描く（診断モードの間は frame graph が capture を使わない）。診断の PSO（地形の面の
    // 分を含む）が作れなければ Full で描く。
    auto effectiveForwardCostMode = ctx.reflectionCapture ? ForwardShadingCostMode::Full : ctx.forwardCostMode;
    if (ctx.pipelineKind == MeshletPipelineKind::ForwardOpaque &&
        effectiveForwardCostMode != ForwardShadingCostMode::Full &&
        (!PrepareForwardVariant(effectiveForwardCostMode) ||
         (ctx.terrainSurfacesPresent && !PrepareTerrainDiagnosticPipelines())))
        effectiveForwardCostMode = ForwardShadingCostMode::Full;

    ID3D12Resource* argumentBuffer = range.argumentBuffer;
    ID3D12Resource* countBuffer = visibility.counterBuffer;
    ID3D12CommandSignature* commandSignature = range.commandSignature;
    if (argumentBuffer == nullptr || countBuffer == nullptr || commandSignature == nullptr) {
        stats_.skippedDispatchCount += requestedDispatchCount;
        return false;
    }

    const GPUDRIVEN::GpuDrivenCommandPassLayout& layout = *range.gpuCommandLayout;
    const size_t bucketCapacity = layout.commandBucketCapacity;
    if (bucketCapacity == 0) {
        stats_.skippedDispatchCount += requestedDispatchCount;
        return false;
    }
    constexpr UINT maxCommandCount = 1u;

    bool submittedAnyBucket = false;
    const bool forwardDebug = ctx.forwardDebugView && !ctx.reflectionCapture &&
                              ctx.pipelineKind == MeshletPipelineKind::ForwardOpaque && PrepareForwardDebugPipelines();
    if (ctx.pipelineKind == MeshletPipelineKind::ForwardOpaque)
        PublishForwardCostMode(forwardDebug ? ForwardShadingCostMode::Full : effectiveForwardCostMode);
    // Full の前向きは、描く PSO の種類（標準 PBR・材質 program・地形）ごとに Meshlet Draw Forward の中で計る。
    // 診断モードと debug view の PSO は一般面すべてを描くので、種類で分けない。
    const bool timeByPipelineClass = ctx.pipelineKind == MeshletPipelineKind::ForwardOpaque && !forwardDebug &&
                                     effectiveForwardCostMode == ForwardShadingCostMode::Full;
    // 深度が揃ったフレームの Forward（Full）は深度を書かない PSO で描く。まだ作れていない PSO は深度を書く方で描く。
    const bool readDepth = ctx.opaqueDepthComplete && ctx.pipelineKind == MeshletPipelineKind::ForwardOpaque &&
                           !forwardDebug && effectiveForwardCostMode == ForwardShadingCostMode::Full;
    if (readDepth)
        CollectReadDepthPipelines();
    const auto pick = [](ID3D12PipelineState* preferred, ID3D12PipelineState* fallback) {
        return preferred != nullptr ? preferred : fallback;
    };
    const size_t captureVariant = CaptureVariantIndex(ctx.subsurfaceCapture, false);
    const size_t captureReadVariant = CaptureVariantIndex(ctx.subsurfaceCapture, true);
    const GFX::GPU_PROFILE::Pass profilePass = ctx.profilePass != GFX::GPU_PROFILE::Pass::Count
                                                   ? ctx.profilePass
                                                   : ProfilePassForPipelineKind(ctx.pipelineKind);
    GFX::GPU_PROFILE::ScopedGpuTimer gpuDraw(ctx.commandList, profilePass);
    GFX::GPU_PIPELINE_STATS::ScopedPipelineStats pipelineStats(ctx.commandList, profilePass);
    for (size_t bucketIndex = 0; bucketIndex < GPUDRIVEN::kGpuDrivenCommandBucketCount; ++bucketIndex) {
        const GPUDRIVEN::GpuDrivenCommandBucket bucket = static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(bucketIndex);
        if (!HasSourceForBucket(visibility, ctx.pass, bucket)) {
            ++stats_.skippedBucketCount;
            continue;
        }

        const auto captureOf = [&](const std::array<PipelineBucketArray, kCaptureVariantCount>& captures) {
            return pick(readDepth ? captures[captureReadVariant][bucketIndex].Get() : nullptr,
                        captures[captureVariant][bucketIndex].Get());
        };
        ID3D12PipelineState* selectedPipeline =
            ctx.reflectionCapture ? captureOf(reflectionPipelines_.standard)
            : forwardDebug        ? forwardDebugPipelineStates_[bucketIndex].Get()
                                  : pick(readDepth ? readDepthPipelines_.standard[bucketIndex].Get() : nullptr,
                                  GetPipelineState(ctx.pipelineKind, bucket, effectiveForwardCostMode));
        if (selectedPipeline == nullptr) {
            continue;
        }
        // 一般 PSO の AS は地形を除外する。同じ可視範囲を地形 PSO（診断モードでは地形の診断 PSO）で描き足す。
        ID3D12PipelineState* terrainPipeline = nullptr;
        if (!forwardDebug && ctx.terrainSurfacesPresent) {
            terrainPipeline = ctx.reflectionCapture ? captureOf(reflectionPipelines_.terrain)
                              : ctx.pipelineKind == MeshletPipelineKind::ForwardOpaque &&
                                      effectiveForwardCostMode != ForwardShadingCostMode::Full
                                  ? terrainDiagnosticPipelineStates_[bucketIndex].Get()
                                  : pick(readDepth ? readDepthPipelines_.terrain[bucketIndex].Get() : nullptr,
                                         GetTerrainPipelineState(ctx.pipelineKind, bucket));
        }
        // 通常の Forward と SSR capture の PSO は標準 PBR の面だけを描く。材質 program の面を別 PSO で描き足す。
        // 診断 variant と debug view の PSO は一般面すべてを描く。
        ID3D12PipelineState* programPipeline = nullptr;
        if (ctx.pipelineKind == MeshletPipelineKind::ForwardOpaque && ctx.materialProgramSurfacesPresent &&
            !forwardDebug) {
            programPipeline = ctx.reflectionCapture ? captureOf(reflectionPipelines_.program)
                              : effectiveForwardCostMode == ForwardShadingCostMode::Full
                                  ? pick(readDepth ? readDepthPipelines_.program[bucketIndex].Get() : nullptr,
                                         programForwardPipelineStates_[bucketIndex].Get())
                                  : nullptr;
        }

        // 深度を読む Forward は、光照贴图の面と探針の面を分けた PSO が揃っていればそれで描く。
        const LightingSplitPipelines& lighting =
            ctx.reflectionCapture ? reflectionPipelines_.lightingRead[ctx.subsurfaceCapture ? 1u : 0u]
                                  : readDepthPipelines_.lighting;
        const bool lightingSplit = readDepth && lighting.IsReady(bucketIndex);

        ++stats_.submitCallCount;
        if (bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided) {
            ++stats_.doubleSidedSubmitCallCount;
        } else {
            ++stats_.backFaceSubmitCallCount;
        }

        GFX::PIX::ScopedGpuEvent pix(ctx.commandList, GFX::PIX::kColorRender,
                                     PipelineEventName(ctx.pipelineKind, bucket));

        const auto submit = [&](ID3D12PipelineState* pipeline, GFX::GPU_PROFILE::Pass classPass) {
            GFX::GPU_PROFILE::ScopedGpuTimer classTimer(
                ctx.commandList, timeByPipelineClass ? classPass : GFX::GPU_PROFILE::Pass::Count);
            ctx.commandList->SetPipelineState(pipeline);
            ctx.commandList->ExecuteIndirect(commandSignature, maxCommandCount, argumentBuffer,
                                             DispatchArgumentOffsetForBucket(layout, bucket), countBuffer,
                                             CounterOffsetForBucket(layout, bucket));
        };
        if (lightingSplit) {
            submit(lighting.standardProbe[bucketIndex].Get(), GFX::GPU_PROFILE::Pass::MeshletForwardStandard);
            submit(lighting.standardLightmap[bucketIndex].Get(), GFX::GPU_PROFILE::Pass::MeshletForwardStandard);
        } else {
            submit(selectedPipeline, GFX::GPU_PROFILE::Pass::MeshletForwardStandard);
        }
        if (programPipeline != nullptr) {
            ++stats_.submitCallCount;
            if (lightingSplit) {
                submit(lighting.programProbe[bucketIndex].Get(), GFX::GPU_PROFILE::Pass::MeshletForwardMaterialProgram);
                submit(lighting.programLightmap[bucketIndex].Get(),
                       GFX::GPU_PROFILE::Pass::MeshletForwardMaterialProgram);
            } else {
                submit(programPipeline, GFX::GPU_PROFILE::Pass::MeshletForwardMaterialProgram);
            }
        }
        if (terrainPipeline != nullptr) {
            ++stats_.submitCallCount;
            submit(terrainPipeline, GFX::GPU_PROFILE::Pass::MeshletForwardTerrain);
        }
        submittedAnyBucket = true;
    }

    if (!submittedAnyBucket) {
        stats_.skippedDispatchCount += requestedDispatchCount;
        return false;
    }

    stats_.submittedDispatchCount += requestedDispatchCount;
    if (ctx.pipelineKind == MeshletPipelineKind::GeometryAux ||
        ctx.pipelineKind == MeshletPipelineKind::GeometryAuxFeatureMask) {
        stats_.geometryAuxSubmittedDispatchCount += requestedDispatchCount;
    } else if (ctx.pipelineKind == MeshletPipelineKind::GeometryOutline) {
        stats_.geometryOutlineSubmittedDispatchCount += requestedDispatchCount;
    } else if (ctx.pipelineKind == MeshletPipelineKind::VisibilityOverlay) {
        stats_.visibilityOverlaySubmittedDispatchCount += requestedDispatchCount;
    } else if (ctx.pipelineKind == MeshletPipelineKind::DepthPrepass) {
        stats_.depthPrepassSubmittedDispatchCount += requestedDispatchCount;
    } else if (ctx.pipelineKind == MeshletPipelineKind::WaterVisibility) {
        stats_.depthPrepassSubmittedDispatchCount += requestedDispatchCount;
    } else if (ctx.pipelineKind == MeshletPipelineKind::Shadow) {
        stats_.shadowSubmittedDispatchCount += requestedDispatchCount;
    } else {
        stats_.forwardSubmittedDispatchCount += requestedDispatchCount;
    }
    return true;
}

const MeshletRenderBackendStats& MeshletRenderBackend::GetStats() const { return stats_; }

ID3D12PipelineState* MeshletRenderBackend::GetPipelineState(MeshletPipelineKind kind,
                                                            GPUDRIVEN::GpuDrivenCommandBucket bucket,
                                                            ForwardShadingCostMode costMode) const {
    const size_t index = GPUDRIVEN::ToCommandBucketIndex(bucket);
    if (index >= GPUDRIVEN::kGpuDrivenCommandBucketCount) {
        return nullptr;
    }
    switch (kind) {
    case MeshletPipelineKind::DepthPrepass: return depthPrepassPipelineStates_[index].Get();
    case MeshletPipelineKind::WaterVisibility: return waterVisibilityPipelineStates_[index].Get();
    case MeshletPipelineKind::GeometryAux: return geometryAuxPipelineStates_[index].Get();
    case MeshletPipelineKind::GeometryAuxFeatureMask: return geometryFeatureMaskPipelineStates_[index].Get();
    case MeshletPipelineKind::GeometryOutline: return geometryOutlinePipelineStates_[index].Get();
    case MeshletPipelineKind::VisibilityOverlay: return visibilityOverlayPipelineStates_[index].Get();
    case MeshletPipelineKind::ForwardDepthAware: return depthAwarePipelineStates_[index].Get();
    case MeshletPipelineKind::ForwardTransparent: return transparentPipelineStates_[index].Get();
    case MeshletPipelineKind::Shadow: return shadowPipelineStates_[index].Get();
    case MeshletPipelineKind::ForwardOpaque:
    default: return forwardPipelineStates_[ForwardPipelineVariantIndex(costMode)][index].Get();
    }
}

} // namespace HIKARI::RENDER3D::MESHLET
