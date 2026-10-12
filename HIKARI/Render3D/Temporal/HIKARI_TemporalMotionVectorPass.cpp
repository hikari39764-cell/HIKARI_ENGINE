#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"
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
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalPassScratch.h"

namespace HIKARI::RENDER3D::TEMPORAL {

namespace {
using Microsoft::WRL::ComPtr;
struct MotionVectorConstants {
    MATH::Mat4 invViewProj{};
    MATH::Mat4 viewProj{};
    MATH::Mat4 prevViewProj{};
    MATH::Vec4 screenParams{};
    MATH::Vec4 historyParams{};
    MATH::Vec4 cameraPos{};
    MATH::Vec4 prevCameraPos{};
};

static_assert(sizeof(MotionVectorConstants) == 256u);
static_assert(offsetof(MotionVectorConstants, screenParams) == 192u);
static_assert(offsetof(MotionVectorConstants, prevCameraPos) == 240u);

struct MotionVectorPassState {
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipelineState{};
    uint64_t ownerDeviceGeneration = 0u;
    ID3D12Device* ownerDevice = nullptr;
    bool ready = false;
};

MotionVectorPassState& State() {
    static MotionVectorPassState state{};
    return state;
}

bool EnsurePipeline(const GFX::Context& graphics) {
    MotionVectorPassState& state = State();
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

    D3D12_DESCRIPTOR_RANGE depthRange{};
    depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depthRange.NumDescriptors = 1;
    depthRange.BaseShaderRegister = 0;
    depthRange.RegisterSpace = 0;
    depthRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &depthRange;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(std::size(params));
    rsDesc.pParameters = params;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    HRESULT hr = GFX::CreateRootSignature(device, rsDesc, rootSignature.GetAddressOf());
    if (!HIKARI_DX_CHECK(hr, "TemporalMotionVectorPass::CreateRootSignature")) {
        return false;
    }
    rootSignature->SetName(L"HIKARI.Temporal.MotionVectorRootSignature");

    const auto vs = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::MotionVector, "VSMain",
                                                 ASSETS::SHADER::ShaderStage::Vertex);
    const auto ps = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::MotionVector, "PSMain",
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
    psoDesc.NumRenderTargets = 2;
    psoDesc.RTVFormats[0] = kTemporalMotionVectorFormat;
    psoDesc.RTVFormats[1] = kTemporalMotionMetadataFormat;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "TemporalMotionVectorPass::CreatePSO")) {
        return false;
    }
    pipelineState->SetName(L"HIKARI.Temporal.MotionVectorPSO");

    state.rootSignature = std::move(rootSignature);
    state.pipelineState = std::move(pipelineState);
    state.ownerDeviceGeneration = graphics.deviceGeneration;
    state.ownerDevice = device;
    state.ready = true;
    return true;
}
} // namespace

bool ExecuteMotionVectorPass(const GFX::Context& graphics, const TemporalInputs& inputs) {
    MotionVectorPassState& state = State();
    ID3D12GraphicsCommandList* cmd = graphics.cmdList;
    ID3D12Device* device = graphics.device;
    ID3D12DescriptorHeap* srvHeap = graphics.srvHeap;
    RenderTarget2D* motionTarget = GetMotionVectorRenderTarget(inputs.resourceFrame);
    RenderTarget2D* metadataTarget = GetMotionMetadataRenderTarget(inputs.resourceFrame);
    if (cmd == nullptr || device == nullptr || srvHeap == nullptr || !inputs.MatchesResourceFrame() ||
        graphics.deviceGeneration != inputs.resourceFrame.deviceGeneration || motionTarget == nullptr ||
        metadataTarget == nullptr || !inputs.motionVectors.valid || !inputs.motionMetadata.valid) {
        (void)MarkMotionVectorsWritten(inputs.resourceFrame, false);
        return false;
    }
    if (!EnsurePipeline(graphics)) {
        (void)MarkMotionVectorsWritten(inputs.resourceFrame, false);
        return false;
    }

    DETAIL::TemporalPassUploadSlice constantUpload{};
    if (!DETAIL::TryAcquireTemporalConstantUpload(graphics, inputs.resourceFrame,
                                                  DETAIL::TemporalConstantUploadKind::MotionVector,
                                                  GFX::AlignD3D12ConstantBufferByteSize(sizeof(MotionVectorConstants)),
                                                  L"HIKARI.Temporal.MotionVectorCB", constantUpload)) {
        (void)MarkMotionVectorsWritten(inputs.resourceFrame, false);
        return false;
    }

    MotionVectorConstants constants{};
    constants.invViewProj = inputs.frame.camera.invViewProj;
    constants.viewProj = inputs.frame.camera.unjitteredViewProj;
    constants.prevViewProj = inputs.frame.camera.prevUnjitteredViewProj;
    constants.screenParams = inputs.frame.camera.screenParams;
    // 反射などは AA Off でも再投影する。色の履歴ではなく、前フレームの相機の連続性で判定する。
    const bool previousValid = inputs.frame.camera.previousValid && !inputs.frame.resetHistory;
    constants.historyParams = {previousValid ? 1.0f : 0.0f, inputs.sceneDepth.IsSampleable() ? 1.0f : 0.0f,
                               (std::max)(inputs.frame.camera.farZ, 1.0f), 0.0f};
    constants.cameraPos = inputs.frame.camera.cameraPos;
    constants.prevCameraPos = inputs.frame.camera.prevCameraPos;
    std::memcpy(constantUpload.mapped, &constants, sizeof(constants));

    GFX::PIX::ScopedGpuEvent pix(cmd, GFX::PIX::kColorRender, "Temporal.MotionVectors");
    GFX::GPU_PROFILE::ScopedGpuTimer gpuTimer(cmd, GFX::GPU_PROFILE::Pass::TemporalMotionVectors);

    if (!inputs.sceneDepth.IsSampleable()) {
        (void)MarkMotionVectorsWritten(inputs.resourceFrame, false);
        return false;
    }

    motionTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {motionTarget->GetRtvHandle(), metadataTarget->GetRtvHandle()};
    cmd->OMSetRenderTargets(2, rtvs, FALSE, nullptr);
    const float motionClear[4] = {};
    const float metadataClear[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    cmd->ClearRenderTargetView(rtvs[0], motionClear, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[1], metadataClear, 0, nullptr);
    const D3D12_VIEWPORT viewport{0.0f,
                                  0.0f,
                                  static_cast<float>((std::max)(1, motionTarget->GetWidth())),
                                  static_cast<float>((std::max)(1, motionTarget->GetHeight())),
                                  0.0f,
                                  1.0f};
    const D3D12_RECT scissor{0, 0, (std::max)(1, motionTarget->GetWidth()), (std::max)(1, motionTarget->GetHeight())};
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);

    cmd->SetDescriptorHeaps(1, &srvHeap);
    cmd->SetGraphicsRootSignature(state.rootSignature.Get());
    cmd->SetPipelineState(state.pipelineState.Get());
    cmd->SetGraphicsRootConstantBufferView(0, constantUpload.gpuAddress);
    cmd->SetGraphicsRootDescriptorTable(1, inputs.sceneDepth.srv);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
    motionTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    (void)MarkMotionVectorsWritten(inputs.resourceFrame, true);
    return true;
}

void ShutdownMotionVectorPass() {
    MotionVectorPassState& state = State();
    state.pipelineState.Reset();
    state.rootSignature.Reset();
    state.ownerDeviceGeneration = 0u;
    state.ownerDevice = nullptr;
    state.ready = false;
}

} // namespace HIKARI::RENDER3D::TEMPORAL
