#include "Render3D/DepthOfField/HIKARI_CameraDepthOfFieldStage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include <d3dx12.h>
#include <wrl/client.h>

#include "RenderContracts/Shader/HIKARI_CameraDepthOfFieldShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::DOF {
namespace {
using Microsoft::WRL::ComPtr;
constexpr uint32_t kLayerTargetCount = 6u;
constexpr DXGI_FORMAT kLayerColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kLayerMetadataFormat = DXGI_FORMAT_R32G32_FLOAT;
constexpr DXGI_FORMAT kLayerDepthFormat = DXGI_FORMAT_R32_FLOAT;
constexpr uint32_t kInitialPipelineRetryDelayFrames = 30u;
constexpr uint32_t kMaximumPipelineRetryDelayFrames = 300u;

struct alignas(16) CameraDepthOfFieldConstants {
    MATH::Mat4 inverseViewProjection{};
    MATH::Vec4 cameraPosition{};
    MATH::Vec4 cameraForward{};
    MATH::Vec4 focusBand{};
    MATH::Vec4 blurAndRenderSize{};
    MATH::Vec4 fogDistancesAndVolumeSize{};
    MATH::Vec4 frameAndQuality{};
};
static_assert(sizeof(CameraDepthOfFieldConstants) == 160u);

struct Texture2DResource {
    ComPtr<ID3D12Resource> resource{};
    RenderResourceView srv{};
    RenderResourceView uav{};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t width = 0u;
    uint32_t height = 0u;

    bool IsValid() const noexcept { return resource != nullptr && width != 0u && height != 0u; }
};

struct StageState {
    GFX::Context context{};
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> gatherPipeline{};
    ComPtr<ID3D12PipelineState> compositePipeline{};
    ComPtr<ID3D12DescriptorHeap> layerRtvHeap{};
    std::array<Texture2DResource, kLayerTargetCount> layers{};
    std::array<Texture2DResource, 4u> spriteBlur{};
    Texture2DResource output{};
    ComPtr<ID3D12Resource> fallbackFogVolume{};
    RenderResourceView fallbackFogSrv{};
    RenderResourceView sceneColorSrv{};
    RenderResourceView sceneDepthSrv{};
    ID3D12Resource* sceneColorResource = nullptr;
    ID3D12Resource* sceneDepthResource = nullptr;
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads{};
    D3D12_GPU_VIRTUAL_ADDRESS constantAddress = 0u;
    void* constantsCpuAddress = nullptr;
    WorldSpriteLayerCapture capture{};
    uint64_t deviceGeneration = 0u;
    uint64_t preparedFrame = (std::numeric_limits<uint64_t>::max)();
    uint64_t nextPipelineRetryFrame = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t layerWidth = 0u;
    uint32_t layerHeight = 0u;
    uint32_t halfWidth = 0u;
    uint32_t halfHeight = 0u;
    uint32_t rtvDescriptorSize = 0u;
    uint32_t pipelineRetryDelayFrames = 0u;
    bool pipelineReady = false;
    bool gatherReady = false;
    bool pipelineFailureReported = false;
    bool prepared = false;
    bool capturePrepared = false;
    bool captureInProgress = false;
    bool layersCaptured = false;
};

StageState& State() {
    static StageState state{};
    return state;
}

uint32_t ResolveTapCount(DepthOfFieldQuality quality) noexcept {
    switch (quality) {
    case DepthOfFieldQuality::Low: return 8u;
    case DepthOfFieldQuality::High: return 16u;
    case DepthOfFieldQuality::Medium:
    default: return 12u;
    }
}

void RetireView(StageState& state, RenderResourceView& view, const char* name) noexcept {
    if (view.IsValid()) {
        RetireRenderResourceDescriptor(view, state.context, name);
    }
}

void RetireTexture(StageState& state, Texture2DResource& texture, const char* name) noexcept {
    RetireView(state, texture.srv, name);
    RetireView(state, texture.uav, name);
    GFX::RetireD3D12ObjectForFrame(texture.resource, state.context, name);
    texture = {};
}

void ReleaseLayerTargets(StageState& state) noexcept {
    for (Texture2DResource& texture : state.layers) {
        RetireTexture(state, texture, "CameraDepthOfField.SpriteLayer");
    }
    for (Texture2DResource& texture : state.spriteBlur) {
        RetireTexture(state, texture, "CameraDepthOfField.SpriteBlur");
    }
    if (state.layerRtvHeap != nullptr) {
        GFX::RetireD3D12ObjectForFrame(state.layerRtvHeap, state.context, "CameraDepthOfField.SpriteLayerRTVHeap");
    }
    state.rtvDescriptorSize = 0u;
    state.layerWidth = 0u;
    state.layerHeight = 0u;
    state.halfWidth = 0u;
    state.halfHeight = 0u;
    state.capturePrepared = false;
    state.captureInProgress = false;
    state.layersCaptured = false;
}

void ReleaseAll(StageState& state) noexcept {
    ReleaseLayerTargets(state);
    RetireTexture(state, state.output, "CameraDepthOfField.Output");
    RetireView(state, state.sceneColorSrv, "CameraDepthOfField.SceneColorSRV");
    RetireView(state, state.sceneDepthSrv, "CameraDepthOfField.SceneDepthSRV");
    state.sceneColorResource = nullptr;
    state.sceneDepthResource = nullptr;
    RetireView(state, state.fallbackFogSrv, "CameraDepthOfField.FallbackFogSRV");
    GFX::RetireD3D12ObjectForFrame(state.fallbackFogVolume, state.context, "CameraDepthOfField.FallbackFogVolume");
    GFX::RetireD3D12ObjectForFrame(state.rootSignature, state.context, "CameraDepthOfField.RootSignature");
    GFX::RetireD3D12ObjectForFrame(state.gatherPipeline, state.context, "CameraDepthOfField.GatherPipeline");
    GFX::RetireD3D12ObjectForFrame(state.compositePipeline, state.context, "CameraDepthOfField.CompositePipeline");
    for (DX::DynamicUploadBuffer& upload : state.uploads) {
        upload.Finalize(&state.context);
    }
    state.constantAddress = 0u;
    state.constantsCpuAddress = nullptr;
    state.pipelineReady = false;
    state.gatherReady = false;
    state.nextPipelineRetryFrame = 0u;
    state.pipelineRetryDelayFrames = 0u;
    state.pipelineFailureReported = false;
    state.width = 0u;
    state.height = 0u;
    state.halfWidth = 0u;
    state.halfHeight = 0u;
    state.prepared = false;
    state.preparedFrame = (std::numeric_limits<uint64_t>::max)();
}

bool CreateRootSignature(ID3D12Device* device, ID3D12RootSignature** output) {
    if (device == nullptr || output == nullptr) {
        return false;
    }
    std::array<D3D12_DESCRIPTOR_RANGE, 14u> ranges{};
    std::array<D3D12_ROOT_PARAMETER, 15u> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0u;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (uint32_t index = 0u; index < 9u; ++index) {
        D3D12_DESCRIPTOR_RANGE& range = ranges[index];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1u;
        range.BaseShaderRegister = index;
        D3D12_ROOT_PARAMETER& parameter = parameters[index + 1u];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable = {1u, &range};
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (uint32_t index = 0u; index < 5u; ++index) {
        D3D12_DESCRIPTOR_RANGE& range = ranges[index + 9u];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 1u;
        range.BaseShaderRegister = index;
        D3D12_ROOT_PARAMETER& parameter = parameters[index + 10u];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable = {1u, &range};
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0u;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.NumStaticSamplers = 1u;
    description.pStaticSamplers = &sampler;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(device, description, output),
                           "CameraDepthOfField::CreateRootSignature");
}

void SchedulePipelineRetry(StageState& state, uint64_t frameIndex, const std::string& diagnostic) {
    if (!state.pipelineFailureReported) {
        HIKARI_LOG_WARN("[CameraDepthOfField] Shader pipeline unavailable: " + diagnostic);
        state.pipelineFailureReported = true;
    }
    state.pipelineRetryDelayFrames =
        state.pipelineRetryDelayFrames == 0u
            ? kInitialPipelineRetryDelayFrames
            : std::min(state.pipelineRetryDelayFrames * 2u, kMaximumPipelineRetryDelayFrames);
    const uint64_t maxFrame = (std::numeric_limits<uint64_t>::max)();
    state.nextPipelineRetryFrame =
        frameIndex > maxFrame - state.pipelineRetryDelayFrames ? maxFrame : frameIndex + state.pipelineRetryDelayFrames;
}

bool EnsurePipeline(StageState& state, uint64_t frameIndex, bool requireGather) {
    if (state.pipelineReady && state.gatherReady) {
        state.nextPipelineRetryFrame = 0u;
        state.pipelineRetryDelayFrames = 0u;
        state.pipelineFailureReported = false;
        return true;
    }
    if (state.pipelineReady && !requireGather) {
        return true;
    }
    if (frameIndex < state.nextPipelineRetryFrame) {
        return state.pipelineReady;
    }
    if (state.context.device == nullptr) {
        SchedulePipelineRetry(state, frameIndex, "D3D12 device is unavailable");
        return false;
    }
    if (state.rootSignature == nullptr &&
        !CreateRootSignature(state.context.device, state.rootSignature.GetAddressOf())) {
        SchedulePipelineRetry(state, frameIndex, "root signature creation failed");
        return false;
    }
    const GFX::ShaderDeviceCapabilities deviceCaps = GFX::QueryShaderDeviceCapabilities(state.context.device);
    const ASSETS::SHADER::ShaderRuntimeDeviceCapabilities caps{deviceCaps.shaderModel, deviceCaps.rootSignatureVersion,
                                                               deviceCaps.featureMask};
    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = state.rootSignature.Get();
    if (state.compositePipeline == nullptr) {
        auto composite = ASSETS::SHADER::ResolveCameraDepthOfFieldShaderEntry("CompositeCameraDepthOfFieldCS", caps);
        if (!composite) {
            SchedulePipelineRetry(state, frameIndex, composite.diagnostic);
            state.pipelineReady = false;
            return false;
        }
        description.CS = {composite.lease.GetDxil().data(), composite.lease.GetDxil().size()};
        if (!HIKARI_DX_CHECK(state.context.device->CreateComputePipelineState(
                                 &description, IID_PPV_ARGS(state.compositePipeline.GetAddressOf())),
                             "CameraDepthOfField::CreateCompositePipeline")) {
            SchedulePipelineRetry(state, frameIndex, "composite pipeline creation failed");
            state.pipelineReady = false;
            return false;
        }
    }
    if (requireGather && state.gatherPipeline == nullptr) {
        auto gather = ASSETS::SHADER::ResolveCameraDepthOfFieldShaderEntry("GatherSpriteLayersCS", caps);
        if (gather) {
            description.CS = {gather.lease.GetDxil().data(), gather.lease.GetDxil().size()};
            state.gatherReady = HIKARI_DX_CHECK(state.context.device->CreateComputePipelineState(
                                                    &description, IID_PPV_ARGS(state.gatherPipeline.GetAddressOf())),
                                                "CameraDepthOfField::CreateGatherPipeline");
            if (!state.gatherReady) {
                SchedulePipelineRetry(state, frameIndex, "sprite gather pipeline creation failed");
            }
        } else {
            state.gatherReady = false;
            SchedulePipelineRetry(state, frameIndex, gather.diagnostic);
        }
    }
    state.pipelineReady = state.compositePipeline != nullptr;
    if (state.pipelineReady && state.gatherReady) {
        state.nextPipelineRetryFrame = 0u;
        state.pipelineRetryDelayFrames = 0u;
        state.pipelineFailureReported = false;
    }
    return true;
}

bool CreateTexture(StageState& state, Texture2DResource& texture, uint32_t width, uint32_t height, DXGI_FORMAT format,
                   bool renderTarget, bool unorderedAccess, bool shaderResource, uint32_t rtvIndex = 0u) {
    if (state.context.device == nullptr || width == 0u || height == 0u) {
        return false;
    }
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (renderTarget) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (unorderedAccess) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    const auto description = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1u, 1u, 1u, 0u, flags);
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = format;
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                             state.context.device, &heap, D3D12_HEAP_FLAG_NONE, &description,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, renderTarget ? &clearValue : nullptr,
                             GFX::VIDEO_MEMORY::Owner::Post, IID_PPV_ARGS(texture.resource.GetAddressOf())),
                         "CameraDepthOfField::CreateTexture")) {
        return false;
    }
    texture.width = width;
    texture.height = height;
    texture.format = format;
    texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (shaderResource) {
        texture.srv = AllocateTexture2DSrvDescriptor(texture.resource.Get(), format, 0u, 1u);
    }
    if (unorderedAccess) {
        texture.uav = AllocateTexture2DUavDescriptor(texture.resource.Get(), format);
    }
    if (renderTarget) {
        if (state.layerRtvHeap == nullptr || state.rtvDescriptorSize == 0u) {
            RetireTexture(state, texture, "CameraDepthOfField.IncompleteTexture");
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = state.layerRtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += SIZE_T(rtvIndex) * state.rtvDescriptorSize;
        state.context.device->CreateRenderTargetView(texture.resource.Get(), nullptr, rtv);
        texture.rtv = rtv;
    }
    if ((shaderResource && !texture.srv.IsValid()) || (unorderedAccess && !texture.uav.IsValid())) {
        RetireTexture(state, texture, "CameraDepthOfField.IncompleteTexture");
        return false;
    }
    return true;
}

bool EnsureLayerRtvHeap(StageState& state) {
    if (state.layerRtvHeap != nullptr && state.rtvDescriptorSize != 0u) {
        return true;
    }
    if (state.layerRtvHeap != nullptr) {
        GFX::RetireD3D12ObjectForFrame(state.layerRtvHeap, state.context, "CameraDepthOfField.IncompleteLayerRTVHeap");
    }
    D3D12_DESCRIPTOR_HEAP_DESC description{};
    description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    description.NumDescriptors = kLayerTargetCount;
    description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!HIKARI_DX_CHECK(
            state.context.device->CreateDescriptorHeap(&description, IID_PPV_ARGS(state.layerRtvHeap.GetAddressOf())),
            "CameraDepthOfField::CreateLayerRTVHeap")) {
        return false;
    }
    state.rtvDescriptorSize = state.context.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return state.rtvDescriptorSize != 0u;
}

bool EnsureOutput(StageState& state, uint32_t width, uint32_t height) {
    if (state.output.IsValid() && state.output.width == width && state.output.height == height &&
        state.output.uav.IsValid()) {
        return true;
    }
    RetireTexture(state, state.output, "CameraDepthOfField.Output");
    return CreateTexture(state, state.output, width, height, kLayerColorFormat, false, true, false);
}

bool EnsureSpriteLayers(StageState& state, uint32_t width, uint32_t height, bool blur) {
    const uint32_t halfWidth = blur ? (width + 1u) / 2u : 0u;
    const uint32_t halfHeight = blur ? (height + 1u) / 2u : 0u;
    bool current = state.layerWidth == width && state.layerHeight == height && state.halfWidth == halfWidth &&
                   state.halfHeight == halfHeight && state.layers[2].IsValid() && state.layers[5].IsValid();
    if (blur) {
        for (const auto& texture : state.layers)
            current = current && texture.IsValid();
        for (const auto& texture : state.spriteBlur)
            current = current && texture.IsValid();
    }
    if (current) {
        return true;
    }
    ReleaseLayerTargets(state);
    if (!EnsureLayerRtvHeap(state)) {
        return false;
    }
    const std::array<DXGI_FORMAT, kLayerTargetCount> formats{kLayerColorFormat,    kLayerColorFormat,
                                                             kLayerColorFormat,    kLayerMetadataFormat,
                                                             kLayerMetadataFormat, kLayerDepthFormat};
    for (uint32_t index = 0u; index < kLayerTargetCount; ++index) {
        if (!blur && index != 2u && index != 5u) {
            // ぼけなしの霧合成ではsharp color/depthだけ保持し、未使用MRTはnullへ書き捨てる。
            D3D12_RENDER_TARGET_VIEW_DESC description{};
            description.Format = formats[index];
            description.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            auto rtv = state.layerRtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtv.ptr += SIZE_T(index) * state.rtvDescriptorSize;
            state.context.device->CreateRenderTargetView(nullptr, &description, rtv);
            state.layers[index].rtv = rtv;
            continue;
        }
        if (!CreateTexture(state, state.layers[index], width, height, formats[index], true, false, true, index)) {
            ReleaseLayerTargets(state);
            return false;
        }
    }
    const std::array<DXGI_FORMAT, 4u> blurFormats{kLayerColorFormat, kLayerColorFormat, kLayerMetadataFormat,
                                                  kLayerMetadataFormat};
    for (uint32_t index = 0u; blur && index < state.spriteBlur.size(); ++index) {
        if (!CreateTexture(state, state.spriteBlur[index], halfWidth, halfHeight, blurFormats[index], false, true,
                           false)) {
            ReleaseLayerTargets(state);
            return false;
        }
    }
    state.halfWidth = halfWidth;
    state.halfHeight = halfHeight;
    state.layerWidth = width;
    state.layerHeight = height;
    return true;
}

bool EnsureFallbackFog(StageState& state) {
    if (state.fallbackFogVolume == nullptr) {
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        description.Width = 1u;
        description.Height = 1u;
        description.DepthOrArraySize = 1u;
        description.MipLevels = 1u;
        description.Format = kLayerColorFormat;
        description.SampleDesc.Count = 1u;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                                 state.context.device, &heap, D3D12_HEAP_FLAG_NONE, &description,
                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, GFX::VIDEO_MEMORY::Owner::Post,
                                 IID_PPV_ARGS(state.fallbackFogVolume.GetAddressOf())),
                             "CameraDepthOfField::CreateFallbackFogVolume")) {
            return false;
        }
    }
    if (!state.fallbackFogSrv.IsValid()) {
        state.fallbackFogSrv = AllocateTexture3DSrvDescriptor(state.fallbackFogVolume.Get(), kLayerColorFormat, 0u, 1u);
    }
    return state.fallbackFogSrv.IsValid();
}

bool IsTexture2DResourceCompatible(const RenderTextureView& view, DXGI_FORMAT resourceFormat) noexcept {
    if (view.resource == nullptr || view.width == 0u || view.height == 0u || view.format == DXGI_FORMAT_UNKNOWN) {
        return false;
    }
    const D3D12_RESOURCE_DESC description = view.resource->GetDesc();
    return description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && description.Width == view.width &&
           description.Height == view.height && description.MipLevels > 0u && description.SampleDesc.Count == 1u &&
           description.Format == resourceFormat;
}

bool EnsureSceneViews(StageState& state, const RenderTextureView& color, const RenderTextureView& depth) {
    if (!IsTexture2DResourceCompatible(color, color.format) ||
        !IsTexture2DResourceCompatible(depth, DXGI_FORMAT_R32_TYPELESS) || depth.format != DXGI_FORMAT_R32_FLOAT) {
        return false;
    }
    if (state.sceneColorResource != color.resource || !state.sceneColorSrv.IsValid()) {
        RetireView(state, state.sceneColorSrv, "CameraDepthOfField.SceneColorSRV");
        state.sceneColorResource = color.resource;
        state.sceneColorSrv = AllocateTexture2DSrvDescriptor(color.resource, color.format, 0u, 1u);
    }
    if (state.sceneDepthResource != depth.resource || !state.sceneDepthSrv.IsValid()) {
        RetireView(state, state.sceneDepthSrv, "CameraDepthOfField.SceneDepthSRV");
        state.sceneDepthResource = depth.resource;
        state.sceneDepthSrv = AllocateTexture2DSrvDescriptor(depth.resource, depth.format, 0u, 1u);
    }
    return state.sceneColorSrv.IsValid() && state.sceneDepthSrv.IsValid();
}

void TransitionTexture(StageState& state, Texture2DResource& texture, D3D12_RESOURCE_STATES nextState) {
    if (!texture.IsValid() || texture.state == nextState || state.context.cmdList == nullptr) {
        return;
    }
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), texture.state, nextState);
    state.context.cmdList->ResourceBarrier(1u, &barrier);
    texture.state = nextState;
}

void UavBarrier(StageState& state, Texture2DResource& texture) {
    if (texture.IsValid() && state.context.cmdList != nullptr) {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(texture.resource.Get());
        state.context.cmdList->ResourceBarrier(1u, &barrier);
    }
}

void SetShaderResources(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sceneColor,
                        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth, const StageState& state, bool hasLayers,
                        D3D12_GPU_DESCRIPTOR_HANDLE fog) {
    commandList->SetComputeRootDescriptorTable(1u, sceneColor);
    commandList->SetComputeRootDescriptorTable(2u, sceneDepth);
    if (hasLayers) {
        for (uint32_t index = 0u; index < 6u; ++index) {
            const auto srv = state.layers[index].srv.IsValid() ? state.layers[index].srv.gpu
                             : index < 5u                      ? sceneColor
                                                               : sceneDepth;
            commandList->SetComputeRootDescriptorTable(3u + index, srv);
        }
    } else {
        for (uint32_t index = 0u; index < 5u; ++index) {
            commandList->SetComputeRootDescriptorTable(3u + index, sceneColor);
        }
        commandList->SetComputeRootDescriptorTable(8u, sceneDepth);
    }
    commandList->SetComputeRootDescriptorTable(9u, fog);
}

void SetUnorderedAccess(ID3D12GraphicsCommandList* commandList, const StageState& state,
                        D3D12_GPU_DESCRIPTOR_HANDLE fallback, bool hasLayers) {
    for (uint32_t index = 0u; index < 4u; ++index) {
        const D3D12_GPU_DESCRIPTOR_HANDLE handle = hasLayers ? state.spriteBlur[index].uav.gpu : fallback;
        commandList->SetComputeRootDescriptorTable(10u + index, handle);
    }
    commandList->SetComputeRootDescriptorTable(14u, state.output.uav.gpu);
}

} // namespace

bool IsCameraDepthOfFieldActive(const CAMERA::DepthOfFieldSettings& settings, DepthOfFieldQuality quality) noexcept {
    return settings.enabled && quality != DepthOfFieldQuality::Off &&
           (settings.nearBlurRadius1080pPixels > 0.0f || settings.farBlurRadius1080pPixels > 0.0f);
}

bool PrepareCameraDepthOfFieldFrame(const GFX::Context& context, uint64_t frameIndex,
                                    const RenderTextureView& sceneColor, const RenderTextureView& sceneDepth,
                                    const CAMERA::DepthOfFieldSettings& settings, DepthOfFieldQuality quality,
                                    bool captureWorldSprites, bool compositeSpriteFog) {
    StageState& state = State();
    if (state.deviceGeneration != 0u && state.deviceGeneration != context.deviceGeneration) {
        ReleaseAll(state);
    }
    if (context.device != nullptr && context.cmdList != nullptr) {
        state.context = context;
        state.deviceGeneration = context.deviceGeneration;
    }
    const bool blur = IsCameraDepthOfFieldActive(settings, quality);
    if ((!blur && !(captureWorldSprites && compositeSpriteFog)) || !context.device || !context.cmdList ||
        !context.srvHeap || sceneColor.resource == nullptr || sceneDepth.resource == nullptr ||
        sceneColor.width == 0u || sceneColor.height == 0u || sceneColor.width != sceneDepth.width ||
        sceneColor.height != sceneDepth.height) {
        state.prepared = false;
        state.preparedFrame = (std::numeric_limits<uint64_t>::max)();
        state.constantAddress = 0u;
        state.constantsCpuAddress = nullptr;
        ReleaseLayerTargets(state);
        RetireTexture(state, state.output, "CameraDepthOfField.DisabledOutput");
        return false;
    }
    UpdateRenderResourceDescriptorPoolContext(context);
    state.prepared = false;
    state.capturePrepared = false;
    state.captureInProgress = false;
    state.layersCaptured = false;
    const uint32_t width = sceneColor.width;
    const uint32_t height = sceneColor.height;
    if (!EnsurePipeline(state, frameIndex, captureWorldSprites && blur) || !EnsureOutput(state, width, height) ||
        !EnsureFallbackFog(state) || !EnsureSceneViews(state, sceneColor, sceneDepth)) {
        return false;
    }
    state.width = width;
    state.height = height;
    if (captureWorldSprites && (!blur || state.gatherReady) && EnsureSpriteLayers(state, width, height, blur)) {
        state.capturePrepared = true;
    } else {
        ReleaseLayerTargets(state);
    }
    state.capture = {};
    state.capture.settings = settings;
    if (!blur) {
        state.capture.settings.nearBlurRadius1080pPixels = 0.0f;
        state.capture.settings.farBlurRadius1080pPixels = 0.0f;
    }
    state.capture.width = width;
    state.capture.height = height;
    state.capture.frameSlot = context.frameIndex;
    state.capture.frameIndex = frameIndex;
    for (uint32_t index = 0u; index < kLayerTargetCount && state.capturePrepared; ++index) {
        state.capture.renderTargets[index] = state.layers[index].rtv;
    }
    DX::DynamicUploadBuffer& upload = state.uploads[context.frameIndex % GFX::kFrameResourceCount];
    if (!upload.IsInitialized() && !upload.Init(context.device, 4096u)) {
        ReleaseLayerTargets(state);
        state.capturePrepared = false;
        return false;
    }
    upload.ResetForFrame(context.currentFrameRetireFenceValue);
    const DX::UploadAllocation allocation = upload.Allocate(sizeof(CameraDepthOfFieldConstants));
    if (!allocation) {
        ReleaseLayerTargets(state);
        state.capturePrepared = false;
        return false;
    }
    std::memset(allocation.cpuAddress, 0, sizeof(CameraDepthOfFieldConstants));
    state.constantsCpuAddress = allocation.cpuAddress;
    state.constantAddress = allocation.gpuAddress;
    state.preparedFrame = frameIndex;
    state.prepared = true;
    return true;
}

const WorldSpriteLayerCapture* BeginWorldSpriteLayerCapture(uint64_t frameIndex) noexcept {
    StageState& state = State();
    if (!state.prepared || !state.capturePrepared || state.preparedFrame != frameIndex ||
        state.context.cmdList == nullptr) {
        return nullptr;
    }
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kLayerTargetCount> handles{};
    for (uint32_t index = 0u; index < kLayerTargetCount; ++index) {
        TransitionTexture(state, state.layers[index], D3D12_RESOURCE_STATE_RENDER_TARGET);
        handles[index] = state.layers[index].rtv;
        const float clear[4]{};
        if (state.layers[index].IsValid())
            state.context.cmdList->ClearRenderTargetView(handles[index], clear, 0u, nullptr);
    }
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(state.width), static_cast<float>(state.height),
                                  0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(state.width), static_cast<LONG>(state.height)};
    state.context.cmdList->RSSetViewports(1u, &viewport);
    state.context.cmdList->RSSetScissorRects(1u, &scissor);
    state.captureInProgress = true;
    return &state.capture;
}

void MarkWorldSpriteLayerCapture(uint64_t frameIndex) noexcept {
    StageState& state = State();
    if (state.captureInProgress && state.preparedFrame == frameIndex) {
        state.layersCaptured = true;
    }
}

void EndWorldSpriteLayerCapture(uint64_t frameIndex) noexcept {
    StageState& state = State();
    if (!state.captureInProgress || state.preparedFrame != frameIndex) {
        return;
    }
    for (Texture2DResource& texture : state.layers) {
        TransitionTexture(state, texture,
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    state.captureInProgress = false;
}

bool ExecuteCameraDepthOfField(const GFX::Context& context, uint64_t frameIndex, RenderTarget2D& sceneColor,
                               const RenderTextureView& sceneDepth, const TEMPORAL::TemporalFrameState& temporalFrame,
                               const Camera3D& camera, const CAMERA::DepthOfFieldSettings& settings,
                               DepthOfFieldQuality quality, const VOLUMETRIC::VolumetricFogSamplingView& fogView) {
    StageState& state = State();
    if (!state.prepared || state.preparedFrame != frameIndex || temporalFrame.frameIndex != frameIndex ||
        !state.constantsCpuAddress || state.constantAddress == 0u || context.device != state.context.device ||
        context.deviceGeneration != state.deviceGeneration || context.srvHeap != state.context.srvHeap ||
        context.cmdList == nullptr || context.srvHeap == nullptr || !state.fallbackFogSrv.IsValid() ||
        sceneColor.GetResource() == nullptr || sceneColor.GetResource() != state.sceneColorResource ||
        sceneDepth.resource != state.sceneDepthResource || sceneColor.GetWidth() != static_cast<int>(state.width) ||
        sceneColor.GetHeight() != static_cast<int>(state.height) || !state.pipelineReady || !state.output.IsValid() ||
        !state.output.uav.IsValid() || (sceneDepth.state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0u) {
        return false;
    }
    const bool hasLayers = state.capturePrepared && state.layersCaptured && !state.captureInProgress;
    const bool blur = IsCameraDepthOfFieldActive(settings, quality);
    const bool hasBlurLayers = hasLayers && state.halfWidth > 0u;
    if (hasBlurLayers) {
        for (Texture2DResource& texture : state.spriteBlur) {
            TransitionTexture(state, texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
    TransitionTexture(state, state.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const D3D12_RESOURCE_STATES scenePreviousState = sceneColor.GetColorState();
    sceneColor.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto cameraForward = MATH::Normalize(camera.GetTarget() - camera.GetPosition());
    CameraDepthOfFieldConstants constants{};
    constants.inverseViewProjection = temporalFrame.camera.invViewProj;
    constants.cameraPosition = {temporalFrame.camera.cameraPos.x, temporalFrame.camera.cameraPos.y,
                                temporalFrame.camera.cameraPos.z, 1.0f};
    constants.cameraForward = {cameraForward.x, cameraForward.y, cameraForward.z, 0.0f};
    constants.focusBand = {settings.focusDistance, settings.focusRange, settings.nearTransition,
                           settings.farTransition};
    const float resolutionScale = blur ? static_cast<float>(state.height) / 1080.0f : 0.0f;
    constants.blurAndRenderSize = {settings.nearBlurRadius1080pPixels * resolutionScale,
                                   settings.farBlurRadius1080pPixels * resolutionScale, static_cast<float>(state.width),
                                   static_cast<float>(state.height)};
    const bool fogValid = fogView.valid && fogView.frameIndex == frameIndex && fogView.integratedVolumeSrv.ptr != 0u &&
                          fogView.depthSlices > 0u;
    constants.fogDistancesAndVolumeSize = {fogView.nearDistance, fogView.farDistance,
                                           static_cast<float>(fogView.depthSlices), fogValid ? 1.0f : 0.0f};
    constants.frameAndQuality = {static_cast<float>(ResolveTapCount(quality)), camera.IsOrthographic() ? 1.0f : 0.0f,
                                 hasBlurLayers ? 2.0f
                                 : hasLayers   ? 1.0f
                                               : 0.0f,
                                 temporalFrame.camera.farZ};
    std::memcpy(state.constantsCpuAddress, &constants, sizeof(constants));

    const D3D12_GPU_DESCRIPTOR_HANDLE fogSrv = fogValid ? fogView.integratedVolumeSrv : state.fallbackFogSrv.gpu;
    if (fogSrv.ptr == 0u) {
        sceneColor.TransitionColor(scenePreviousState);
        return false;
    }
    ID3D12DescriptorHeap* heap = context.srvHeap;
    ID3D12GraphicsCommandList* commandList = context.cmdList;
    const D3D12_GPU_DESCRIPTOR_HANDLE fallbackUav = state.output.uav.gpu;
    commandList->SetDescriptorHeaps(1u, &heap);
    commandList->SetComputeRootSignature(state.rootSignature.Get());
    commandList->SetComputeRootConstantBufferView(0u, state.constantAddress);
    SetShaderResources(commandList, state.sceneColorSrv.gpu, state.sceneDepthSrv.gpu, state, hasLayers, fogSrv);
    SetUnorderedAccess(commandList, state, fallbackUav, hasBlurLayers);

    GFX::PIX::ScopedGpuEvent event(commandList, GFX::PIX::kColorPost,
                                   blur ? "CameraDepthOfField" : "WorldSpriteFogComposite");
    // 霧だけの合成はPIXとCommandFrameで計測する。閉じたTransparent timerは再利用しない。
    GFX::GPU_PROFILE::ScopedGpuTimer timer(blur ? commandList : nullptr, GFX::GPU_PROFILE::Pass::DepthOfField);
    if (hasBlurLayers) {
        commandList->SetPipelineState(state.gatherPipeline.Get());
        commandList->Dispatch((state.halfWidth + 7u) / 8u, (state.halfHeight + 7u) / 8u, 1u);
        for (Texture2DResource& texture : state.spriteBlur) {
            UavBarrier(state, texture);
        }
    }
    commandList->SetPipelineState(state.compositePipeline.Get());
    commandList->Dispatch((state.width + 7u) / 8u, (state.height + 7u) / 8u, 1u);
    UavBarrier(state, state.output);
    sceneColor.TransitionColor(D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionTexture(state, state.output, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->CopyResource(sceneColor.GetResource(), state.output.resource.Get());
    TransitionTexture(state, state.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    sceneColor.TransitionColor(scenePreviousState);
    state.prepared = false;
    state.capturePrepared = false;
    state.layersCaptured = false;
    state.constantAddress = 0u;
    state.constantsCpuAddress = nullptr;
    state.preparedFrame = (std::numeric_limits<uint64_t>::max)();
    return true;
}

void ShutdownCameraDepthOfFieldStage() noexcept {
    StageState& state = State();
    ReleaseAll(state);
    state.context = {};
    state.deviceGeneration = 0u;
}

} // namespace HIKARI::RENDER3D::DOF
