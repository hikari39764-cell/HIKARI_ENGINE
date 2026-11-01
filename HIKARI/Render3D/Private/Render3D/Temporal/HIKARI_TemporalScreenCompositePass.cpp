#include "Render3D/Temporal/HIKARI_TemporalScreenCompositePass.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalShader.h"

namespace HIKARI::RENDER3D::TEMPORAL {

namespace {
using Microsoft::WRL::ComPtr;

// b0 の定数（Temporal_ScreenComposite.hlsl の TemporalScreenCompositeCB と同じ並び）。
struct ScreenCompositeConstants {
    MATH::Vec4 outputSize{};
    MATH::Vec4 viewExposure{};
    MATH::Vec4 fogParams{};
    MATH::Vec4 fogColorDensity{};
    MATH::Vec4 cameraPositionCount{};
    ScreenCompositeRegionConstants regions[kScreenCompositeRegionCount]{};
};
static_assert(sizeof(ScreenCompositeConstants) == 80u + 176u * kScreenCompositeRegionCount);

// 定数の書き場所。GPU が使い終わる前に上書きしないよう、フレームの数より多めの枠を順に回す。
constexpr uint32_t kConstantSlotCount = GFX::kFrameResourceCount * 4u;
constexpr uint32_t kConstantSlotBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(ScreenCompositeConstants));

struct ScreenCompositePassState {
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipelineState{};
    ComPtr<ID3D12Resource> constants{};
    uint8_t* mappedConstants = nullptr;
    uint32_t nextConstantSlot = 0u;
    uint64_t ownerDeviceGeneration = 0u;
    ID3D12Device* ownerDevice = nullptr;
    bool ready = false;
};

ScreenCompositePassState& State() {
    static ScreenCompositePassState state{};
    return state;
}

bool EnsurePipeline(const GFX::Context& graphics) {
    ScreenCompositePassState& state = State();
    ID3D12Device* device = graphics.device;
    if (device == nullptr || graphics.deviceGeneration == 0u) {
        return false;
    }
    if (state.ready) {
        return state.ownerDeviceGeneration == graphics.deviceGeneration && state.ownerDevice == device;
    }
    if (state.ownerDeviceGeneration != 0u &&
        (state.ownerDeviceGeneration != graphics.deviceGeneration || state.ownerDevice != device)) {
        return false;
    }

    // b0 定数、t0 均した深度、t1..t8 キャプチャの貼図、s0 線形・端で止める。
    constexpr uint32_t kTableCount = 1u + kScreenCompositeTextureCount;
    D3D12_DESCRIPTOR_RANGE ranges[kTableCount]{};
    D3D12_ROOT_PARAMETER params[1u + kTableCount]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor = {0u, 0u};
    for (uint32_t index = 0; index < kTableCount; ++index) {
        ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[index].NumDescriptors = 1;
        ranges[index].BaseShaderRegister = index;
        ranges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER& table = params[index + 1u];
        table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        table.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        table.DescriptorTable.NumDescriptorRanges = 1;
        table.DescriptorTable.pDescriptorRanges = &ranges[index];
    }
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
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

    ComPtr<ID3D12RootSignature> rootSignature{};
    HRESULT hr = GFX::CreateRootSignature(device, rootDesc, rootSignature.GetAddressOf());
    if (!HIKARI_DX_CHECK(hr, "TemporalScreenCompositePass::CreateRootSignature")) {
        return false;
    }
    rootSignature->SetName(L"HIKARI.Temporal.ScreenCompositeRootSignature");

    const auto vs = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::ScreenComposite, "VSMain",
                                                 ASSETS::SHADER::ShaderStage::Vertex);
    const auto ps = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::ScreenComposite, "PSMain",
                                                 ASSETS::SHADER::ShaderStage::Pixel);
    if (!vs || !ps) {
        return false;
    }

    // 前乗算の色と覆いを重ねる。覆いの値は書かない（拡大の結果の alpha をそのまま残す）。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = INTERNAL::TemporalShaderBytecode(vs);
    psoDesc.PS = INTERNAL::TemporalShaderBytecode(ps);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    D3D12_RENDER_TARGET_BLEND_DESC& blend = psoDesc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                                  D3D12_COLOR_WRITE_ENABLE_BLUE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pipelineState{};
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "TemporalScreenCompositePass::CreatePSO")) {
        return false;
    }
    pipelineState->SetName(L"HIKARI.Temporal.ScreenCompositePSO");

    ComPtr<ID3D12Resource> constants{};
    void* mapped = nullptr;
    if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(device, static_cast<UINT64>(kConstantSlotBytes) * kConstantSlotCount,
                                                     constants, &mapped) ||
        mapped == nullptr) {
        return false;
    }
    constants->SetName(L"HIKARI.Temporal.ScreenCompositeConstants");

    state.rootSignature = std::move(rootSignature);
    state.pipelineState = std::move(pipelineState);
    state.constants = std::move(constants);
    state.mappedConstants = static_cast<uint8_t*>(mapped);
    state.ownerDeviceGeneration = graphics.deviceGeneration;
    state.ownerDevice = device;
    state.ready = true;
    return true;
}
} // namespace

bool ExecuteTemporalScreenComposite(const GFX::Context& graphics, RenderTarget2D& output,
                                    D3D12_GPU_DESCRIPTOR_HANDLE depthSrv, const ScreenCompositeFrame& frame,
                                    const MATH::Vec4& viewExposure, const MATH::Vec4& fogParams,
                                    const MATH::Vec4& fogColorDensity) {
    ID3D12GraphicsCommandList* cmd = graphics.cmdList;
    ID3D12DescriptorHeap* srvHeap = graphics.srvHeap;
    if (frame.count == 0u || frame.textureCount == 0u || cmd == nullptr || srvHeap == nullptr ||
        depthSrv.ptr == 0u || output.GetResource() == nullptr ||
        output.GetFormat() != DXGI_FORMAT_R16G16B16A16_FLOAT || !EnsurePipeline(graphics)) {
        return false;
    }
    // キャプチャの貼図は材質が引くのと同じ SRV（同じ書式・同じ色空間）を使う。
    D3D12_GPU_DESCRIPTOR_HANDLE textures[kScreenCompositeTextureCount]{};
    for (uint32_t index = 0; index < frame.textureCount; ++index) {
        textures[index] = GetTextureResourceSrvGpuHandle(frame.textures[index]);
        if (textures[index].ptr == 0u) {
            return false;
        }
    }
    for (uint32_t index = frame.textureCount; index < kScreenCompositeTextureCount; ++index) {
        textures[index] = textures[0];
    }

    ScreenCompositePassState& state = State();
    const uint32_t width = static_cast<uint32_t>((std::max)(1, output.GetWidth()));
    const uint32_t height = static_cast<uint32_t>((std::max)(1, output.GetHeight()));
    ScreenCompositeConstants constants{};
    constants.outputSize = {static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width),
                            1.0f / static_cast<float>(height)};
    constants.viewExposure = viewExposure;
    constants.fogParams = fogParams;
    constants.fogColorDensity = fogColorDensity;
    constants.cameraPositionCount = {frame.cameraPosition.x, frame.cameraPosition.y, frame.cameraPosition.z,
                                     static_cast<float>(frame.count)};
    std::copy(frame.regions.begin(), frame.regions.begin() + frame.count, constants.regions);
    const uint32_t slot = state.nextConstantSlot;
    state.nextConstantSlot = (slot + 1u) % kConstantSlotCount;
    std::memcpy(state.mappedConstants + static_cast<size_t>(slot) * kConstantSlotBytes, &constants, sizeof(constants));
    const D3D12_GPU_VIRTUAL_ADDRESS constantAddress =
        state.constants->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * kConstantSlotBytes;

    GFX::PIX::ScopedGpuEvent pix(cmd, GFX::PIX::kColorPost, "Temporal.ScreenComposite");
    const D3D12_RESOURCE_STATES previousState = output.GetColorState();
    output.TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = output.GetRtvHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetDescriptorHeaps(1, &srvHeap);
    cmd->SetGraphicsRootSignature(state.rootSignature.Get());
    cmd->SetPipelineState(state.pipelineState.Get());
    cmd->SetGraphicsRootConstantBufferView(0, constantAddress);
    cmd->SetGraphicsRootDescriptorTable(1, depthSrv);
    for (uint32_t index = 0; index < kScreenCompositeTextureCount; ++index) {
        cmd->SetGraphicsRootDescriptorTable(2u + index, textures[index]);
    }
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
    output.TransitionColor(previousState);
    return true;
}

void ShutdownTemporalScreenCompositePass() {
    ScreenCompositePassState& state = State();
    if (state.constants != nullptr && state.mappedConstants != nullptr) {
        state.constants->Unmap(0, nullptr);
    }
    state.mappedConstants = nullptr;
    state.constants.Reset();
    state.pipelineState.Reset();
    state.rootSignature.Reset();
    state.nextConstantSlot = 0u;
    state.ownerDeviceGeneration = 0u;
    state.ownerDevice = nullptr;
    state.ready = false;
}

} // namespace HIKARI::RENDER3D::TEMPORAL
