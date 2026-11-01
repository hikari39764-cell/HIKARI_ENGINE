#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI::RENDER3D::SCREENSPACE {

namespace {
struct SsaoPassCB {
    MATH::Mat4 viewProj{};
    MATH::Mat4 invViewProj{};
    MATH::Vec4 cameraPos{};
    MATH::Vec4 screenParams{};
    MATH::Vec4 aoParams0{};
    MATH::Vec4 aoParams1{};
    MATH::Vec4 blurParams{};
    MATH::Vec4 depthScreenParams{};
};

constexpr uint32_t kSsaoConstantSliceCount = 8;

SsaoDebugState gDebugState{};

using CpuClock = std::chrono::steady_clock;

float ElapsedMs(CpuClock::time_point start, CpuClock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - start).count();
}

uint32_t NormalizeBlurIterations(uint32_t value) { return std::clamp<uint32_t>(value, 0u, 4u); }

uint32_t NormalizeHalfResolutionBlurPasses(uint32_t value) {
    if (value == 0u) {
        return 0u;
    }
    return std::clamp<uint32_t>(value * 2u, 2u, 4u);
}

uint32_t NormalizeSampleCount(uint32_t value) {
    if (value <= 8u) {
        return 8u;
    }
    if (value <= 16u) {
        return 16u;
    }
    if (value <= 24u) {
        return 24u;
    }
    return 32u;
}

MATH::Vec4 MakeScreenParams(uint32_t width, uint32_t height) {
    const float w = static_cast<float>(std::max(1u, width));
    const float h = static_cast<float>(std::max(1u, height));
    return {w, h, 1.0f / w, 1.0f / h};
}

struct SsaoResolvedMode {
    SsaoMode mode = SsaoMode::Off;
    // GTAO のスライス数と片側の歩数。読み出し数は slice × step × 2。
    uint32_t sliceCount = 0;
    uint32_t stepCount = 0;
    uint32_t blurIterations = 0;
    float radiusScale = 1.0f;
    float strengthScale = 1.0f;
    bool halfResolution = false;
    const char* pixEventName = "SSAO.Off";
};

// Mode ごとの実行時パラメータを決める。設定の sampleCount（8..32）を片側の歩数へ配り、Mode がスライス数と解像度を決める。
SsaoResolvedMode ResolveModeParameters(const AmbientOcclusionSettings& settings) {
    const uint32_t referenceSamples = NormalizeSampleCount(settings.sampleCount);
    const uint32_t referenceBlur = NormalizeBlurIterations(settings.blurIterations);

    SsaoResolvedMode resolved{};
    resolved.mode = ResolveEffectiveSsaoMode(settings);
    switch (resolved.mode) {
    case SsaoMode::Reference:
        resolved.sliceCount = 4u;
        resolved.stepCount = std::clamp<uint32_t>(referenceSamples / 4u, 2u, 8u);
        resolved.blurIterations = referenceBlur;
        resolved.pixEventName = "SSAO.Reference";
        break;
    case SsaoMode::OptimizedHigh:
        resolved.sliceCount = 3u;
        resolved.stepCount = std::clamp<uint32_t>(referenceSamples / 4u, 2u, 6u);
        resolved.blurIterations = NormalizeHalfResolutionBlurPasses(referenceBlur);
        resolved.radiusScale = 0.92f;
        resolved.halfResolution = true;
        resolved.pixEventName = "SSAO.OptimizedHigh";
        break;
    case SsaoMode::Balanced:
        // 深度から法線を作る半解像度。2 スライスだと球の下側などで角度の偏りが斑に残った。
        resolved.sliceCount = 3u;
        resolved.stepCount = std::clamp<uint32_t>(referenceSamples / 4u, 2u, 4u);
        resolved.blurIterations = NormalizeHalfResolutionBlurPasses(referenceBlur);
        resolved.radiusScale = 0.95f;
        resolved.halfResolution = true;
        resolved.pixEventName = "SSAO.Balanced";
        break;
    case SsaoMode::Off:
    default:
        resolved.blurIterations = 0;
        resolved.pixEventName = "SSAO.Off";
        break;
    }
    return resolved;
}

void FillDebugStateBase(uint32_t width, uint32_t height, const AmbientOcclusionSettings& settings) {
    const SsaoResolvedMode resolved = ResolveModeParameters(settings);
    gDebugState.enabled = resolved.mode != SsaoMode::Off;
    gDebugState.valid = false;
    gDebugState.suppressed = !settings.enabled;
    gDebugState.mode = resolved.mode;
    gDebugState.width = width;
    gDebugState.height = height;
    gDebugState.internalWidth = width;
    gDebugState.internalHeight = height;
    gDebugState.halfResolution = false;
    gDebugState.referenceSampleCount = NormalizeSampleCount(settings.sampleCount);
    gDebugState.referenceBlurIterations = NormalizeBlurIterations(settings.blurIterations);
    gDebugState.sampleCount = resolved.sliceCount * resolved.stepCount * 2u;
    gDebugState.blurIterations = resolved.blurIterations;
    gDebugState.radius = std::max(0.01f, settings.radius) * resolved.radiusScale;
    gDebugState.strength = std::max(0.0f, settings.strength) * resolved.strengthScale;
    gDebugState.power = settings.power;
    gDebugState.pixMarkersAvailable = true;
    gDebugState.mainCpuMs = 0.0f;
    gDebugState.blurCpuMs = 0.0f;
    gDebugState.compositeCpuMs = 0.0f;
    gDebugState.totalCpuMs = 0.0f;
}

bool CompileShader(const wchar_t* path, const char* entry, const wchar_t* target, ID3DBlob** outBlob) {
    if (!GFX::CompileShaderFileSm6(path, entry, target, outBlob)) {
        DEBUGLOG::PushRenderError("[SSAO][ERROR] SM6 shader compile failed.");
        return false;
    }
    return true;
}
} // namespace

const SsaoDebugState& GetSsaoDebugState() { return gDebugState; }

SsaoMode ResolveEffectiveSsaoMode(const AmbientOcclusionSettings& settings) {
    if (!settings.enabled || settings.mode == SsaoMode::Off) {
        return SsaoMode::Off;
    }
    return settings.mode;
}

bool SsaoRequiresGeometryAux(const AmbientOcclusionSettings& settings) {
    const SsaoResolvedMode resolved = ResolveModeParameters(settings);
    return resolved.mode == SsaoMode::Reference || resolved.mode == SsaoMode::OptimizedHigh;
}

void BeginSsaoDebugFrame(uint32_t width, uint32_t height, const AmbientOcclusionSettings& settings) {
    FillDebugStateBase(width, height, settings);
    gDebugState.geometryAuxEnabled = SsaoRequiresGeometryAux(settings) && settings.enabled;
    gDebugState.geometryAuxWritten = false;
    gDebugState.geometryAuxFormat = DXGI_FORMAT_UNKNOWN;
    gDebugState.geometryAuxCpuMs = 0.0f;
}

void RecordSsaoGeometryAuxDebug(bool written, float cpuMs, DXGI_FORMAT format) {
    gDebugState.geometryAuxWritten = written;
    gDebugState.geometryAuxFormat = format;
    gDebugState.geometryAuxCpuMs = cpuMs;
}

void RecordSsaoCompositeDebug(float cpuMs) { gDebugState.compositeCpuMs = cpuMs; }

bool SsaoRenderer::Render(const GFX::Context& graphics, ID3D12GraphicsCommandList* cmd,
                          const SURFACE::SceneSurfaceBuffer& surfaceBuffer, D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
                          const MESHRENDERER::CameraCB& camera, const AmbientOcclusionSettings& settings) {
    if (!surfaceBuffer.IsValid()) {
        valid_ = false;
        lastAoSrv_ = {};
        return false;
    }
    return RenderInternal(graphics, cmd, surfaceBuffer.GetWidth(), surfaceBuffer.GetHeight(), sceneDepthSrv,
                          surfaceBuffer.GetPackedSurfaceSrv(), camera, settings, false);
}

bool SsaoRenderer::RenderDepthOnly(const GFX::Context& graphics, ID3D12GraphicsCommandList* cmd, uint32_t width,
                                   uint32_t height, D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
                                   const MESHRENDERER::CameraCB& camera, const AmbientOcclusionSettings& settings) {
    return RenderInternal(graphics, cmd, width, height, sceneDepthSrv, {}, camera, settings, true);
}

bool SsaoRenderer::RenderInternal(const GFX::Context& graphics, ID3D12GraphicsCommandList* cmd, uint32_t width,
                                  uint32_t height, D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
                                  D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSrv, const MESHRENDERER::CameraCB& camera,
                                  const AmbientOcclusionSettings& settings, bool depthOnlyNormals) {
    const CpuClock::time_point totalStart = CpuClock::now();
    const SsaoResolvedMode modeParams = ResolveModeParameters(settings);
    FillDebugStateBase(width, height, settings);
    gDebugState.geometryAuxEnabled = !depthOnlyNormals && SsaoRequiresGeometryAux(settings);

    valid_ = false;
    lastAoSrv_ = {};

    if (modeParams.mode == SsaoMode::Off || cmd == nullptr || sceneDepthSrv.ptr == 0 ||
        (!depthOnlyNormals && normalRoughnessSrv.ptr == 0)) {
        return false;
    }
    const bool useHalfResolution = modeParams.halfResolution;
    if (!EnsurePipeline(graphics) || !EnsureResources(graphics, width, height, useHalfResolution)) {
        return false;
    }
    activeFrameSlot_ = graphics.frameIndex % GFX::kFrameResourceCount;
    if (!RefreshAoDescriptorsForCurrentFrame(graphics)) {
        return false;
    }
    gDebugState.internalWidth = width_;
    gDebugState.internalHeight = height_;
    gDebugState.halfResolution = halfResolution_;

    GFX::PIX::ScopedGpuEvent pixSsao(cmd, GFX::PIX::kColorPost, modeParams.pixEventName);

    SsaoPassCB cb{};
    cb.viewProj = camera.viewProj;
    cb.invViewProj = camera.invViewProj;
    cb.cameraPos = camera.cameraPos;
    cb.screenParams = MakeScreenParams(screenWidth_, screenHeight_);
    cb.depthScreenParams = MakeScreenParams(screenWidth_, screenHeight_);
    cb.aoParams0 = {std::max(0.01f, settings.radius) * modeParams.radiusScale, std::max(0.0f, settings.bias),
                    std::max(0.0f, settings.strength) * modeParams.strengthScale, std::max(0.1f, settings.power)};
    // Temporal 蓄積を持たないため per-frame jitter は正味のちらつきにしかならない。乱数は画素位置だけで決める。
    cb.aoParams1 = {static_cast<float>(modeParams.sliceCount), static_cast<float>(modeParams.stepCount),
                    static_cast<float>(static_cast<int>(modeParams.mode)), depthOnlyNormals ? 1.0f : 0.0f};

    uint32_t constantSliceIndex = 0;
    const UINT constantStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(SsaoPassCB));
    auto uploadConstants = [&](const SsaoPassCB& constants) -> D3D12_GPU_VIRTUAL_ADDRESS {
        if (constantMapped_ == nullptr || constantBuffer_ == nullptr) {
            return 0;
        }
        if (constantSliceIndex >= kSsaoConstantSliceCount) {
            DEBUGLOG::PushRenderError("[SSAO][ERROR] Constant buffer slice overflow.");
            return 0;
        }
        const UINT64 offset = static_cast<UINT64>(constantStride) * constantSliceIndex;
        std::memcpy(constantMapped_ + offset, &constants, sizeof(constants));
        ++constantSliceIndex;
        return constantBuffer_->GetGPUVirtualAddress() + offset;
    };

    auto setViewport = [&](uint32_t viewportWidth, uint32_t viewportHeight) {
        const uint32_t safeWidth = std::max(1u, viewportWidth);
        const uint32_t safeHeight = std::max(1u, viewportHeight);
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(safeWidth);
        viewport.Height = static_cast<float>(safeHeight);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{0, 0, static_cast<LONG>(safeWidth), static_cast<LONG>(safeHeight)};
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
    };

    ID3D12DescriptorHeap* heaps[] = {graphics.srvHeap};
    cmd->SetDescriptorHeaps(1, heaps);

    // R8 AO target の optimized clear と完全一致させる。
    const float white[] = {1.0f, 0.0f, 0.0f, 0.0f};

    setViewport(width_, height_);

    const CpuClock::time_point mainStart = CpuClock::now();
    {
        GFX::PIX::ScopedGpuEvent pixMain(cmd, GFX::PIX::kColorPost, "SSAO.Main");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuMain(cmd, GFX::GPU_PROFILE::Pass::SsaoMain);
        Transition(cmd, rawAo_.Get(), rawState_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->OMSetRenderTargets(1, &rawRtv_, FALSE, nullptr);
        cmd->ClearRenderTargetView(rawRtv_, white, 0, nullptr);

        ID3D12PipelineState* pso = depthOnlyNormals ? depthOnlyGeneratePso_.Get() : generatePso_.Get();
        const D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadConstants(cb);
        if (cbAddress == 0) {
            return false;
        }
        cmd->SetGraphicsRootSignature(depthOnlyNormals ? depthOnlyGenerateRootSig_.Get() : generateRootSig_.Get());
        cmd->SetGraphicsRootConstantBufferView(0, cbAddress);
        cmd->SetGraphicsRootDescriptorTable(1, sceneDepthSrv);
        if (!depthOnlyNormals) {
            cmd->SetGraphicsRootDescriptorTable(2, normalRoughnessSrv);
        }
        cmd->SetPipelineState(pso);
        DrawFullscreen(cmd);
        Transition(cmd, rawAo_.Get(), rawState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    gDebugState.mainCpuMs = ElapsedMs(mainStart, CpuClock::now());

    D3D12_GPU_DESCRIPTOR_HANDLE sourceSrv = rawSrvGpu_[activeFrameSlot_];
    D3D12_CPU_DESCRIPTOR_HANDLE targetRtv = blurredRtv_;
    D3D12_RESOURCE_STATES* targetState = &blurredState_;
    ID3D12Resource* targetResource = blurredAo_.Get();

    const CpuClock::time_point blurStart = CpuClock::now();
    const uint32_t blurIterations = modeParams.blurIterations;
    D3D12_GPU_DESCRIPTOR_HANDLE filteredSrv{};
    {
        GFX::GPU_PROFILE::ScopedGpuTimer gpuBlur(cmd, GFX::GPU_PROFILE::Pass::SsaoBlur);
        cb.screenParams = MakeScreenParams(width_, height_);
        for (uint32_t i = 0; i < blurIterations; ++i) {
            const bool horizontal = (i % 2u) == 0u;
            cb.blurParams = {horizontal ? 1.0f : 0.0f, horizontal ? 0.0f : 1.0f, 0.0f, 0.0f};
            const D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadConstants(cb);
            if (cbAddress == 0) {
                return false;
            }

            GFX::PIX::ScopedGpuEvent pixBlur(cmd, GFX::PIX::kColorPost,
                                             horizontal ? "SSAO.BlurHorizontal" : "SSAO.BlurVertical");

            Transition(cmd, targetResource, *targetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->OMSetRenderTargets(1, &targetRtv, FALSE, nullptr);
            cmd->ClearRenderTargetView(targetRtv, white, 0, nullptr);

            cmd->SetGraphicsRootSignature(depthOnlyNormals ? depthOnlyBlurRootSig_.Get() : blurRootSig_.Get());
            cmd->SetGraphicsRootConstantBufferView(0, cbAddress);
            cmd->SetGraphicsRootDescriptorTable(1, sourceSrv);
            cmd->SetGraphicsRootDescriptorTable(2, sceneDepthSrv);
            if (!depthOnlyNormals) {
                cmd->SetGraphicsRootDescriptorTable(3, normalRoughnessSrv);
            }
            cmd->SetPipelineState(depthOnlyNormals ? depthOnlyBlurPso_.Get() : blurPso_.Get());
            DrawFullscreen(cmd);

            Transition(cmd, targetResource, *targetState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            if (targetResource == blurredAo_.Get()) {
                sourceSrv = blurredSrvGpu_[activeFrameSlot_];
                targetRtv = rawRtv_;
                targetState = &rawState_;
                targetResource = rawAo_.Get();
            } else {
                sourceSrv = rawSrvGpu_[activeFrameSlot_];
                targetRtv = blurredRtv_;
                targetState = &blurredState_;
                targetResource = blurredAo_.Get();
            }
        }

        filteredSrv = blurIterations == 0u ? rawSrvGpu_[activeFrameSlot_] : sourceSrv;
        if (useHalfResolution) {
            GFX::PIX::ScopedGpuEvent pixUpsample(cmd, GFX::PIX::kColorPost, "SSAO.DepthAwareUpsample");

            cb.screenParams = MakeScreenParams(screenWidth_, screenHeight_);
            cb.blurParams = {0.0f, 0.0f, 1.0f / static_cast<float>(std::max(1u, width_)),
                             1.0f / static_cast<float>(std::max(1u, height_))};
            const D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadConstants(cb);
            if (cbAddress == 0) {
                return false;
            }

            setViewport(screenWidth_, screenHeight_);
            Transition(cmd, resolvedAo_.Get(), resolvedState_, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->OMSetRenderTargets(1, &resolvedRtv_, FALSE, nullptr);
            cmd->ClearRenderTargetView(resolvedRtv_, white, 0, nullptr);

            cmd->SetGraphicsRootSignature(depthOnlyNormals ? depthOnlyBlurRootSig_.Get() : blurRootSig_.Get());
            cmd->SetGraphicsRootConstantBufferView(0, cbAddress);
            cmd->SetGraphicsRootDescriptorTable(1, filteredSrv);
            cmd->SetGraphicsRootDescriptorTable(2, sceneDepthSrv);
            if (!depthOnlyNormals) {
                cmd->SetGraphicsRootDescriptorTable(3, normalRoughnessSrv);
            }
            cmd->SetPipelineState(depthOnlyNormals ? depthOnlyUpsamplePso_.Get() : upsamplePso_.Get());
            DrawFullscreen(cmd);
            Transition(cmd, resolvedAo_.Get(), resolvedState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            filteredSrv = resolvedSrvGpu_[activeFrameSlot_];
        }
    }
    gDebugState.blurCpuMs = ElapsedMs(blurStart, CpuClock::now());

    lastAoSrv_ = filteredSrv;
    valid_ = lastAoSrv_.ptr != 0;
    gDebugState.valid = valid_;
    gDebugState.totalCpuMs =
        gDebugState.geometryAuxCpuMs + ElapsedMs(totalStart, CpuClock::now()) + gDebugState.compositeCpuMs;
    return valid_;
}

void SsaoRenderer::RecordSkipped(uint32_t width, uint32_t height, const AmbientOcclusionSettings& settings) {
    valid_ = false;
    lastAoSrv_ = {};

    FillDebugStateBase(width, height, settings);
    gDebugState.geometryAuxEnabled = false;
    gDebugState.geometryAuxWritten = false;
    gDebugState.geometryAuxFormat = DXGI_FORMAT_UNKNOWN;
    gDebugState.geometryAuxCpuMs = 0.0f;
}

bool SsaoRenderer::EnsureResources(const GFX::Context& graphics, uint32_t width, uint32_t height, bool halfResolution) {
    const uint32_t screenWidth = std::max(1u, width);
    const uint32_t screenHeight = std::max(1u, height);
    // Optimized 系は AO の作業解像度だけを下げ、外部へ渡す SRV は全解像度に戻す。
    const uint32_t aoWidth = halfResolution ? std::max(1u, (screenWidth + 1u) / 2u) : screenWidth;
    const uint32_t aoHeight = halfResolution ? std::max(1u, (screenHeight + 1u) / 2u) : screenHeight;
    if (rawAo_ && blurredAo_ && resolvedAo_ && screenWidth_ == screenWidth && screenHeight_ == screenHeight &&
        width_ == aoWidth && height_ == aoHeight && halfResolution_ == halfResolution && constantBuffer_) {
        return true;
    }

    ID3D12Device* device = graphics.device;
    if (device == nullptr || graphics.srvHeap == nullptr) {
        return false;
    }

    GFX::RetireD3D12ObjectForFrame(rawAo_, graphics, "SSAO.RawAO");
    GFX::RetireD3D12ObjectForFrame(blurredAo_, graphics, "SSAO.BlurredAO");
    GFX::RetireD3D12ObjectForFrame(resolvedAo_, graphics, "SSAO.ResolvedAO");
    GFX::RetireD3D12ObjectForFrame(rtvHeap_, graphics, "SSAO.RTVHeap");
    rawRtv_ = {};
    blurredRtv_ = {};
    resolvedRtv_ = {};
    rawSrvCpu_.fill({});
    blurredSrvCpu_.fill({});
    resolvedSrvCpu_.fill({});
    rawSrvGpu_.fill({});
    blurredSrvGpu_.fill({});
    resolvedSrvGpu_.fill({});
    rawSrvResources_.fill(nullptr);
    blurredSrvResources_.fill(nullptr);
    resolvedSrvResources_.fill(nullptr);
    rawSrvGenerations_.fill(0u);
    blurredSrvGenerations_.fill(0u);
    resolvedSrvGenerations_.fill(0u);
    rawState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    blurredState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    resolvedState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 3;
    HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "SsaoRenderer::Create RTV heap")) {
        return false;
    }

    const UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    rawRtv_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    blurredRtv_ = rawRtv_;
    blurredRtv_.ptr += rtvSize;
    resolvedRtv_ = blurredRtv_;
    resolvedRtv_.ptr += rtvSize;

    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    width_ = aoWidth;
    height_ = aoHeight;
    halfResolution_ = halfResolution;

    if (!CreateAoResource(graphics, aoWidth, aoHeight, rawAo_, rawRtv_, L"HIKARI.SSAO.Raw")) {
        return false;
    }
    if (!CreateAoResource(graphics, aoWidth, aoHeight, blurredAo_, blurredRtv_, L"HIKARI.SSAO.Blurred")) {
        return false;
    }
    if (!CreateAoResource(graphics, screenWidth, screenHeight, resolvedAo_, resolvedRtv_, L"HIKARI.SSAO.Resolved")) {
        return false;
    }

    if (!constantBuffer_) {
        const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const UINT constantStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(SsaoPassCB));
        const auto cbDesc =
            CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(constantStride) * kSsaoConstantSliceCount);
        hr = GFX::VIDEO_MEMORY::CreateCommittedResource(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            GFX::VIDEO_MEMORY::Owner::ScreenSpace, IID_PPV_ARGS(constantBuffer_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "SsaoRenderer::Create constant buffer")) {
            return false;
        }
        if (FAILED(constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantMapped_)))) {
            return false;
        }
        constantBuffer_->SetName(L"HIKARI.SSAO.CB");
    }

    HIKARI_LOG_INFO("[SSAO] resized screen " + std::to_string(screenWidth_) + "x" + std::to_string(screenHeight_) +
                    ", ao " + std::to_string(width_) + "x" + std::to_string(height_));
    return true;
}

RenderTextureView SsaoRenderer::GetAoView() const {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (lastAoSrv_.ptr == resolvedSrvGpu_[activeFrameSlot_].ptr) {
        resource = resolvedAo_.Get();
        resourceState = resolvedState_;
    } else if (lastAoSrv_.ptr == blurredSrvGpu_[activeFrameSlot_].ptr) {
        resource = blurredAo_.Get();
        resourceState = blurredState_;
    } else if (lastAoSrv_.ptr == rawSrvGpu_[activeFrameSlot_].ptr) {
        resource = rawAo_.Get();
        resourceState = rawState_;
    }
    return {resource,      lastAoSrv_, DXGI_FORMAT_R8_UNORM, screenWidth_,
            screenHeight_, 1,          resourceState,        valid_ && resource != nullptr && lastAoSrv_.ptr != 0};
}

bool SsaoRenderer::RefreshAoDescriptorsForCurrentFrame(const GFX::Context& graphics) {
    if (graphics.device == nullptr || graphics.srvHeap == nullptr || rawAo_ == nullptr || blurredAo_ == nullptr ||
        resolvedAo_ == nullptr) {
        return false;
    }

    const uint32_t frameSlot = graphics.frameIndex % GFX::kFrameResourceCount;
    const UINT descriptorSize =
        graphics.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    const auto refresh = [&](ID3D12Resource* resource, GFX::DESCRIPTOR::SystemSrv descriptorSlot,
                             D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu,
                             ID3D12Resource*& publishedResource, uint64_t& publishedGeneration) {
        if (publishedResource == resource && publishedGeneration == graphics.deviceGeneration && cpu.ptr != 0 &&
            gpu.ptr != 0) {
            return true;
        }

        cpu = GFX::DESCRIPTOR::CpuAt(graphics.srvHeap, descriptorSize, GFX::DESCRIPTOR::ToIndex(descriptorSlot));
        gpu = GFX::DESCRIPTOR::GpuAt(graphics.srvHeap, descriptorSize, GFX::DESCRIPTOR::ToIndex(descriptorSlot));
        if (cpu.ptr == 0 || gpu.ptr == 0) {
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        graphics.device->CreateShaderResourceView(resource, &srv, cpu);
        publishedResource = resource;
        publishedGeneration = graphics.deviceGeneration;
        return true;
    };

    return refresh(rawAo_.Get(), GFX::DESCRIPTOR::SsaoRawFrameSlot(graphics.frameIndex), rawSrvCpu_[frameSlot],
                   rawSrvGpu_[frameSlot], rawSrvResources_[frameSlot], rawSrvGenerations_[frameSlot]) &&
           refresh(blurredAo_.Get(), GFX::DESCRIPTOR::SsaoBlurredFrameSlot(graphics.frameIndex),
                   blurredSrvCpu_[frameSlot], blurredSrvGpu_[frameSlot], blurredSrvResources_[frameSlot],
                   blurredSrvGenerations_[frameSlot]) &&
           refresh(resolvedAo_.Get(), GFX::DESCRIPTOR::SsaoResolvedFrameSlot(graphics.frameIndex),
                   resolvedSrvCpu_[frameSlot], resolvedSrvGpu_[frameSlot], resolvedSrvResources_[frameSlot],
                   resolvedSrvGenerations_[frameSlot]);
}

bool SsaoRenderer::CreateAoResource(const GFX::Context& graphics, uint32_t width, uint32_t height,
                                    Microsoft::WRL::ComPtr<ID3D12Resource>& outResource,
                                    D3D12_CPU_DESCRIPTOR_HANDLE& outRtv, const wchar_t* debugName) {
    ID3D12Device* device = graphics.device;
    if (device == nullptr) {
        return false;
    }

    const D3D12_CLEAR_VALUE clearValue{DXGI_FORMAT_R8_UNORM, {1.0f, 0.0f, 0.0f, 0.0f}};
    const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UNORM, width, height, 1, 1, 1, 0,
                                                   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    HRESULT hr = GFX::VIDEO_MEMORY::CreateCommittedResource(
        device, &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        GFX::VIDEO_MEMORY::Owner::ScreenSpace, IID_PPV_ARGS(outResource.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "SsaoRenderer::Create AO texture")) {
        return false;
    }
    outResource->SetName(debugName);
    device->CreateRenderTargetView(outResource.Get(), nullptr, outRtv);
    return true;
}

bool SsaoRenderer::EnsurePipeline(const GFX::Context& graphics) {
    if (generatePso_ && blurPso_ && upsamplePso_ && depthOnlyGeneratePso_ && depthOnlyBlurPso_ &&
        depthOnlyUpsamplePso_) {
        return true;
    }

    ID3D12Device* device = graphics.device;
    if (device == nullptr) {
        return false;
    }
    if (!GFX::SupportsShaderModel6(device)) {
        DEBUGLOG::PushRenderError("[SSAO][ERROR] Shader Model 6.0 is not supported by this device.");
        return false;
    }

    D3D12_STATIC_SAMPLER_DESC pointSampler{};
    pointSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    pointSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointSampler.ShaderRegister = 0;
    pointSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC linearSampler = pointSampler;
    linearSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linearSampler.ShaderRegister = 1;

    const D3D12_STATIC_SAMPLER_DESC samplers[] = {pointSampler, linearSampler};

    auto createRootSignature = [&](UINT textureCount, ID3D12RootSignature** outRootSig) -> bool {
        std::array<D3D12_DESCRIPTOR_RANGE, 3> ranges{};
        std::array<D3D12_ROOT_PARAMETER, 4> params{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[0].Descriptor.ShaderRegister = 0;

        for (UINT i = 0; i < textureCount; ++i) {
            ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[i].NumDescriptors = 1;
            ranges[i].BaseShaderRegister = i;
            ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[i + 1].DescriptorTable.NumDescriptorRanges = 1;
            params[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
        }

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = textureCount + 1;
        rsDesc.pParameters = params.data();
        rsDesc.NumStaticSamplers = static_cast<UINT>(std::size(samplers));
        rsDesc.pStaticSamplers = samplers;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        return SUCCEEDED(GFX::CreateRootSignature(device, rsDesc, outRootSig));
    };

    if (!createRootSignature(2, generateRootSig_.GetAddressOf()) ||
        !createRootSignature(1, depthOnlyGenerateRootSig_.GetAddressOf()) ||
        !createRootSignature(3, blurRootSig_.GetAddressOf()) ||
        !createRootSignature(2, depthOnlyBlurRootSig_.GetAddressOf())) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> generateVs;
    Microsoft::WRL::ComPtr<ID3DBlob> generatePs;
    Microsoft::WRL::ComPtr<ID3DBlob> depthOnlyGeneratePs;
    Microsoft::WRL::ComPtr<ID3DBlob> blurVs;
    Microsoft::WRL::ComPtr<ID3DBlob> blurPs;
    Microsoft::WRL::ComPtr<ID3DBlob> upsamplePs;
    Microsoft::WRL::ComPtr<ID3DBlob> depthOnlyBlurPs;
    Microsoft::WRL::ComPtr<ID3DBlob> depthOnlyUpsamplePs;
    if (!CompileShader(L"HIKARI/Shaders/Post_SSAOPS.hlsl", "VSMain", L"vs_6_0", generateVs.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Post_SSAOPS.hlsl", "PSMain", L"ps_6_0", generatePs.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Post_SSAOPS.hlsl", "PSMainDepthOnly", L"ps_6_0",
                       depthOnlyGeneratePs.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Post_SSAOBlurPS.hlsl", "VSMain", L"vs_6_0", blurVs.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Post_SSAOBlurPS.hlsl", "PSMain", L"ps_6_0", blurPs.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Post_SSAOBlurPS.hlsl", "PSMainUpsample", L"ps_6_0",
                       upsamplePs.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Post_SSAOBlurPS.hlsl", "PSMainDepthOnly", L"ps_6_0",
                       depthOnlyBlurPs.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Post_SSAOBlurPS.hlsl", "PSMainUpsampleDepthOnly", L"ps_6_0",
                       depthOnlyUpsamplePs.GetAddressOf())) {
        return false;
    }

    auto makePso = [&](ID3D12RootSignature* rootSig, ID3DBlob* vs, ID3DBlob* ps, ID3D12PipelineState** outPso) -> bool {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSig;
        desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.InputLayout = {nullptr, 0};
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(outPso)));
    };

    return makePso(generateRootSig_.Get(), generateVs.Get(), generatePs.Get(), generatePso_.GetAddressOf()) &&
           makePso(depthOnlyGenerateRootSig_.Get(), generateVs.Get(), depthOnlyGeneratePs.Get(),
                   depthOnlyGeneratePso_.GetAddressOf()) &&
           makePso(blurRootSig_.Get(), blurVs.Get(), blurPs.Get(), blurPso_.GetAddressOf()) &&
           makePso(blurRootSig_.Get(), blurVs.Get(), upsamplePs.Get(), upsamplePso_.GetAddressOf()) &&
           makePso(depthOnlyBlurRootSig_.Get(), blurVs.Get(), depthOnlyBlurPs.Get(),
                   depthOnlyBlurPso_.GetAddressOf()) &&
           makePso(depthOnlyBlurRootSig_.Get(), blurVs.Get(), depthOnlyUpsamplePs.Get(),
                   depthOnlyUpsamplePso_.GetAddressOf());
}

void SsaoRenderer::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state,
                              D3D12_RESOURCE_STATES nextState) {
    if (cmd == nullptr || resource == nullptr || state == nextState) {
        state = nextState;
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, state, nextState);
    cmd->ResourceBarrier(1, &barrier);
    state = nextState;
}

void SsaoRenderer::DrawFullscreen(ID3D12GraphicsCommandList* cmd) {
    if (cmd == nullptr) {
        return;
    }
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 0, nullptr);
    cmd->IASetIndexBuffer(nullptr);
    cmd->DrawInstanced(3, 1, 0, 0);
}

void SsaoRenderer::Release() {
    if (constantBuffer_ && constantMapped_) {
        constantBuffer_->Unmap(0, nullptr);
        constantMapped_ = nullptr;
    }
    GFX::RetireD3D12ObjectForCurrentFrame(constantBuffer_, "SSAO.ConstantBuffer");
    GFX::RetireD3D12ObjectForCurrentFrame(rawAo_, "SSAO.RawAO");
    GFX::RetireD3D12ObjectForCurrentFrame(blurredAo_, "SSAO.BlurredAO");
    GFX::RetireD3D12ObjectForCurrentFrame(resolvedAo_, "SSAO.ResolvedAO");
    GFX::RetireD3D12ObjectForCurrentFrame(rtvHeap_, "SSAO.RTVHeap");
    generateRootSig_.Reset();
    depthOnlyGenerateRootSig_.Reset();
    generatePso_.Reset();
    depthOnlyGeneratePso_.Reset();
    blurRootSig_.Reset();
    depthOnlyBlurRootSig_.Reset();
    blurPso_.Reset();
    upsamplePso_.Reset();
    depthOnlyBlurPso_.Reset();
    depthOnlyUpsamplePso_.Reset();
    lastAoSrv_ = {};
    valid_ = false;
    activeFrameSlot_ = 0u;
    rawSrvCpu_.fill({});
    blurredSrvCpu_.fill({});
    resolvedSrvCpu_.fill({});
    rawSrvGpu_.fill({});
    blurredSrvGpu_.fill({});
    resolvedSrvGpu_.fill({});
    rawSrvResources_.fill(nullptr);
    blurredSrvResources_.fill(nullptr);
    resolvedSrvResources_.fill(nullptr);
    rawSrvGenerations_.fill(0u);
    blurredSrvGenerations_.fill(0u);
    resolvedSrvGenerations_.fill(0u);
    screenWidth_ = 0;
    screenHeight_ = 0;
    width_ = 0;
    height_ = 0;
    halfResolution_ = false;
    rawState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    blurredState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    resolvedState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    gDebugState = {};
}

} // namespace HIKARI::RENDER3D::SCREENSPACE
