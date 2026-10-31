#include "Render3D/Temporal/HIKARI_TemporalStableDepthPass.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalResourceSystemInternal.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalShader.h"

namespace HIKARI::RENDER3D::TEMPORAL {

namespace {
using Microsoft::WRL::ComPtr;

// 前の結果に掛ける重み。大きいほど線は落ち着くが、深度の変化への追従が遅れる（位置は運動ベクトルで追う）。
constexpr float kStableDepthHistoryWeight = 0.9f;

// b0 の根の定数（Temporal_StableDepth.hlsl の TemporalStableDepthCB と同じ並び）。
struct StableDepthConstants {
    float renderSize[4];
    float outputSize[4];
    float params[4];
};
static_assert(sizeof(StableDepthConstants) == 48u);

struct StableDepthPassState {
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipelineState{};
    uint64_t ownerDeviceGeneration = 0u;
    ID3D12Device* ownerDevice = nullptr;
    bool ready = false;
};

StableDepthPassState& State() {
    static StableDepthPassState state{};
    return state;
}

bool EnsurePipeline(const GFX::Context& graphics) {
    StableDepthPassState& state = State();
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

    // b0 根の定数、t0 ずらした深度、t1 運動ベクトル、t2 前の結果。
    D3D12_DESCRIPTOR_RANGE ranges[3]{};
    for (uint32_t index = 0; index < static_cast<uint32_t>(std::size(ranges)); ++index) {
        ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[index].NumDescriptors = 1;
        ranges[index].BaseShaderRegister = index;
        ranges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }
    D3D12_ROOT_PARAMETER params[4]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Constants = {0u, 0u, static_cast<UINT>(sizeof(StableDepthConstants) / sizeof(uint32_t))};
    for (uint32_t index = 0; index < static_cast<uint32_t>(std::size(ranges)); ++index) {
        D3D12_ROOT_PARAMETER& table = params[index + 1u];
        table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        table.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        table.DescriptorTable.NumDescriptorRanges = 1;
        table.DescriptorTable.pDescriptorRanges = &ranges[index];
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(std::size(params));
    rootDesc.pParameters = params;

    ComPtr<ID3D12RootSignature> rootSignature{};
    HRESULT hr = GFX::CreateRootSignature(device, rootDesc, rootSignature.GetAddressOf());
    if (!HIKARI_DX_CHECK(hr, "TemporalStableDepthPass::CreateRootSignature")) {
        return false;
    }
    rootSignature->SetName(L"HIKARI.Temporal.StableDepthRootSignature");

    const auto vs = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::StableDepth, "VSMain",
                                                 ASSETS::SHADER::ShaderStage::Vertex);
    const auto ps = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::StableDepth, "PSMain",
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
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pipelineState{};
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "TemporalStableDepthPass::CreatePSO")) {
        return false;
    }
    pipelineState->SetName(L"HIKARI.Temporal.StableDepthPSO");
    state.rootSignature = std::move(rootSignature);
    state.pipelineState = std::move(pipelineState);
    state.ownerDeviceGeneration = graphics.deviceGeneration;
    state.ownerDevice = device;
    state.ready = true;
    return true;
}

// 前の結果は、同じ資源の組で、ちょうど 1 フレーム前に書いた時だけ使う。
bool PreviousResultUsable(const DETAIL::TemporalResourceState& state, const TemporalInputs& inputs,
                          const DETAIL::TemporalTarget& previous, const DETAIL::TemporalTarget& output) {
    const TemporalResourceFrame& written = state.stableDepthWritten;
    const TemporalResourceFrame& current = inputs.resourceFrame;
    return !inputs.frame.resetHistory && written.IsValid() && written.view.Matches(current.view) &&
           written.resourceGeneration == current.resourceGeneration &&
           written.deviceGeneration == current.deviceGeneration &&
           current.publication.frameIndex > written.publication.frameIndex &&
           current.publication.frameIndex - written.publication.frameIndex == 1u && previous.ready &&
           previous.width == output.width && previous.height == output.height;
}
} // namespace

RenderTarget2D* ExecuteTemporalStableDepthPass(const GFX::Context& graphics, const TemporalInputs& inputs,
                                               D3D12_GPU_DESCRIPTOR_HANDLE* outSrv) {
    if (outSrv != nullptr) {
        *outSrv = {};
    }
    ID3D12GraphicsCommandList* cmd = graphics.cmdList;
    ID3D12DescriptorHeap* srvHeap = graphics.srvHeap;
    DETAIL::TemporalResourceState* state = DETAIL::FindWritableState(inputs.resourceFrame);
    if (cmd == nullptr || srvHeap == nullptr || state == nullptr || !inputs.MatchesResourceFrame() ||
        graphics.deviceGeneration != inputs.resourceFrame.deviceGeneration || !inputs.sceneDepth.valid ||
        !inputs.motionVectors.valid) {
        return nullptr;
    }
    const uint32_t writeIndex = state->stableDepthWriteIndex & 1u;
    DETAIL::TemporalTarget& output = state->stableDepth[writeIndex];
    DETAIL::TemporalTarget& previous = state->stableDepth[1u - writeIndex];
    if (!output.ready || output.target.GetResource() == nullptr || !previous.srv.IsValid() ||
        !EnsurePipeline(graphics)) {
        return nullptr;
    }
    const bool previousUsable = PreviousResultUsable(*state, inputs, previous, output);

    const float renderWidth = static_cast<float>((std::max)(1u, inputs.frame.renderWidth));
    const float renderHeight = static_cast<float>((std::max)(1u, inputs.frame.renderHeight));
    const float outputWidth = static_cast<float>((std::max)(1u, output.width));
    const float outputHeight = static_cast<float>((std::max)(1u, output.height));
    const StableDepthConstants constants{
        {renderWidth, renderHeight, 1.0f / renderWidth, 1.0f / renderHeight},
        {outputWidth, outputHeight, 1.0f / outputWidth, 1.0f / outputHeight},
        {inputs.frame.camera.jitter.x, inputs.frame.camera.jitter.y, previousUsable ? 1.0f : 0.0f,
         kStableDepthHistoryWeight}};

    GFX::PIX::ScopedGpuEvent pix(cmd, GFX::PIX::kColorPost, "Temporal.StableDepth");
    previous.target.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    output.target.TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = output.target.GetRtvHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, outputWidth, outputHeight, 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(output.width), static_cast<LONG>(output.height)};
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetDescriptorHeaps(1, &srvHeap);
    cmd->SetGraphicsRootSignature(State().rootSignature.Get());
    cmd->SetPipelineState(State().pipelineState.Get());
    cmd->SetGraphicsRoot32BitConstants(0, static_cast<UINT>(sizeof(constants) / sizeof(uint32_t)), &constants, 0);
    cmd->SetGraphicsRootDescriptorTable(1, inputs.sceneDepth.srv);
    cmd->SetGraphicsRootDescriptorTable(2, inputs.motionVectors.srv);
    cmd->SetGraphicsRootDescriptorTable(3, previous.srv.gpu);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
    output.target.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    state->stableDepthWritten = inputs.resourceFrame;
    state->stableDepthWriteIndex = 1u - writeIndex;
    if (outSrv != nullptr) {
        *outSrv = output.srv.gpu;
    }
    return &output.target;
}

void ShutdownTemporalStableDepthPass() {
    StableDepthPassState& state = State();
    state.pipelineState.Reset();
    state.rootSignature.Reset();
    state.ownerDeviceGeneration = 0u;
    state.ownerDevice = nullptr;
    state.ready = false;
}

} // namespace HIKARI::RENDER3D::TEMPORAL
