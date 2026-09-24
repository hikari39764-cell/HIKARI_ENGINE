#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Temporal/HIKARI_TaaResolvePass.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalShader.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalPassScratch.h"

namespace HIKARI::RENDER3D::TEMPORAL {

namespace {
using Microsoft::WRL::ComPtr;
struct TaaResolveConstants {
    MATH::Vec4 screenParams{};
    MATH::Vec4 taaParams{};
    MATH::Vec4 rejectionParams{};
    MATH::Vec4 jitterPixels{};
};

static_assert(sizeof(TaaResolveConstants) == 64u);

struct TaaResolvePassState {
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipelineState{};
    uint64_t ownerDeviceGeneration = 0u;
    ID3D12Device* ownerDevice = nullptr;
    bool ready = false;
};

TaaResolvePassState& State() {
    static TaaResolvePassState state{};
    return state;
}

bool EnsurePipeline(const GFX::Context& graphics) {
    TaaResolvePassState& state = State();
    ID3D12Device* device = graphics.device;
    if (device == nullptr || graphics.deviceGeneration == 0u) {
        return false;
    }
    if (state.ready) {
        return state.ownerDeviceGeneration == graphics.deviceGeneration && state.ownerDevice == device &&
               state.rootSignature != nullptr && state.pipelineState != nullptr;
    }
    if (state.ownerDeviceGeneration != 0u &&
        (state.ownerDeviceGeneration != graphics.deviceGeneration || state.ownerDevice != device)) {
        return false;
    }

    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipelineState{};

    D3D12_DESCRIPTOR_RANGE ranges[10]{};
    for (uint32_t i = 0; i < static_cast<uint32_t>(std::size(ranges)); ++i) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = i;
        ranges[i].RegisterSpace = 0;
        ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    D3D12_ROOT_PARAMETER params[11]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;

    for (uint32_t i = 0; i < 10u; ++i) {
        params[i + 1u].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i + 1u].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[i + 1u].DescriptorTable.NumDescriptorRanges = 1;
        params[i + 1u].DescriptorTable.pDescriptorRanges = &ranges[i];
    }

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(std::size(params));
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    HRESULT hr = GFX::CreateRootSignature(device, rsDesc, rootSignature.GetAddressOf());
    if (!HIKARI_DX_CHECK(hr, "TaaResolvePass::CreateRootSignature")) {
        return false;
    }
    rootSignature->SetName(L"HIKARI.Temporal.TaaResolveRootSignature");

    const auto vs = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::TaaResolve, "VSMain",
                                                 ASSETS::SHADER::ShaderStage::Vertex);
    const auto ps = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::TaaResolve, "PSMain",
                                                 ASSETS::SHADER::ShaderStage::Pixel);
    if (!vs || !ps) {
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = INTERNAL::TemporalShaderBytecode(vs);
    psoDesc.PS = INTERNAL::TemporalShaderBytecode(ps);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.InputLayout = {};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 4;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R32_FLOAT;
    psoDesc.RTVFormats[3] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "TaaResolvePass::CreatePSO")) {
        return false;
    }
    pipelineState->SetName(L"HIKARI.Temporal.TaaResolvePSO");

    state.rootSignature = std::move(rootSignature);
    state.pipelineState = std::move(pipelineState);
    state.ownerDeviceGeneration = graphics.deviceGeneration;
    state.ownerDevice = device;
    state.ready = true;
    return true;
}
} // namespace

RenderTarget2D* ExecuteTaaResolvePass(const GFX::Context& graphics, const TemporalInputs& inputs,
                                      const TaaResolveSettings& settings) {
    TaaResolvePassState& state = State();
    ID3D12GraphicsCommandList* cmd = graphics.cmdList;
    ID3D12Device* device = graphics.device;
    ID3D12DescriptorHeap* srvHeap = graphics.srvHeap;
    RenderTarget2D* historyTarget = GetHistoryColorWriteRenderTarget(inputs.resourceFrame);
    RenderTarget2D* historyDepthTarget = GetHistoryDepthWriteRenderTarget(inputs.resourceFrame);
    RenderTarget2D* outputTarget = GetTaaResolvedColorRenderTarget(inputs.resourceFrame);
    RenderTarget2D* debugTarget = GetTemporalDebugRenderTarget(inputs.resourceFrame);

    (void)MarkTemporalAntiAliasing(inputs.resourceFrame, settings.enabled, false);
    if (!settings.enabled || !inputs.frame.temporalResolveAllowed || cmd == nullptr || device == nullptr ||
        srvHeap == nullptr || !inputs.MatchesResourceFrame() ||
        graphics.deviceGeneration != inputs.resourceFrame.deviceGeneration || historyTarget == nullptr ||
        historyDepthTarget == nullptr || outputTarget == nullptr || debugTarget == nullptr ||
        !inputs.sceneColor.valid || !inputs.motionVectors.valid || !inputs.motionMetadata.valid ||
        !inputs.sceneDepth.IsSampleable() || !inputs.historyColorWrite.valid || !inputs.historyDepthWrite.valid ||
        !inputs.reactiveMask.valid || !inputs.transparencyMask.valid || !inputs.invalidDepthMotionMask.valid ||
        !inputs.exposure.valid || !inputs.debugOutput.valid) {
        return nullptr;
    }
    if (!EnsurePipeline(graphics)) {
        return nullptr;
    }

    DETAIL::TemporalPassUploadSlice constantUpload{};
    if (!DETAIL::TryAcquireTemporalConstantUpload(graphics, inputs.resourceFrame,
                                                  DETAIL::TemporalConstantUploadKind::TaaResolve,
                                                  GFX::AlignD3D12ConstantBufferByteSize(sizeof(TaaResolveConstants)),
                                                  L"HIKARI.Temporal.TaaResolveCB", constantUpload)) {
        return nullptr;
    }

    const bool historyValid =
        inputs.frame.historyValid && inputs.historyColorRead.valid && inputs.historyDepthRead.valid;
    TaaResolveConstants constants{};
    constants.screenParams = inputs.frame.camera.screenParams;
    constants.jitterPixels = {inputs.frame.camera.jitter.x, inputs.frame.camera.jitter.y,
                              inputs.frame.camera.prevJitter.x, inputs.frame.camera.prevJitter.y};
    constants.taaParams = {historyValid ? 1.0f : 0.0f, std::clamp(settings.historyWeight, 0.0f, 0.97f),
                           inputs.exposure.valid ? 1.0f : 0.0f, (std::max)(0.0f, settings.varianceClipGamma)};
    constants.rejectionParams = {
        std::clamp(settings.depthRejection, 0.0001f, 0.05f), std::clamp(settings.luminanceRejection, 0.05f, 4.0f),
        std::clamp(settings.sharpness, 0.0f, 1.0f), static_cast<float>(GetTemporalDebugView(inputs.resourceFrame))};
    std::memcpy(constantUpload.mapped, &constants, sizeof(constants));

    D3D12_GPU_DESCRIPTOR_HANDLE historySrv = historyValid ? inputs.historyColorRead.srv : inputs.sceneColor.srv;
    D3D12_GPU_DESCRIPTOR_HANDLE historyDepthSrv = historyValid ? inputs.historyDepthRead.srv : inputs.sceneDepth.srv;

    GFX::PIX::ScopedGpuEvent pix(cmd, GFX::PIX::kColorPost, "Temporal.TAA");
    GFX::GPU_PROFILE::ScopedGpuTimer gpuTimer(cmd, GFX::GPU_PROFILE::Pass::TemporalTaaResolve);

    historyTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    historyDepthTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    outputTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    debugTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[4] = {outputTarget->GetRtvHandle(), historyTarget->GetRtvHandle(),
                                           historyDepthTarget->GetRtvHandle(), debugTarget->GetRtvHandle()};
    cmd->OMSetRenderTargets(4, rtvs, FALSE, nullptr);
    const uint32_t viewportWidth = static_cast<uint32_t>((std::max)(1, historyTarget->GetWidth()));
    const uint32_t viewportHeight = static_cast<uint32_t>((std::max)(1, historyTarget->GetHeight()));
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight),
                                  0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(viewportWidth), static_cast<LONG>(viewportHeight)};
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    const float colorClear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float depthClear[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmd->ClearRenderTargetView(rtvs[0], colorClear, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[1], colorClear, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[2], depthClear, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[3], colorClear, 0, nullptr);

    cmd->SetDescriptorHeaps(1, &srvHeap);
    cmd->SetGraphicsRootSignature(state.rootSignature.Get());
    cmd->SetPipelineState(state.pipelineState.Get());
    cmd->SetGraphicsRootConstantBufferView(0, constantUpload.gpuAddress);
    cmd->SetGraphicsRootDescriptorTable(1, inputs.sceneColor.srv);
    cmd->SetGraphicsRootDescriptorTable(2, historySrv);
    cmd->SetGraphicsRootDescriptorTable(3, inputs.motionVectors.srv);
    cmd->SetGraphicsRootDescriptorTable(4, inputs.motionMetadata.srv);
    cmd->SetGraphicsRootDescriptorTable(5, inputs.sceneDepth.srv);
    cmd->SetGraphicsRootDescriptorTable(6, historyDepthSrv);
    cmd->SetGraphicsRootDescriptorTable(7, inputs.reactiveMask.srv);
    cmd->SetGraphicsRootDescriptorTable(8, inputs.transparencyMask.srv);
    cmd->SetGraphicsRootDescriptorTable(9, inputs.invalidDepthMotionMask.srv);
    cmd->SetGraphicsRootDescriptorTable(10, inputs.exposure.srv);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    historyTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    historyDepthTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    outputTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    debugTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    if (!MarkTemporalHistoryOutputWritten(inputs.resourceFrame, true, true)) {
        return nullptr;
    }
    if (!CommitTemporalHistory(inputs.resourceFrame, true)) {
        return nullptr;
    }
    (void)MarkTemporalAntiAliasing(inputs.resourceFrame, true, true);
    return outputTarget;
}

void ShutdownTaaResolvePass() {
    TaaResolvePassState& state = State();
    state.pipelineState.Reset();
    state.rootSignature.Reset();
    state.ownerDeviceGeneration = 0u;
    state.ownerDevice = nullptr;
    state.ready = false;
}

} // namespace HIKARI::RENDER3D::TEMPORAL
