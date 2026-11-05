#include "Render3D/Vfx/Fluid/HIKARI_FluidRender.h"

#include <memory>
#include <string>
#include <utility>

#include <d3dx12.h>

#include "RenderContracts/Shader/HIKARI_FluidShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/HIKARI_VideoMemory.h"

// 流体の描画の資源：根と PSO、視図ごとの半分辨率の描画先と履歴と相機体積、細部のノイズ、空の体積。
namespace HIKARI::RENDER3D::FLUID::INTERNAL {
using Microsoft::WRL::ComPtr;

bool FluidRenderer::EnsurePipelines(ID3D12Device* device, DXGI_FORMAT sceneColorFormat) {
    if (device_ != device) {
        root_.Reset();
        noise_.Reset();
        light_.Reset();
        cameraVolume_.Reset();
        march_.Reset();
        reconstruct_.Reset();
        composite_.Reset();
        compositeFormat_ = DXGI_FORMAT_UNKNOWN;
        failed_ = false;
        device_ = device;
    }
    if (failed_)
        return false;
    const GFX::ShaderDeviceCapabilities deviceCaps = GFX::QueryShaderDeviceCapabilities(device);
    const ASSETS::SHADER::ShaderRuntimeDeviceCapabilities caps{deviceCaps.shaderModel, deviceCaps.rootSignatureVersion,
                                                               deviceCaps.featureMask};
    using ASSETS::SHADER::ShaderStage;
    const auto resolve = [&](const char* entry, ShaderStage stage, ASSETS::SHADER::ShaderAssetResolveResult& out) {
        out = ASSETS::SHADER::ResolveFluidVolumeShaderEntry(entry, stage, caps);
        if (!out)
            HIKARI_LOG_WARN(std::string("[FluidSmoke] Volume shader unavailable: ") + entry + ": " + out.diagnostic);
        return static_cast<bool>(out);
    };
    if (!root_) {
        failed_ = true;
        std::array<CD3DX12_DESCRIPTOR_RANGE, kInputCount + 1u> ranges{};
        std::array<CD3DX12_ROOT_PARAMETER, kRootCount> parameters{};
        parameters[kConstants].InitAsConstantBufferView(0);
        parameters[kPassConstants].InitAsConstants(4, 1);
        for (UINT index = 0; index < kInputCount; ++index) {
            ranges[index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, index);
            parameters[kFirstInput + index].InitAsDescriptorTable(1, &ranges[index]);
        }
        ranges[kInputCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        parameters[kOutput].InitAsDescriptorTable(1, &ranges[kInputCount]);
        std::array<CD3DX12_STATIC_SAMPLER_DESC, 5> samplers{};
        samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                         D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        samplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                         D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.0f, 16,
                         D3D12_COMPARISON_FUNC_ALWAYS, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK);
        samplers[2].Init(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                         D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
        samplers[3].Init(3, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                         D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        samplers[4].Init(4, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                         D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.0f, 16,
                         D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);
        const CD3DX12_ROOT_SIGNATURE_DESC description(kRootCount, parameters.data(), static_cast<UINT>(samplers.size()),
                                                      samplers.data());
        if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(device, description, root_.ReleaseAndGetAddressOf()),
                             "FluidRenderer::CreateRoot"))
            return false;
        const std::array<std::pair<const char*, ComPtr<ID3D12PipelineState>*>, 3> computes{
            {{"NoiseCS", std::addressof(noise_)},
             {"LightCS", std::addressof(light_)},
             {"CameraVolumeCS", std::addressof(cameraVolume_)}}};
        for (const auto& [entry, pipeline] : computes) {
            ASSETS::SHADER::ShaderAssetResolveResult shader{};
            if (!resolve(entry, ShaderStage::Compute, shader))
                return false;
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = root_.Get();
            desc.CS = {shader.lease.GetDxil().data(), shader.lease.GetDxil().size()};
            if (!HIKARI_DX_CHECK(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(pipeline->GetAddressOf())),
                                 "FluidRenderer::CreateComputePipeline"))
                return false;
        }
        failed_ = false;
    }
    if (march_ && composite_ && compositeFormat_ == sceneColorFormat)
        return true;
    failed_ = true;
    ASSETS::SHADER::ShaderAssetResolveResult vertex{}, marchShader{}, reconstructShader{}, compositeShader{};
    if (!resolve("FullscreenVS", ShaderStage::Vertex, vertex) || !resolve("MarchPS", ShaderStage::Pixel, marchShader) ||
        !resolve("ReconstructPS", ShaderStage::Pixel, reconstructShader) ||
        !resolve("CompositePS", ShaderStage::Pixel, compositeShader))
        return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root_.Get();
    desc.VS = {vertex.lease.GetDxil().data(), vertex.lease.GetDxil().size()};
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.SampleMask = UINT_MAX;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1u;
    desc.PS = {marchShader.lease.GetDxil().data(), marchShader.lease.GetDxil().size()};
    desc.NumRenderTargets = 2u;
    desc.RTVFormats[0] = kColorFormat;
    desc.RTVFormats[1] = kDepthFormat;
    if (!HIKARI_DX_CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(march_.ReleaseAndGetAddressOf())),
                         "FluidRenderer::CreateMarchPipeline"))
        return false;
    desc.PS = {reconstructShader.lease.GetDxil().data(), reconstructShader.lease.GetDxil().size()};
    desc.NumRenderTargets = 1u;
    desc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
    if (!HIKARI_DX_CHECK(
            device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(reconstruct_.ReleaseAndGetAddressOf())),
            "FluidRenderer::CreateReconstructPipeline"))
        return false;
    // 合成は乗算済みの流体を場面の上へ重ねる（ONE / INV_SRC_ALPHA）。深度は読み取り専用で束ねたまま。
    desc.PS = {compositeShader.lease.GetDxil().data(), compositeShader.lease.GetDxil().size()};
    desc.RTVFormats[0] = sceneColorFormat;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (!HIKARI_DX_CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(composite_.ReleaseAndGetAddressOf())),
                         "FluidRenderer::CreateCompositePipeline"))
        return false;
    compositeFormat_ = sceneColorFormat;
    failed_ = false;
    return true;
}

bool FluidRenderer::EnsureView(const GFX::Context& context, ViewState& state, uint32_t width, uint32_t height) {
    const uint32_t halfWidth = (width + 1u) / 2u, halfHeight = (height + 1u) / 2u;
    if (state.width == width && state.height == height && state.marchColor.resource)
        return true;
    RetireView(context, state);
    ID3D12Device* device = context.device;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = 4u;
    if (!HIKARI_DX_CHECK(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(state.rtvHeap.GetAddressOf())),
                         "FluidRenderer::CreateRtvHeap"))
        return false;
    const UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const auto create = [&](Target& target, DXGI_FORMAT format, uint32_t index) {
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(format, halfWidth, halfHeight, 1u, 1u, 1u, 0u,
                                                       D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                                        kRead, nullptr, GFX::VIDEO_MEMORY::Owner::Vfx,
                                                                        IID_PPV_ARGS(target.resource.GetAddressOf())),
                             "FluidRenderer::CreateTarget"))
            return false;
        target.srv = AllocateTexture2DSrvDescriptor(target.resource.Get(), format, 0u, 1u);
        target.rtv = {state.rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + index * stride};
        device->CreateRenderTargetView(target.resource.Get(), nullptr, target.rtv);
        target.state = kRead;
        return target.srv.IsValid();
    };
    const bool created =
        create(state.marchColor, kColorFormat, 0u) && create(state.marchDepth, kDepthFormat, 1u) &&
        create(state.history[0], kColorFormat, 2u) && create(state.history[1], kColorFormat, 3u) &&
        CreateFluidVolumeTexture(device, kColorFormat, (width + kCameraVolumeTile - 1u) / kCameraVolumeTile,
                                 (height + kCameraVolumeTile - 1u) / kCameraVolumeTile, kCameraVolumeSlices,
                                 state.cameraVolume);
    if (!created) {
        RetireView(context, state);
        return false;
    }
    state.width = width;
    state.height = height;
    state.historyValid = false;
    return true;
}

void FluidRenderer::RetireView(const GFX::Context& context, ViewState& state) {
    for (Target* target : {&state.marchColor, &state.marchDepth, &state.history[0], &state.history[1]}) {
        RetireRenderResourceDescriptor(target->srv, context, "FluidRenderer.TargetSrv");
        if (target->resource)
            GFX::RetireD3D12ObjectForFrame(target->resource, context, "FluidRenderer.Target");
        *target = {};
    }
    if (state.rtvHeap)
        GFX::RetireD3D12ObjectForFrame(state.rtvHeap, context, "FluidRenderer.RtvHeap");
    RetireVolume(context, state.cameraVolume);
    state.width = state.height = 0u;
    state.historyValid = false;
}

void FluidRenderer::TransitionTarget(ID3D12GraphicsCommandList* commands, Target& target, D3D12_RESOURCE_STATES state) {
    if (target.state == state)
        return;
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(target.resource.Get(), target.state, state);
    commands->ResourceBarrier(1, &barrier);
    target.state = state;
}

void FluidRenderer::TransitionVolume(ID3D12GraphicsCommandList* commands, FluidVolume& volume,
                                     D3D12_RESOURCE_STATES state) {
    if (volume.state == state)
        return;
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(volume.resource.Get(), volume.state, state);
    commands->ResourceBarrier(1, &barrier);
    volume.state = state;
}

bool FluidRenderer::EnsureNoise(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS constants) {
    if (noiseReady_)
        return true;
    if (!noiseVolume_.resource && !CreateFluidVolumeTexture(context.device, DXGI_FORMAT_R8_UNORM, kNoiseSize,
                                                            kNoiseSize, kNoiseSize, noiseVolume_))
        return false;
    ID3D12GraphicsCommandList* commands = context.cmdList;
    TransitionVolume(commands, noiseVolume_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->SetComputeRootSignature(root_.Get());
    commands->SetPipelineState(noise_.Get());
    commands->SetComputeRootConstantBufferView(kConstants, constants);
    commands->SetComputeRootDescriptorTable(kOutput, noiseVolume_.uav.gpu);
    commands->Dispatch(kNoiseSize / 4u, kNoiseSize / 4u, kNoiseSize / 4u);
    TransitionVolume(commands, noiseVolume_, kRead);
    noiseReady_ = true;
    return true;
}

bool FluidRenderer::BeginFrame(const GFX::Context& context) {
    // 透明物体は流体の無いフレームでも相機体積を束ねるので、1 texel の空の体積を常に用意する。
    if (!fallbackVolume_.resource) {
        if (!CreateFluidVolumeTexture(context.device, kColorFormat, 1u, 1u, 1u, fallbackVolume_))
            return false;
        TransitionVolume(context.cmdList, fallbackVolume_, kRead);
    }
    cameraVolumeView_ = {fallbackVolume_.srv.gpu, {}};
    // 長く描かれていない視図（閉じた編集器の視口など）の目標と相機体積は捨てる（描かれれば作り直す）。
    constexpr uint64_t kStaleViewFrames = 120u;
    for (auto it = views_.begin(); it != views_.end();) {
        const uint64_t last = it->second.lastFrame;
        if (last != UINT64_MAX && last + kStaleViewFrames >= context.currentFrameRetireFenceValue) {
            ++it;
            continue;
        }
        RetireView(context, it->second);
        it = views_.erase(it);
    }
    return true;
}

FluidCameraVolumeView FluidRenderer::GetCameraVolume() const noexcept { return cameraVolumeView_; }

void FluidRenderer::Shutdown(const GFX::Context& context) {
    for (auto& [key, state] : views_)
        RetireView(context, state);
    views_.clear();
    RetireVolume(context, noiseVolume_);
    RetireVolume(context, fallbackVolume_);
    noiseReady_ = false;
    for (ComPtr<ID3D12PipelineState>* pipeline :
         {std::addressof(noise_), std::addressof(light_), std::addressof(cameraVolume_), std::addressof(march_),
          std::addressof(reconstruct_), std::addressof(composite_)})
        if (*pipeline)
            GFX::RetireD3D12ObjectForFrame(*pipeline, context, "FluidRenderer.Pipeline");
    if (root_)
        GFX::RetireD3D12ObjectForFrame(root_, context, "FluidRenderer.Root");
    for (auto& upload : uploads_)
        upload.Finalize(&context);
    device_ = nullptr;
    compositeFormat_ = DXGI_FORMAT_UNKNOWN;
    cameraVolumeView_ = {};
}

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
