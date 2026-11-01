#include "Render3D/Pipeline/HIKARI_ProjectFullscreenShaderRuntime.h"

#include <algorithm>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <d3dcommon.h>
#include <wrl/client.h>

#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Shader/HIKARI_ShaderRegistryResolve.h"
#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_ShaderAssetResolver.h"
#include "RenderContracts/Shader/HIKARI_ShaderCookedArtifact.h"
#include "RenderContracts/Shader/HIKARI_ProjectFullscreenShaderContract.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::RENDER3D::PIPELINE {
namespace {

using Microsoft::WRL::ComPtr;
namespace SHADER = ASSETS::SHADER;

constexpr size_t kMaximumCachedProjectPipelines = 16u;
constexpr DXGI_FORMAT kWorkingSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

struct PipelineRecord {
    std::string assetGuid{};
    std::string pixelEntryId{};
    uint64_t registryInstanceId = 0u;
    uint64_t registryRevision = 0u;
    HASH::Sha256Digest sourceClosureHash{};
    HASH::Sha256Digest vertexDxilHash{};
    HASH::Sha256Digest pixelDxilHash{};
    ComPtr<ID3D12PipelineState> pipeline{};
};

struct RuntimeState {
    std::thread::id ownerThread{};
    GFX::Context graphics{};
    ComPtr<ID3D12RootSignature> rootSignature{};
    std::vector<PipelineRecord> pipelines{};
};

RuntimeState& GetState() noexcept {
    static RuntimeState state{};
    return state;
}

void RetireState(RuntimeState& state) noexcept {
    for (PipelineRecord& record : state.pipelines) {
        GFX::RetireD3D12ObjectForFrame(record.pipeline, state.graphics, "ProjectFullscreenShader.PSO");
    }
    state.pipelines.clear();
    GFX::RetireD3D12ObjectForFrame(state.rootSignature, state.graphics, "ProjectFullscreenShader.RootSignature");
    state.graphics = {};
    state.ownerThread = {};
}

bool PrepareState(RuntimeState& state, const GFX::Context& graphics, std::string& outDiagnostic) {
    if (graphics.device == nullptr || graphics.cmdList == nullptr || graphics.srvHeap == nullptr ||
        graphics.deviceGeneration == 0u) {
        outDiagnostic = "graphics context is unavailable";
        return false;
    }
    const std::thread::id caller = std::this_thread::get_id();
    if (state.ownerThread != std::thread::id{} && state.ownerThread != caller) {
        outDiagnostic = "fullscreen shader runtime used off owner thread";
        return false;
    }
    if (state.graphics.deviceGeneration != 0u && state.graphics.deviceGeneration != graphics.deviceGeneration) {
        RetireState(state);
    }
    if (state.ownerThread == std::thread::id{}) {
        state.ownerThread = caller;
    }
    state.graphics = graphics;
    return true;
}

bool CreateRootSignature(RuntimeState& state, std::string& outDiagnostic) {
    if (state.rootSignature != nullptr) {
        return true;
    }
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1u;
    range.BaseShaderRegister = 0u;
    range.RegisterSpace = 0u;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].Descriptor.ShaderRegister = 0u;
    parameters[1].Descriptor.RegisterSpace = 0u;

    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[2].Constants = {1u, 0u, kScreenReadabilityConstantCount};
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1u;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0u;
    sampler.RegisterSpace = 0u;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = 3u;
    description.pParameters = parameters;
    description.NumStaticSamplers = 1u;
    description.pStaticSamplers = &sampler;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serialized{};
    ComPtr<ID3DBlob> error{};
    HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(),
                                                 error.GetAddressOf());
    if (FAILED(result) || serialized == nullptr) {
        outDiagnostic = "could not serialize project root signature";
        if (error != nullptr && error->GetBufferPointer() != nullptr) {
            outDiagnostic += ": ";
            outDiagnostic.append(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        }
        return false;
    }
    result = state.graphics.device->CreateRootSignature(0u, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                        IID_PPV_ARGS(state.rootSignature.GetAddressOf()));
    if (!HIKARI_DX_CHECK(result, "CreateRootSignature: ProjectFullscreenShader")) {
        outDiagnostic = "could not create project root signature";
        return false;
    }
    GFX::SetD3D12Name(state.rootSignature.Get(), L"HIKARI Project Fullscreen Root Signature");
    return true;
}

bool SameLogicalProgram(const PipelineRecord& record, const GameProjectShaderProgramRef& program) noexcept {
    return record.assetGuid == program.assetGuid && record.pixelEntryId == program.pixelEntryId;
}

bool SameArtifact(const PipelineRecord& record, const SHADER::ShaderCookedArtifact& projectArtifact,
                  const SHADER::ShaderCookedEntry& vertex, const SHADER::ShaderCookedEntry& pixel) noexcept {
    return record.sourceClosureHash == projectArtifact.sourceClosureHash && record.vertexDxilHash == vertex.dxilHash &&
           record.pixelDxilHash == pixel.dxilHash;
}

ID3D12PipelineState* ResolvePipeline(RuntimeState& state, const AssetRegistry& assets,
                                     const GameProjectShaderProgramRef& program, std::string& outDiagnostic) {
    const uint64_t registryInstanceId = assets.GetInstanceId();
    const uint64_t registryRevision = assets.GetRevision();
    const auto found =
        std::find_if(state.pipelines.begin(), state.pipelines.end(),
                     [&program](const PipelineRecord& record) { return SameLogicalProgram(record, program); });
    if (found != state.pipelines.end() && found->registryInstanceId == registryInstanceId &&
        found->registryRevision == registryRevision && found->pipeline != nullptr) {
        return found->pipeline.Get();
    }
    const SHADER::ShaderAssetContractExpectation contract = SHADER::MakeProjectFullscreenShaderContract([&state]() {
        const GFX::ShaderDeviceCapabilities capabilities = GFX::QueryShaderDeviceCapabilities(state.graphics.device);
        return SHADER::ShaderRuntimeDeviceCapabilities{capabilities.shaderModel, capabilities.rootSignatureVersion,
                                                       capabilities.featureMask};
    }());
    SHADER::ShaderAssetResolveResult vertex =
        SHADER::ResolveBuiltinShaderEntry("HIKARI.EngineShader.Post_ProjectFullscreen",
                                          {"FullscreenVS", SHADER::ShaderStage::Vertex, "vs_6_0"}, contract);
    if (!vertex) {
        outDiagnostic = "vertex resolve failed: " + vertex.diagnostic;
        return nullptr;
    }
    SHADER::ShaderAssetResolveResult pixel = SHADER::ResolveShaderAssetByGuid(
        assets, AssetGuid{program.assetGuid}, {program.pixelEntryId, SHADER::ShaderStage::Pixel, "ps_6_0"}, contract);
    if (!pixel) {
        outDiagnostic = "pixel resolve failed: " + pixel.diagnostic;
        return nullptr;
    }
    const SHADER::ShaderCookedArtifact* vertexArtifact = vertex.lease.GetArtifact();
    const SHADER::ShaderCookedArtifact* projectArtifact = pixel.lease.GetArtifact();
    const SHADER::ShaderCookedEntry* vertexEntry = vertex.lease.GetEntry();
    const SHADER::ShaderCookedEntry* pixelEntry = pixel.lease.GetEntry();
    if (vertexArtifact == nullptr || projectArtifact == nullptr || vertexEntry == nullptr || pixelEntry == nullptr ||
        !SHADER::ValidateProjectFullscreenShaderContract(*vertexArtifact, *vertexEntry, *projectArtifact, *pixelEntry,
                                                         outDiagnostic)) {
        if (outDiagnostic.empty()) {
            outDiagnostic = "fullscreen Shader entries are invalid";
        }
        return nullptr;
    }

    if (found != state.pipelines.end() && SameArtifact(*found, *projectArtifact, *vertexEntry, *pixelEntry)) {
        found->registryInstanceId = registryInstanceId;
        found->registryRevision = registryRevision;
        return found->pipeline.Get();
    }
    if (found == state.pipelines.end() && state.pipelines.size() >= kMaximumCachedProjectPipelines) {
        outDiagnostic = "project shader pipeline cache is full";
        return nullptr;
    }
    if (!CreateRootSignature(state, outDiagnostic)) {
        return nullptr;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = state.rootSignature.Get();
    const std::span<const uint8_t> vertexDxil = vertex.lease.GetDxil();
    const std::span<const uint8_t> pixelDxil = pixel.lease.GetDxil();
    description.VS = {vertexDxil.data(), vertexDxil.size()};
    description.PS = {pixelDxil.data(), pixelDxil.size()};

    D3D12_RENDER_TARGET_BLEND_DESC targetBlend{};
    targetBlend.BlendEnable = FALSE;
    targetBlend.LogicOpEnable = FALSE;
    targetBlend.SrcBlend = D3D12_BLEND_ONE;
    targetBlend.DestBlend = D3D12_BLEND_ZERO;
    targetBlend.BlendOp = D3D12_BLEND_OP_ADD;
    targetBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    targetBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    targetBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    targetBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    targetBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    description.BlendState.AlphaToCoverageEnable = FALSE;
    description.BlendState.IndependentBlendEnable = FALSE;
    description.BlendState.RenderTarget[0] = targetBlend;

    description.SampleMask = UINT_MAX;
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.RasterizerState.FrontCounterClockwise = FALSE;
    description.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    description.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    description.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    description.RasterizerState.DepthClipEnable = TRUE;
    description.RasterizerState.MultisampleEnable = FALSE;
    description.RasterizerState.AntialiasedLineEnable = FALSE;
    description.RasterizerState.ForcedSampleCount = 0u;
    description.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    description.DepthStencilState.DepthEnable = FALSE;
    description.DepthStencilState.StencilEnable = FALSE;
    description.InputLayout = {nullptr, 0u};
    description.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1u;
    description.RTVFormats[0] = kWorkingSceneColorFormat;
    description.DSVFormat = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1u;

    ComPtr<ID3D12PipelineState> pipeline{};
    const HRESULT created =
        state.graphics.device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(pipeline.GetAddressOf()));
    if (!HIKARI_DX_CHECK(created, "CreateGraphicsPipelineState: ProjectFullscreenShader")) {
        outDiagnostic = "could not create project fullscreen PSO";
        return nullptr;
    }
    GFX::SetD3D12Name(pipeline.Get(), L"HIKARI Project Fullscreen Shader PSO");

    PipelineRecord record{};
    record.assetGuid = program.assetGuid;
    record.pixelEntryId = program.pixelEntryId;
    record.registryInstanceId = registryInstanceId;
    record.registryRevision = registryRevision;
    record.sourceClosureHash = projectArtifact->sourceClosureHash;
    record.vertexDxilHash = vertexEntry->dxilHash;
    record.pixelDxilHash = pixelEntry->dxilHash;
    record.pipeline = std::move(pipeline);
    if (found != state.pipelines.end()) {
        GFX::RetireD3D12ObjectForFrame(found->pipeline, state.graphics, "ProjectFullscreenShader.StalePSO");
        *found = std::move(record);
        return found->pipeline.Get();
    }
    state.pipelines.push_back(std::move(record));
    return state.pipelines.back().pipeline.Get();
}

} // namespace

bool DrawProjectFullscreenShader(const GFX::Context& graphics, const AssetRegistry& assets,
                                 const GameProjectShaderProgramRef& program, const RenderTextureView& input,
                                 uint32_t width, uint32_t height, const ScreenReadabilityFrame& readability,
                                 std::string& outDiagnostic) noexcept {
    try {
        outDiagnostic.clear();
        if (!program.IsValid() || !input.IsSampleable() || input.resource == nullptr || width == 0u || height == 0u ||
            (input.state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) == 0u) {
            outDiagnostic = "fullscreen draw request is invalid";
            return false;
        }
        RuntimeState& state = GetState();
        if (!PrepareState(state, graphics, outDiagnostic)) {
            return false;
        }
        ID3D12PipelineState* pipeline = ResolvePipeline(state, assets, program, outDiagnostic);
        if (pipeline == nullptr || state.rootSignature == nullptr) {
            return false;
        }

        ID3D12DescriptorHeap* heaps[] = {graphics.srvHeap};
        graphics.cmdList->SetDescriptorHeaps(1u, heaps);
        graphics.cmdList->SetGraphicsRootSignature(state.rootSignature.Get());
        graphics.cmdList->SetPipelineState(pipeline);
        graphics.cmdList->SetGraphicsRootDescriptorTable(0u, input.srv);
        graphics.cmdList->SetGraphicsRoot32BitConstants(2u, kScreenReadabilityConstantCount, &readability, 0u);

        const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        graphics.cmdList->RSSetViewports(1u, &viewport);
        graphics.cmdList->RSSetScissorRects(1u, &scissor);
        graphics.cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        graphics.cmdList->DrawInstanced(3u, 1u, 0u, 0u);
        return true;
    } catch (...) {
        outDiagnostic = "fullscreen shader runtime threw an exception";
        return false;
    }
}

void ReleaseProjectFullscreenShaderRuntime() noexcept {
    RuntimeState& state = GetState();
    if (state.ownerThread != std::thread::id{} && state.ownerThread != std::this_thread::get_id()) {
        return;
    }
    RetireState(state);
}

} // namespace HIKARI::RENDER3D::PIPELINE
