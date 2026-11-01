#include "Render3D/Pipeline/HIKARI_ProjectComputeShaderRuntime.h"

#include <algorithm>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include "Core/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Shader/HIKARI_ShaderRegistryResolve.h"
#include "RenderContracts/Shader/HIKARI_ProjectDispatchShaderContract.h"
#include "RenderContracts/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Render3D/Pipeline/Internal/HIKARI_ProjectDispatchOutput.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::PIPELINE {
namespace {

using Microsoft::WRL::ComPtr;
namespace SHADER = ASSETS::SHADER;

constexpr size_t kMaximumCachedProjectComputePipelines = 16u;
constexpr DXGI_FORMAT kDispatchOutputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

struct PipelineRecord {
    std::string assetGuid{};
    std::string computeEntryId{};
    uint64_t registryInstanceId = 0u;
    uint64_t registryRevision = 0u;
    HASH::Sha256Digest sourceClosureHash{};
    HASH::Sha256Digest computeDxilHash{};
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
        GFX::RetireD3D12ObjectForFrame(record.pipeline, state.graphics, "ProjectDispatch.PSO");
    }
    state.pipelines.clear();
    GFX::RetireD3D12ObjectForFrame(state.rootSignature, state.graphics, "ProjectDispatch.RootSignature");
    state.graphics = {};
    state.ownerThread = {};
}

bool PrepareState(RuntimeState& state, const GFX::Context& graphics, std::string& outDiagnostic) {
    if (graphics.device == nullptr || graphics.cmdList == nullptr || graphics.srvHeap == nullptr ||
        graphics.deviceGeneration == 0u || graphics.frameIndex >= GFX::kFrameResourceCount) {
        outDiagnostic = "project Dispatch graphics context is unavailable";
        return false;
    }
    const std::thread::id caller = std::this_thread::get_id();
    if (state.ownerThread != std::thread::id{} && state.ownerThread != caller) {
        outDiagnostic = "project Dispatch used off render owner thread";
        return false;
    }
    if (state.graphics.deviceGeneration != 0u &&
        (state.graphics.deviceGeneration != graphics.deviceGeneration || state.graphics.device != graphics.device ||
         state.graphics.srvHeap != graphics.srvHeap)) {
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
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1u;
    ranges[0].OffsetInDescriptorsFromTableStart = 0u;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1u;
    ranges[1].OffsetInDescriptorsFromTableStart = 1u;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[0].DescriptorTable.NumDescriptorRanges = 2u;
    parameters[0].DescriptorTable.pDescriptorRanges = ranges;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].Constants.Num32BitValues = 4u;
    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = 2u;
    description.pParameters = parameters;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ComPtr<ID3DBlob> serialized{};
    ComPtr<ID3DBlob> errors{};
    const HRESULT serializedResult = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1,
                                                                 serialized.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(serializedResult) || serialized == nullptr) {
        outDiagnostic = "could not serialize project Dispatch root signature";
        return false;
    }
    const HRESULT created =
        state.graphics.device->CreateRootSignature(0u, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                   IID_PPV_ARGS(state.rootSignature.GetAddressOf()));
    if (!HIKARI_DX_CHECK(created, "CreateRootSignature: ProjectDispatch")) {
        outDiagnostic = "could not create project Dispatch root signature";
        return false;
    }
    GFX::SetD3D12Name(state.rootSignature.Get(), L"HIKARI Project Dispatch Root Signature");
    return true;
}

bool SameLogicalProgram(const PipelineRecord& record, const GameProjectComputeShaderProgramRef& program) noexcept {
    return record.assetGuid == program.assetGuid && record.computeEntryId == program.computeEntryId;
}

ID3D12PipelineState* ResolvePipeline(RuntimeState& state, const AssetRegistry& assets,
                                     const GameProjectComputeShaderProgramRef& program, std::string& outDiagnostic) {
    const uint64_t registryInstanceId = assets.GetInstanceId();
    const uint64_t registryRevision = assets.GetRevision();
    auto found = std::find_if(state.pipelines.begin(), state.pipelines.end(),
                              [&program](const PipelineRecord& record) { return SameLogicalProgram(record, program); });
    if (found != state.pipelines.end() && found->registryInstanceId == registryInstanceId &&
        found->registryRevision == registryRevision && found->pipeline != nullptr) {
        return found->pipeline.Get();
    }
    const GFX::ShaderDeviceCapabilities capabilities = GFX::QueryShaderDeviceCapabilities(state.graphics.device);
    const SHADER::ShaderAssetContractExpectation contract = SHADER::MakeProjectDispatchShaderContract(
        {capabilities.shaderModel, capabilities.rootSignatureVersion, capabilities.featureMask});
    SHADER::ShaderAssetResolveResult resolved =
        SHADER::ResolveShaderAssetByGuid(assets, AssetGuid{program.assetGuid},
                                         {program.computeEntryId, SHADER::ShaderStage::Compute, "cs_6_0"}, contract);
    const SHADER::ShaderCookedArtifact* artifact = resolved ? resolved.lease.GetArtifact() : nullptr;
    const SHADER::ShaderCookedEntry* entry = resolved ? resolved.lease.GetEntry() : nullptr;
    if (!resolved || artifact == nullptr || entry == nullptr ||
        !SHADER::ValidateProjectDispatchShaderContract(*artifact, *entry, outDiagnostic)) {
        if (outDiagnostic.empty()) {
            outDiagnostic = resolved.diagnostic;
        }
        return nullptr;
    }
    if (found != state.pipelines.end() && found->sourceClosureHash == artifact->sourceClosureHash &&
        found->computeDxilHash == entry->dxilHash) {
        found->registryInstanceId = registryInstanceId;
        found->registryRevision = registryRevision;
        return found->pipeline.Get();
    }
    if (found == state.pipelines.end() && state.pipelines.size() >= kMaximumCachedProjectComputePipelines) {
        outDiagnostic = "project Dispatch pipeline cache is full";
        return nullptr;
    }
    if (!CreateRootSignature(state, outDiagnostic)) {
        return nullptr;
    }
    const std::span<const uint8_t> dxil = resolved.lease.GetDxil();
    if (dxil.empty()) {
        outDiagnostic = "project Dispatch bytecode is empty";
        return nullptr;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = state.rootSignature.Get();
    description.CS = {dxil.data(), dxil.size()};
    ComPtr<ID3D12PipelineState> pipeline{};
    const HRESULT created =
        state.graphics.device->CreateComputePipelineState(&description, IID_PPV_ARGS(pipeline.GetAddressOf()));
    if (!HIKARI_DX_CHECK(created, "CreateComputePipelineState: ProjectDispatch")) {
        outDiagnostic = "could not create project Dispatch PSO";
        return nullptr;
    }
    PipelineRecord replacement{};
    replacement.assetGuid = program.assetGuid;
    replacement.computeEntryId = program.computeEntryId;
    replacement.registryInstanceId = registryInstanceId;
    replacement.registryRevision = registryRevision;
    replacement.sourceClosureHash = artifact->sourceClosureHash;
    replacement.computeDxilHash = entry->dxilHash;
    replacement.pipeline = std::move(pipeline);
    if (found != state.pipelines.end()) {
        GFX::RetireD3D12ObjectForFrame(found->pipeline, state.graphics, "ProjectDispatch.StalePSO");
        *found = std::move(replacement);
        return found->pipeline.Get();
    }
    state.pipelines.push_back(std::move(replacement));
    return state.pipelines.back().pipeline.Get();
}

} // namespace

bool DispatchProjectComputeShader(const GFX::Context& graphics, const AssetRegistry& assets,
                                  const GameProjectComputeShaderProgramRef& program, const RenderTextureView& input,
                                  SURFACE::SceneSurfaceSet& surfaces, RenderFrameToken frame, uint32_t width,
                                  uint32_t height, std::string& outDiagnostic) noexcept {
    try {
        outDiagnostic.clear();
        if (!program.IsValid() || !frame.IsValid() || !surfaces.MatchesFrame(frame) || !input.IsSampleable() ||
            input.resource == nullptr || input.format != kDispatchOutputFormat || input.width != width ||
            input.height != height || width == 0u || height == 0u) {
            outDiagnostic = "project Dispatch request is invalid";
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
        INTERNAL::ProjectDispatchOutputBinding output{};
        if (!INTERNAL::PrepareProjectDispatchOutput(graphics, input, width, height, output, outDiagnostic)) {
            return false;
        }
        const bool transitionInput = (input.state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0u;
        if (transitionInput) {
            const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                input.resource, input.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            graphics.cmdList->ResourceBarrier(1u, &barrier);
        }
        ID3D12DescriptorHeap* heaps[] = {graphics.srvHeap};
        graphics.cmdList->SetDescriptorHeaps(1u, heaps);
        graphics.cmdList->SetComputeRootSignature(state.rootSignature.Get());
        graphics.cmdList->SetPipelineState(pipeline);
        graphics.cmdList->SetComputeRootDescriptorTable(0u, output.table);
        const uint32_t constants[4]{width, height, static_cast<uint32_t>(frame.frameIndex), 0u};
        graphics.cmdList->SetComputeRoot32BitConstants(1u, 4u, constants, 0u);
        graphics.cmdList->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
        if (transitionInput) {
            const D3D12_RESOURCE_BARRIER restore = CD3DX12_RESOURCE_BARRIER::Transition(
                input.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, input.state);
            graphics.cmdList->ResourceBarrier(1u, &restore);
        }
        return INTERNAL::PublishProjectDispatchOutput(graphics, surfaces, frame, output, outDiagnostic);
    } catch (...) {
        outDiagnostic = "project Dispatch runtime threw an exception";
        return false;
    }
}

void ReleaseProjectComputeShaderRuntime() noexcept {
    RuntimeState& state = GetState();
    if (state.ownerThread != std::thread::id{} && state.ownerThread != std::this_thread::get_id()) {
        return;
    }
    RetireState(state);
    INTERNAL::ReleaseProjectDispatchOutputResources();
}

} // namespace HIKARI::RENDER3D::PIPELINE
