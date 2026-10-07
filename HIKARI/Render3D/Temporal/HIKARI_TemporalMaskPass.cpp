#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Temporal/HIKARI_TemporalMaskPass.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <utility>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalShader.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalResourceSystemInternal.h"

namespace HIKARI::RENDER3D::TEMPORAL {

namespace {
using Microsoft::WRL::ComPtr;

struct TemporalMaskPassState {
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipelineState{};
    uint64_t ownerDeviceGeneration = 0u;
    ID3D12Device* ownerDevice = nullptr;
    bool ready = false;
};

TemporalMaskPassState& State() {
    static TemporalMaskPassState state{};
    return state;
}

bool EnsurePipeline(const GFX::Context& graphics) {
    TemporalMaskPassState& state = State();
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

    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    for (uint32_t index = 0; index < static_cast<uint32_t>(std::size(ranges)); ++index) {
        ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[index].NumDescriptors = 1;
        ranges[index].BaseShaderRegister = index;
        ranges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    D3D12_ROOT_PARAMETER params[2]{};
    for (uint32_t index = 0; index < static_cast<uint32_t>(std::size(params)); ++index) {
        params[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[index].DescriptorTable.NumDescriptorRanges = 1;
        params[index].DescriptorTable.pDescriptorRanges = &ranges[index];
    }

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(std::size(params));
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;

    HRESULT hr = GFX::CreateRootSignature(device, rootDesc, rootSignature.GetAddressOf());
    if (!HIKARI_DX_CHECK(hr, "TemporalMaskPass::CreateRootSignature")) {
        return false;
    }
    rootSignature->SetName(L"HIKARI.Temporal.MaskRootSignature");

    const auto vs = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::Masks, "VSMain",
                                                 ASSETS::SHADER::ShaderStage::Vertex);
    const auto ps = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::Masks, "PSMain",
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
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 3;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R8_UNORM;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "TemporalMaskPass::CreatePSO")) {
        return false;
    }
    pipelineState->SetName(L"HIKARI.Temporal.MaskPSO");
    state.rootSignature = std::move(rootSignature);
    state.pipelineState = std::move(pipelineState);
    state.ownerDeviceGeneration = graphics.deviceGeneration;
    state.ownerDevice = device;
    state.ready = true;
    return true;
}
} // namespace

bool ExecuteTemporalMaskPass(const GFX::Context& graphics, const TemporalInputs& inputs) {
    ID3D12GraphicsCommandList* cmd = graphics.cmdList;
    ID3D12Device* device = graphics.device;
    ID3D12DescriptorHeap* srvHeap = graphics.srvHeap;
    RenderTarget2D* reactive = GetReactiveMaskRenderTarget(inputs.resourceFrame);
    RenderTarget2D* transparency = GetTransparencyMaskRenderTarget(inputs.resourceFrame);
    RenderTarget2D* invalidDepthMotion = GetInvalidDepthMotionMaskRenderTarget(inputs.resourceFrame);
    if (cmd == nullptr || device == nullptr || !inputs.MatchesResourceFrame() ||
        graphics.deviceGeneration != inputs.resourceFrame.deviceGeneration || reactive == nullptr ||
        transparency == nullptr || invalidDepthMotion == nullptr) {
        (void)MarkTemporalMasksWritten(inputs.resourceFrame, false, false);
        return false;
    }

    reactive->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    transparency->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    invalidDepthMotion->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] = {reactive->GetRtvHandle(), transparency->GetRtvHandle(),
                                                 invalidDepthMotion->GetRtvHandle()};
    cmd->OMSetRenderTargets(3, rtvs, FALSE, nullptr);
    const float clear[4] = {};
    cmd->ClearRenderTargetView(rtvs[0], clear, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[1], clear, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[2], clear, 0, nullptr);

    const D3D12_GPU_DESCRIPTOR_HANDLE compositionBase = GetTemporalCompositionBase(inputs.resourceFrame);
    const bool canGenerate =
        srvHeap != nullptr && inputs.sceneColor.valid && compositionBase.ptr != 0 && EnsurePipeline(graphics);
    if (canGenerate) {
        GFX::PIX::ScopedGpuEvent pix(cmd, GFX::PIX::kColorPost, "Temporal.Masks");
        const D3D12_VIEWPORT viewport{0.0f,
                                      0.0f,
                                      static_cast<float>((std::max)(1, reactive->GetWidth())),
                                      static_cast<float>((std::max)(1, reactive->GetHeight())),
                                      0.0f,
                                      1.0f};
        const D3D12_RECT scissor{0, 0, (std::max)(1, reactive->GetWidth()), (std::max)(1, reactive->GetHeight())};
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
        cmd->SetDescriptorHeaps(1, &srvHeap);
        cmd->SetGraphicsRootSignature(State().rootSignature.Get());
        cmd->SetPipelineState(State().pipelineState.Get());
        cmd->SetGraphicsRootDescriptorTable(0, compositionBase);
        cmd->SetGraphicsRootDescriptorTable(1, inputs.sceneColor.srv);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    // TAA の metadata と同じ保守的な失効範囲を、外部 upscaler にも公開する。
    const auto* temporalState = DETAIL::FindWritableState(inputs.resourceFrame);
    if (temporalState != nullptr &&
        temporalState->passScratch.geometryHistoryRejectionFrame.Matches(inputs.resourceFrame)) {
        const auto& rects = temporalState->passScratch.geometryHistoryRejectionRects;
        if (!rects.empty()) {
            const float rejected[4]{1.0f, 1.0f, 1.0f, 1.0f};
            cmd->ClearRenderTargetView(rtvs[0], rejected, static_cast<UINT>(rects.size()), rects.data());
            cmd->ClearRenderTargetView(rtvs[2], rejected, static_cast<UINT>(rects.size()), rects.data());
        }
    }
    reactive->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transparency->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    invalidDepthMotion->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // Cleared zero masks are a valid opaque-only hint set. When a
    // post-opaque composition base exists, the draw refines them from the
    // actual color difference instead.
    (void)MarkTemporalMasksWritten(inputs.resourceFrame, true, canGenerate);
    return true;
}

void ShutdownTemporalMaskPass() {
    TemporalMaskPassState& state = State();
    state.pipelineState.Reset();
    state.rootSignature.Reset();
    state.ownerDeviceGeneration = 0u;
    state.ownerDevice = nullptr;
    state.ready = false;
}

} // namespace HIKARI::RENDER3D::TEMPORAL
