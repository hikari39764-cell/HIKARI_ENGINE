#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Lighting/HIKARI_EnvironmentLightingCache.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <d3dx12.h>
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::IBL {
namespace {
using Microsoft::WRL::ComPtr;
using namespace RENDER3D;
constexpr UINT kResolution = 128, kMipCount = 8, kDiffuseResolution = 16;
constexpr UINT kDispatchCount = kMipCount + 2, kDispatchBudget = 2;
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr auto kReadState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
struct Constants {
    ATMOSPHERE::AtmosphereLutConstants atmosphere{};
    // x SkyMode、y SkyView LUT あり、z 夜空の天頂背景輝度 [nits]、w 雲の空あり。
    MATH::Vec4 source{0, 0, 0, 0};
};
static_assert(sizeof(Constants) == 144);

struct Cube {
    ComPtr<ID3D12Resource> resource;
    TextureResourceHandle texture;
    std::array<RenderResourceView, kMipCount> uavs{};
    D3D12_RESOURCE_STATES state = kReadState;
    uint64_t allocatedBytes = 0;

    void Reset(const GFX::Context& graphics) {
        for (auto& view : uavs)
            RetireRenderResourceDescriptor(view, graphics, "EnvironmentLighting.UAV");
        if (texture)
            ReleaseTextureResource(texture);
        texture = {};
        allocatedBytes = 0;
        GFX::RetireD3D12ObjectForFrame(resource, graphics, "EnvironmentLighting.Cube");
    }
    bool Create(const GFX::Context& graphics, UINT size, UINT mips) {
        if (resource)
            return true;
        const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(kFormat, size, size, 6, static_cast<UINT16>(mips), 1, 0,
                                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(GFX::VIDEO_MEMORY::CreateCommittedResource(graphics.device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                              kReadState, nullptr, GFX::VIDEO_MEMORY::Owner::Lighting,
                                                              IID_PPV_ARGS(&resource))))
            return false;
        resource->SetName(L"HIKARI.EnvironmentLighting.Cube");
        RenderResourceDesc record{};
        record.debugName = "EnvironmentLighting.Cube";
        texture = RegisterCubeTextureResourceFromNative(resource.Get(), kFormat, record);
        for (UINT mip = 0; mip < mips; ++mip) {
            uavs[mip] = AllocateRenderResourceDescriptor();
            if (!uavs[mip].IsValid()) {
                Reset(graphics);
                return false;
            }
            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
            view.Format = kFormat;
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.ArraySize = 6;
            view.Texture2DArray.MipSlice = mip;
            graphics.device->CreateUnorderedAccessView(resource.Get(), nullptr, &view, uavs[mip].cpu);
        }
        if (!IsTextureResourceValid(texture)) {
            Reset(graphics);
            return false;
        }
        state = kReadState;
        allocatedBytes = graphics.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
        return true;
    }
    void Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES next) {
        if (state == next)
            return;
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), state, next);
        commands->ResourceBarrier(1, &barrier);
        state = next;
    }
};
struct Set {
    Cube radiance, diffuse, specular;
    uint64_t identity = 0, epoch = 0, completedFrame = 0;
    void Reset(const GFX::Context& graphics) {
        radiance.Reset(graphics);
        diffuse.Reset(graphics);
        specular.Reset(graphics);
        identity = epoch = completedFrame = 0;
    }
};
} // namespace

struct EnvironmentLightingCache::State {
    std::array<Set, 2> sets;
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    std::array<RenderResourceView, 2> nullSrvs{};
    ID3D12Device* device = nullptr;
    uint64_t deviceGeneration = 0;
    uint64_t budgetFrame = (std::numeric_limits<uint64_t>::max)();
    uint64_t retryAfterFrame = 0, retryEpoch = 0;
    EnvironmentLightingCacheFailure retryFailure = EnvironmentLightingCacheFailure::None;
    UINT published = 0, pending = 1, nextDispatch = kDispatchCount;
    Constants constants{};
    EnvironmentLightingCacheStatus status{};

    bool PublishStatus(uint64_t identity, uint64_t epoch, uint64_t frameIndex,
                       EnvironmentLightingCacheFailure failure = EnvironmentLightingCacheFailure::None) {
        const auto& result = sets[published];
        status.valid = result.identity == identity && result.epoch != 0;
        status.current = status.valid && result.epoch == epoch;
        status.sourceEpoch = status.valid ? result.epoch : 0;
        status.ageFrames = status.valid && frameIndex >= result.completedFrame ? frameIndex - result.completedFrame : 0;
        status.building = nextDispatch < kDispatchCount;
        status.remainingDispatches = status.building ? kDispatchCount - nextDispatch : 0;
        status.failure = failure;
        status.workingSetBytes = 0;
        for (const auto& set : sets)
            for (const Cube* texture : {&set.radiance, &set.diffuse, &set.specular})
                status.workingSetBytes += texture->allocatedBytes;
        return status.valid;
    }

    bool EnsurePipeline(const GFX::Context& graphics) {
        if (pipeline)
            return true;
        std::array<CD3DX12_DESCRIPTOR_RANGE, 5> ranges;
        std::array<CD3DX12_ROOT_PARAMETER, 7> parameters;
        parameters[0].InitAsConstantBufferView(0);
        parameters[1].InitAsConstants(4, 1);
        for (UINT i = 0; i < 4; ++i) {
            ranges[i].Init(i == 3 ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1,
                           i == 3 ? 0 : i);
            parameters[i + 2].InitAsDescriptorTable(1, &ranges[i]);
        }
        ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
        parameters[6].InitAsDescriptorTable(1, &ranges[4]);
        std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> samplers;
        samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                         D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        samplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                         D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(parameters.size()), parameters.data(), 2, samplers.data());
        if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(graphics.device, rootDesc, &root),
                             "EnvironmentLighting root creation"))
            return false;
        namespace SHADER = ASSETS::SHADER;
        SHADER::ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId = "HIKARI.EnvironmentLighting";
        contract.engineInterfaceVersion = 1;
        contract.rootLayoutId = "HIKARI.EnvironmentLighting.Root";
        contract.rootLayoutVersion = 1;
        if (!HASH::ComputeSha256(
                "EnvironmentLightingCB:192; ConvolutionJob:16; raw scene-linear cubemap E/pi and GGX; no analytic sun disk; source.w premultiplied cloud sky over sky (t3, sky-view mapping)",
                contract.engineInterfaceHash) ||
            !HASH::ComputeSha256(
                "CBV(b0);Constants(b1,4);SRV(t0,cube);SRV(t1,2d);SRV(t2,2d);SRV(t3,2d);UAV(u0,array);Sampler(s0,clamp);Sampler(s1,wrapX)",
                contract.rootLayoutHash))
            return false;
        const auto caps = GFX::QueryShaderDeviceCapabilities(graphics.device);
        contract.availableDeviceCaps = {caps.shaderModel, caps.rootSignatureVersion, caps.featureMask};
        const auto shader =
            SHADER::ResolveBuiltinShaderEntry("HIKARI.EngineShader.Render3D_EnvironmentLightingCS",
                                              {"Main", SHADER::ShaderStage::Compute, "cs_6_0"}, contract);
        if (!shader) {
            DEBUGLOG::PushRenderError("Environment lighting: " + shader.diagnostic);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        const auto bytes = shader.lease.GetDxil();
        desc.CS = {bytes.data(), bytes.size()};
        if (!HIKARI_DX_CHECK(graphics.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)),
                             "EnvironmentLighting pipeline creation"))
            return false;
        for (UINT index = 0; index < 2; ++index) {
            nullSrvs[index] = AllocateRenderResourceDescriptor();
            if (!nullSrvs[index].IsValid()) {
                DEBUGLOG::PushRenderError("EnvironmentLighting null SRV allocation failed");
                return false;
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = kFormat;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.ViewDimension = index == 0 ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
            if (index == 0)
                srv.TextureCube.MipLevels = 1;
            else
                srv.Texture2D.MipLevels = 1;
            graphics.device->CreateShaderResourceView(nullptr, &srv, nullSrvs[index].cpu);
        }
        return true;
    }
};

EnvironmentLightingCache::EnvironmentLightingCache() : state_(std::make_unique<State>()) {}
EnvironmentLightingCache::~EnvironmentLightingCache() = default;

void EnvironmentLightingCache::Reset(const GFX::Context& graphics) {
    auto& state = *state_;
    for (auto& set : state.sets)
        set.Reset(graphics);
    for (auto& upload : state.uploads)
        upload.Finalize(&graphics);
    for (auto& srv : state.nullSrvs)
        RetireRenderResourceDescriptor(srv, graphics, "EnvironmentLighting.NullSRV");
    GFX::RetireD3D12ObjectForFrame(state.pipeline, graphics, "EnvironmentLighting.PSO");
    GFX::RetireD3D12ObjectForFrame(state.root, graphics, "EnvironmentLighting.Root");
    state.status = {};
    state.device = nullptr;
    state.nextDispatch = kDispatchCount;
    state.retryAfterFrame = state.retryEpoch = 0;
    state.retryFailure = EnvironmentLightingCacheFailure::None;
    state.budgetFrame = (std::numeric_limits<uint64_t>::max)();
}

void EnvironmentLightingCache::Invalidate() {
    for (auto& set : state_->sets)
        set.identity = set.epoch = set.completedFrame = 0;
    state_->status = {};
    state_->nextDispatch = kDispatchCount;
    state_->retryAfterFrame = state_->retryEpoch = 0;
    state_->retryFailure = EnvironmentLightingCacheFailure::None;
    state_->budgetFrame = (std::numeric_limits<uint64_t>::max)();
}

bool EnvironmentLightingCache::Update(const GFX::Context& graphics, SkyMode mode, std::string_view skyAsset,
                                      const ATMOSPHERE::AtmosphereLutConstants& atmosphere,
                                      TextureResourceHandle source, D3D12_GPU_DESCRIPTOR_HANDLE atmosphereSkyView,
                                      D3D12_GPU_DESCRIPTOR_HANDLE cloudSky, uint64_t cloudSkyVersion,
                                      float nightSkyLuminance, uint64_t frameIndex) {
    auto& state = *state_;
    if (state.budgetFrame != frameIndex) {
        state.budgetFrame = frameIndex;
        state.status.dispatchCount = 0;
    }
    state.status.valid = false;
    state.status.current = false;
    state.status.building = false;
    state.status.sourceEpoch = state.status.ageFrames = state.status.remainingDispatches = 0;
    state.status.failure = EnvironmentLightingCacheFailure::None;
    if (mode == SkyMode::None || graphics.deviceGeneration == 0 || !graphics.device || !graphics.cmdList ||
        !graphics.srvHeap) {
        state.status.failure = EnvironmentLightingCacheFailure::MissingInput;
        return false;
    }
    if (state.device && (state.device != graphics.device || state.deviceGeneration != graphics.deviceGeneration))
        Reset(graphics);
    state.device = graphics.device;
    state.deviceGeneration = graphics.deviceGeneration;
    UpdateRenderResourceDescriptorPoolContext(graphics);
    const auto descriptors = GetRenderResourceDescriptorPoolStats();
    if (!descriptors.contextAvailable || !descriptors.contextCompatible) {
        state.status.failure = EnvironmentLightingCacheFailure::MissingInput;
        return false;
    }
    const bool cube = mode == SkyMode::Cubemap, panorama = mode == SkyMode::Texture2D;
    if ((cube || panorama) && (!IsTextureResourceValid(source) ||
                               GetTextureResourceDimension(source) != (cube ? TextureResourceDimension::TextureCube
                                                                            : TextureResourceDimension::Texture2D))) {
        state.status.failure = EnvironmentLightingCacheFailure::MissingInput;
        return false;
    }
    Constants requested{};
    if (mode == SkyMode::Atmosphere) {
        requested.atmosphere = atmosphere;
        requested.source = {float(mode), atmosphereSkyView.ptr ? 1.0f : 0.0f, (std::max)(0.0f, nightSkyLuminance),
                            cloudSky.ptr ? 1.0f : 0.0f};
    } else {
        requested.source = {float(mode), 0, 0, cloudSky.ptr ? 1.0f : 0.0f};
    }
    uint64_t identity = HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis, &mode, sizeof(mode));
    if (cube || panorama) {
        identity = HASH::Fnv1aBytes(identity, skyAsset.data(), skyAsset.size());
        identity = HASH::Fnv1aBytes(identity, &source, sizeof(source));
    }
    uint64_t epoch = HASH::Fnv1aBytes(identity, &requested, sizeof(requested));
    if (cloudSky.ptr)
        epoch = HASH::Fnv1aBytes(epoch, &cloudSkyVersion, sizeof(cloudSkyVersion));
    if (state.retryEpoch == epoch && frameIndex < state.retryAfterFrame)
        return state.PublishStatus(identity, epoch, frameIndex, state.retryFailure);
    if (state.nextDispatch < kDispatchCount && state.sets[state.pending].identity != identity)
        state.nextDispatch = kDispatchCount;
    auto& complete = state.sets[state.published];
    const bool wantsUpdate = complete.identity != identity || complete.epoch != epoch;
    if (wantsUpdate && state.status.dispatchCount < kDispatchBudget) {
        if (!state.EnsurePipeline(graphics)) {
            Reset(graphics);
            state.retryEpoch = epoch;
            state.retryAfterFrame = frameIndex + 60;
            state.retryFailure = EnvironmentLightingCacheFailure::Pipeline;
            return state.PublishStatus(identity, epoch, frameIndex, EnvironmentLightingCacheFailure::Pipeline);
        }
        auto& target = state.sets[state.pending];
        if (!target.radiance.Create(graphics, kResolution, 1) ||
            !target.diffuse.Create(graphics, kDiffuseResolution, 1) ||
            !target.specular.Create(graphics, kResolution, kMipCount)) {
            target.Reset(graphics);
            state.nextDispatch = kDispatchCount;
            state.retryEpoch = epoch;
            state.retryAfterFrame = frameIndex + 60;
            state.retryFailure = EnvironmentLightingCacheFailure::Allocation;
            return state.PublishStatus(identity, epoch, frameIndex, EnvironmentLightingCacheFailure::Allocation);
        }
        auto& uploads = state.uploads[graphics.frameIndex % GFX::kFrameResourceCount];
        if (!uploads.IsInitialized() &&
            !uploads.Init(graphics.device, 4096, GFX::UPLOAD_TRAFFIC::Owner::PassConstants)) {
            return state.PublishStatus(identity, epoch, frameIndex, EnvironmentLightingCacheFailure::Upload);
        }
        uploads.ResetForFrame(graphics.currentFrameRetireFenceValue);
        if (state.nextDispatch == kDispatchCount) {
            target.identity = identity;
            target.epoch = epoch;
            state.constants = requested;
            state.nextDispatch = 0;
        }
        const auto constants = uploads.Allocate(sizeof(Constants));
        if (!constants)
            return state.PublishStatus(identity, epoch, frameIndex, EnvironmentLightingCacheFailure::Upload);
        std::memcpy(constants.cpuAddress, &state.constants, sizeof(Constants));
        auto* commands = graphics.cmdList;
        GFX::GPU_PROFILE::ScopedGpuTimer timer(commands, GFX::GPU_PROFILE::Pass::EnvironmentLighting);
        ID3D12DescriptorHeap* heaps[]{graphics.srvHeap};
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetComputeRootSignature(state.root.Get());
        commands->SetPipelineState(state.pipeline.Get());
        commands->SetComputeRootConstantBufferView(0, constants.gpuAddress);
        while (state.nextDispatch < kDispatchCount && state.status.dispatchCount < kDispatchBudget) {
            const UINT step = state.nextDispatch;
            Cube& output = step == 0 ? target.radiance : step == 1 ? target.diffuse : target.specular;
            const UINT mip = step >= 2 ? step - 2 : 0;
            const UINT size = step == 1 ? kDiffuseResolution : std::max(1u, kResolution >> mip);
            const std::array<UINT, 4> job{step == 0 ? 0u : step == 1 ? 1u : 2u, mip, size, kMipCount};
            output.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commands->SetComputeRoot32BitConstants(1, 4, job.data(), 0);
            commands->SetComputeRootDescriptorTable(
                2, step == 0 ? (cube ? GetTextureResourceSrvGpuHandle(source) : state.nullSrvs[0].gpu)
                             : GetTextureResourceSrvGpuHandle(target.radiance.texture));
            commands->SetComputeRootDescriptorTable(3, panorama ? GetTextureResourceSrvGpuHandle(source)
                                                                : state.nullSrvs[1].gpu);
            commands->SetComputeRootDescriptorTable(4,
                                                    atmosphereSkyView.ptr ? atmosphereSkyView : state.nullSrvs[1].gpu);
            commands->SetComputeRootDescriptorTable(5, output.uavs[mip].gpu);
            commands->SetComputeRootDescriptorTable(6, cloudSky.ptr ? cloudSky : state.nullSrvs[1].gpu);
            commands->Dispatch((size + 7) / 8, (size + 7) / 8, 6);
            output.Transition(commands, kReadState);
            ++state.nextDispatch;
            ++state.status.dispatchCount;
        }
        if (state.nextDispatch == kDispatchCount) {
            target.completedFrame = frameIndex;
            std::swap(state.published, state.pending);
        }
    }
    return state.PublishStatus(identity, epoch, frameIndex);
}

IblEnvironmentSource EnvironmentLightingCache::GetSource() const {
    if (!state_->status.valid)
        return {};
    const auto& set = state_->sets[state_->published];
    return {set.diffuse.texture, set.specular.texture, {}, kMipCount};
}
const EnvironmentLightingCacheStatus& EnvironmentLightingCache::GetStatus() const { return state_->status; }
} // namespace HIKARI::IBL
