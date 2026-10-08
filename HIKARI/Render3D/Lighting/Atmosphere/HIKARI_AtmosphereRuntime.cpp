#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <d3dx12.h>

#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Assets/Shader/HIKARI_VolumetricShaderContract.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::ATMOSPHERE {

using Microsoft::WRL::ComPtr;

namespace {

constexpr DXGI_FORMAT kLutFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr uint32_t kLutThreadGroupSize = 8u;

uint32_t SanitizeDimension(uint32_t value, uint32_t fallback, uint32_t maximum) noexcept {
    return std::clamp(value == 0u ? fallback : value, 2u, maximum);
}

std::array<uint64_t, 3> TransportSignatures(const AtmosphereLutConstants& constants, float observerAltitude,
                                            const MATH::Vec4& cloudShading) {
    const auto hash = [](const auto& data) {
        const uint64_t value = HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis, &data, sizeof(data));
        return value ? value : 1u;
    };
    auto medium = constants;
    medium.rayleighScattering.w = medium.mieScattering.w = 0;
    medium.ozoneParams = {constants.ozoneParams.x, 0, 0, 0};
    medium.sunDirection = medium.sunColorRadiance = medium.groundColor = {0, 0, 0, 0};
    const uint64_t transmission = hash(medium);
    medium.ozoneParams.w = constants.ozoneParams.w;
    medium.groundColor = constants.groundColor;
    // multi LUT の光源方向は texel の軸。色/照度/位相異方性には依存しない。
    // 空の LUT は観測者の高さにも依存する。対数で約 4% 刻みに丸めて、歩く程度の上下では作り直さない。
    const int32_t altitudeStep =
        static_cast<int32_t>(std::lround(std::log2((std::max)(observerAltitude, 1.0f)) * 16.0f));
    // 地面は雲の下にある。雲の陰（主光源の割合 1/64 刻み、雲の下の空の輝度 1/8 段刻み）が変わったら作り直す。
    const auto stops = [](float value) {
        return std::isfinite(value) ? static_cast<int32_t>(std::lround(std::log2((std::max)(value, 1.0e-4f)) * 8.0f))
                                    : 0;
    };
    const std::array<int32_t, 5> cloudStep{
        static_cast<int32_t>(
            std::lround(std::clamp(std::isfinite(cloudShading.w) ? cloudShading.w : 1.0f, 0.0f, 1.0f) * 64.0f)),
        stops(cloudShading.x), stops(cloudShading.y), stops(cloudShading.z), altitudeStep};
    return {transmission, hash(medium), HASH::Fnv1aBytes(hash(constants), cloudStep.data(), sizeof(cloudStep))};
}

bool HasRetireFence(const GFX::Context& context) noexcept {
    return context.deferredReleaseQueue != nullptr && context.currentFrameRetireFenceValue != 0u &&
           (context.frameFence == nullptr ||
            context.frameFence->GetCompletedValue() < context.currentFrameRetireFenceValue);
}

} // namespace

AtmosphereLutOwner::~AtmosphereLutOwner() { Reset(); }

uint64_t AtmosphereLutOwner::GetWorkingSetBytes() const noexcept {
    if (!resourcesReady_)
        return 0;
    return 8ull * (uint64_t(dimensions_.transmittanceWidth) * dimensions_.transmittanceHeight +
                   uint64_t(dimensions_.multiScatteringSize) * dimensions_.multiScatteringSize +
                   uint64_t(dimensions_.skyViewWidth) * dimensions_.skyViewHeight +
                   uint64_t(kAerialPerspectiveResolution) * kAerialPerspectiveResolution * kAerialPerspectiveSlices *
                       kAerialPerspectiveRingSize);
}

bool AtmosphereLutOwner::Configure(const AtmosphereSettings& settings, bool enabled) {
    const AtmosphereLutDimensions next{
        SanitizeDimension(settings.transmittanceLutWidth, 256u, 1024u),
        SanitizeDimension(settings.transmittanceLutHeight, 64u, 256u),
        SanitizeDimension(settings.multiScatteringLutSize, 32u, 64u),
        SanitizeDimension(settings.skyViewLutWidth, 192u, 512u),
        SanitizeDimension(settings.skyViewLutHeight, 108u, 256u),
    };
    const bool dimensionsChanged = dimensions_.transmittanceWidth != next.transmittanceWidth ||
                                   dimensions_.transmittanceHeight != next.transmittanceHeight ||
                                   dimensions_.multiScatteringSize != next.multiScatteringSize ||
                                   dimensions_.skyViewWidth != next.skyViewWidth ||
                                   dimensions_.skyViewHeight != next.skyViewHeight;
    if (dimensionsChanged) {
        Invalidate();
        dimensions_ = next;
    }
    // Parameter edits only require a fresh dispatch. 各 LUT の依存値を Dispatch が比較する。
    configured_ = enabled;
    return configured_;
}

void AtmosphereLutOwner::RetireView(const GFX::Context& context, RENDER3D::RenderResourceView& view,
                                    const char* debugName) {
    if (!view.IsValid()) {
        return;
    }
    const RENDER3D::RenderResourceView retired = view;
    view = {};
    if (HasRetireFence(context)) {
        context.deferredReleaseQueue->Enqueue(
            context.currentFrameRetireFenceValue,
            [retired]() { (void)RENDER3D::ReleaseRenderResourceDescriptor(retired); },
            debugName != nullptr ? debugName : "AtmosphereLutOwner.Descriptor");
        return;
    }
    (void)RENDER3D::ReleaseRenderResourceDescriptor(retired);
}

void AtmosphereLutOwner::RetireResource(const GFX::Context& context, ComPtr<ID3D12Resource>& resource,
                                        const char* debugName) {
    if (!resource) {
        return;
    }
    if (HasRetireFence(context)) {
        GFX::RetireD3D12ObjectForFrame(resource, context,
                                       debugName != nullptr ? debugName : "AtmosphereLutOwner.Resource");
        return;
    }
    resource.Reset();
}

void AtmosphereLutOwner::Invalidate() {
    const GFX::Context context = lastContext_;
    RetireView(context, transmittanceGpu_.srv, "Atmosphere.TransmittanceSRV");
    RetireView(context, transmittanceGpu_.uav, "Atmosphere.TransmittanceUAV");
    RetireView(context, multiScatteringGpu_.srv, "Atmosphere.MultiScatteringSRV");
    RetireView(context, multiScatteringGpu_.uav, "Atmosphere.MultiScatteringUAV");
    RetireView(context, skyViewGpu_.srv, "Atmosphere.SkyViewSRV");
    RetireView(context, skyViewGpu_.uav, "Atmosphere.SkyViewUAV");
    for (GpuLut& volume : aerialPerspectiveGpu_) {
        RetireView(context, volume.srv, "Atmosphere.AerialPerspectiveSRV");
        RetireView(context, volume.uav, "Atmosphere.AerialPerspectiveUAV");
        RetireResource(context, volume.resource, "Atmosphere.AerialPerspective");
        volume = {};
    }
    RetireResource(context, transmittanceGpu_.resource, "Atmosphere.Transmittance");
    RetireResource(context, multiScatteringGpu_.resource, "Atmosphere.MultiScattering");
    RetireResource(context, skyViewGpu_.resource, "Atmosphere.SkyView");
    transmittanceGpu_ = {};
    multiScatteringGpu_ = {};
    skyViewGpu_ = {};
    resourcesReady_ = false;
    gpuLutsReady_ = false;
    lastDispatchSignatures_ = {};
    lastDispatchCount_ = 0u;
}

void AtmosphereLutOwner::Reset() {
    Invalidate();
    // Do not retain a pointer to the frame-deferred queue after the owner
    // has been torn down. The static SkyRenderer state can outlive the
    // RuntimeServices descriptor pool during process shutdown.
    lastContext_ = {};
    configured_ = false;
    pipelineReady_ = false;
    computeRoot_.Reset();
    transmittancePso_.Reset();
    multiScatteringPso_.Reset();
    skyViewPso_.Reset();
    aerialPerspectivePso_.Reset();
}

bool AtmosphereLutOwner::EnsurePipeline(const GFX::Context& context) {
    if (pipelineReady_) {
        return true;
    }
    if (context.device == nullptr || GFX::QueryShaderDeviceCapabilities(context.device).shaderModel < 60u) {
        return false;
    }

    std::array<D3D12_DESCRIPTOR_RANGE, 3u> ranges{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1u;
    ranges[0].BaseShaderRegister = 0u;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[1] = ranges[0];
    ranges[1].BaseShaderRegister = 1u;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[2].NumDescriptors = 1u;
    ranges[2].BaseShaderRegister = 0u;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 5u> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[0].Descriptor.ShaderRegister = 0u;
    for (uint32_t index = 0u; index < 3u; ++index) {
        parameters[index + 1u].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[index + 1u].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[index + 1u].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[index + 1u].DescriptorTable.pDescriptorRanges = &ranges[index];
    }
    // 空気遠近の視点（b1）。LUT 自体の shader は使わない。
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[4].Constants.ShaderRegister = 1u;
    parameters[4].Constants.Num32BitValues = sizeof(AtmosphereViewConstants) / sizeof(uint32_t);

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.ShaderRegister = 0u;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(parameters.size());
    rootDesc.pParameters = parameters.data();
    rootDesc.NumStaticSamplers = 1u;
    rootDesc.pStaticSamplers = &sampler;

    if (FAILED(GFX::CreateRootSignature(context.device, rootDesc, computeRoot_.ReleaseAndGetAddressOf()))) {
        return false;
    }

    namespace SHADER = ASSETS::SHADER;
    using Program = SHADER::VolumetricShaderProgram;
    const auto capabilities = GFX::QueryShaderDeviceCapabilities(context.device);
    const SHADER::ShaderRuntimeDeviceCapabilities caps{capabilities.shaderModel, capabilities.rootSignatureVersion,
                                                       capabilities.featureMask};
    const std::array programs{Program::AtmosphereTransmittance, Program::AtmosphereMultiScattering,
                              Program::AtmosphereSkyView, Program::AtmosphereAerialPerspective};
    std::array<SHADER::ShaderAssetEntryLease, 4u> shaders{};
    for (size_t index = 0; index < programs.size(); ++index) {
        auto resolved =
            SHADER::ResolveVolumetricShaderEntry(programs[index], "Main", SHADER::ShaderStage::Compute, caps);
        if (!resolved) {
            DEBUGLOG::PushRenderError("[Atmosphere] Cooked LUT shader unavailable: " + resolved.diagnostic);
            computeRoot_.Reset();
            return false;
        }
        shaders[index] = std::move(resolved.lease);
    }
    std::array<ComPtr<ID3D12PipelineState>, 4u> pipelines{};
    for (uint32_t index = 0u; index < pipelines.size(); ++index) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = computeRoot_.Get();
        const auto bytecode = shaders[index].GetDxil();
        psoDesc.CS = {bytecode.data(), bytecode.size()};
        if (FAILED(
                context.device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(pipelines[index].GetAddressOf())))) {
            computeRoot_.Reset();
            transmittancePso_.Reset();
            multiScatteringPso_.Reset();
            skyViewPso_.Reset();
            aerialPerspectivePso_.Reset();
            return false;
        }
    }
    transmittancePso_ = std::move(pipelines[0]);
    multiScatteringPso_ = std::move(pipelines[1]);
    skyViewPso_ = std::move(pipelines[2]);
    aerialPerspectivePso_ = std::move(pipelines[3]);
    pipelineReady_ = true;
    return true;
}

bool AtmosphereLutOwner::CreateLut(const GFX::Context& context, GpuLut& lut, uint32_t width, uint32_t height,
                                   uint32_t depth, const wchar_t* debugName) {
    if (context.device == nullptr || context.srvHeap == nullptr || width == 0u || height == 0u) {
        return false;
    }
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = depth > 1u ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>((std::max)(depth, 1u));
    desc.MipLevels = 1u;
    desc.Format = kLutFormat;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(context.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                       nullptr, IID_PPV_ARGS(lut.resource.GetAddressOf())))) {
        return false;
    }
    if (debugName != nullptr) {
        lut.resource->SetName(debugName);
    }
    if (depth > 1u) {
        lut.srv = RENDER3D::AllocateTexture3DSrvDescriptor(lut.resource.Get(), kLutFormat);
        lut.uav = RENDER3D::AllocateTexture3DUavDescriptor(lut.resource.Get(), kLutFormat, 0, 0, depth);
    } else {
        lut.srv = RENDER3D::AllocateTexture2DSrvDescriptor(lut.resource.Get(), kLutFormat, 0u, 1u);
        lut.uav = RENDER3D::AllocateTexture2DUavDescriptor(lut.resource.Get(), kLutFormat, 0u);
    }
    if (!lut.srv.IsValid() || !lut.uav.IsValid()) {
        RetireView(context, lut.srv, "Atmosphere.LutSRV.Failed");
        RetireView(context, lut.uav, "Atmosphere.LutUAV.Failed");
        RetireResource(context, lut.resource, "Atmosphere.Lut.Failed");
        return false;
    }
    lut.state = D3D12_RESOURCE_STATE_COMMON;
    return true;
}

bool AtmosphereLutOwner::EnsureGpuResources(const GFX::Context& context) {
    lastContext_ = context;
    if (!configured_ || context.device == nullptr || context.cmdList == nullptr || context.srvHeap == nullptr) {
        return false;
    }
    const bool aerialPerspectiveReady = std::all_of(aerialPerspectiveGpu_.begin(), aerialPerspectiveGpu_.end(),
                                                    [](const GpuLut& volume) { return volume.srv.IsValid(); });
    if (resourcesReady_ && transmittanceGpu_.resource != nullptr && multiScatteringGpu_.resource != nullptr &&
        skyViewGpu_.resource != nullptr && transmittanceGpu_.srv.IsValid() && multiScatteringGpu_.srv.IsValid() &&
        skyViewGpu_.srv.IsValid() && aerialPerspectiveReady) {
        return true;
    }
    if (!EnsurePipeline(context) ||
        !CreateLut(context, transmittanceGpu_, dimensions_.transmittanceWidth, dimensions_.transmittanceHeight, 1u,
                   L"HIKARI.Atmosphere.Transmittance") ||
        !CreateLut(context, multiScatteringGpu_, dimensions_.multiScatteringSize, dimensions_.multiScatteringSize, 1u,
                   L"HIKARI.Atmosphere.MultiScattering") ||
        !CreateLut(context, skyViewGpu_, dimensions_.skyViewWidth, dimensions_.skyViewHeight, 1u,
                   L"HIKARI.Atmosphere.SkyView")) {
        Invalidate();
        return false;
    }
    for (GpuLut& volume : aerialPerspectiveGpu_) {
        if (!CreateLut(context, volume, kAerialPerspectiveResolution, kAerialPerspectiveResolution,
                       kAerialPerspectiveSlices, L"HIKARI.Atmosphere.AerialPerspective")) {
            Invalidate();
            return false;
        }
    }
    resourcesReady_ = true;
    gpuLutsReady_ = false;
    return true;
}

void AtmosphereLutOwner::Transition(ID3D12GraphicsCommandList* commandList, GpuLut& lut, D3D12_RESOURCE_STATES next) {
    if (commandList == nullptr || lut.resource == nullptr || lut.state == next) {
        return;
    }
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(lut.resource.Get(), lut.state, next);
    commandList->ResourceBarrier(1u, &barrier);
    lut.state = next;
}

bool AtmosphereLutOwner::Dispatch(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS constantsGpu,
                                  const AtmosphereLutConstants& constants, const AtmosphereViewConstants& view) {
    lastDispatchCount_ = 0;
    lastContext_ = context;
    if (!EnsureGpuResources(context) || constantsGpu == 0u || context.cmdList == nullptr || !computeRoot_ ||
        !transmittancePso_ || !multiScatteringPso_ || !skyViewPso_) {
        return false;
    }
    const auto signatures = TransportSignatures(
        constants, std::isfinite(view.cameraPreExposure.y) ? view.cameraPreExposure.y : 1.0f, view.cloudShading);
    if (gpuLutsReady_ && lastDispatchSignatures_ == signatures) {
        return true;
    }

    ID3D12GraphicsCommandList* commandList = context.cmdList;
    ID3D12DescriptorHeap* heaps[] = {context.srvHeap};
    commandList->SetDescriptorHeaps(1u, heaps);
    commandList->SetComputeRootSignature(computeRoot_.Get());

    if (!gpuLutsReady_ || lastDispatchSignatures_[0] != signatures[0]) {
        Transition(commandList, transmittanceGpu_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetPipelineState(transmittancePso_.Get());
        commandList->SetComputeRootConstantBufferView(0u, constantsGpu);
        commandList->SetComputeRootDescriptorTable(3u, transmittanceGpu_.uav.gpu);
        commandList->Dispatch((dimensions_.transmittanceWidth + kLutThreadGroupSize - 1u) / kLutThreadGroupSize,
                              (dimensions_.transmittanceHeight + kLutThreadGroupSize - 1u) / kLutThreadGroupSize, 1u);
        const auto transUavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(transmittanceGpu_.resource.Get());
        commandList->ResourceBarrier(1u, &transUavBarrier);
        Transition(commandList, transmittanceGpu_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ++lastDispatchCount_;
    }

    if (!gpuLutsReady_ || lastDispatchSignatures_[1] != signatures[1]) {
        Transition(commandList, multiScatteringGpu_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetPipelineState(multiScatteringPso_.Get());
        commandList->SetComputeRootConstantBufferView(0u, constantsGpu);
        commandList->SetComputeRootDescriptorTable(1u, transmittanceGpu_.srv.gpu);
        commandList->SetComputeRootDescriptorTable(3u, multiScatteringGpu_.uav.gpu);
        commandList->Dispatch((dimensions_.multiScatteringSize + kLutThreadGroupSize - 1u) / kLutThreadGroupSize,
                              (dimensions_.multiScatteringSize + kLutThreadGroupSize - 1u) / kLutThreadGroupSize, 1u);
        const auto multiUavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(multiScatteringGpu_.resource.Get());
        commandList->ResourceBarrier(1u, &multiUavBarrier);
        Transition(commandList, multiScatteringGpu_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ++lastDispatchCount_;
    }

    Transition(commandList, skyViewGpu_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(skyViewPso_.Get());
    commandList->SetComputeRootConstantBufferView(0u, constantsGpu);
    commandList->SetComputeRootDescriptorTable(1u, transmittanceGpu_.srv.gpu);
    commandList->SetComputeRootDescriptorTable(2u, multiScatteringGpu_.srv.gpu);
    commandList->SetComputeRootDescriptorTable(3u, skyViewGpu_.uav.gpu);
    commandList->SetComputeRoot32BitConstants(4u, sizeof(view) / sizeof(uint32_t), &view, 0u);
    commandList->Dispatch((dimensions_.skyViewWidth + kLutThreadGroupSize - 1u) / kLutThreadGroupSize,
                          (dimensions_.skyViewHeight + kLutThreadGroupSize - 1u) / kLutThreadGroupSize, 1u);
    const auto skyUavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(skyViewGpu_.resource.Get());
    commandList->ResourceBarrier(1u, &skyUavBarrier);
    Transition(commandList, skyViewGpu_, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

    ++lastDispatchCount_;
    lastDispatchSignatures_ = signatures;
    gpuLutsReady_ = true;
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE AtmosphereLutOwner::DispatchAerialPerspective(const GFX::Context& context,
                                                                          D3D12_GPU_VIRTUAL_ADDRESS constantsGpu,
                                                                          const AtmosphereViewConstants& view) {
    GpuLut& volume = aerialPerspectiveGpu_[aerialPerspectiveCursor_];
    if (!gpuLutsReady_ || !aerialPerspectivePso_ || constantsGpu == 0u || context.cmdList == nullptr ||
        volume.resource == nullptr) {
        return {};
    }
    aerialPerspectiveCursor_ = (aerialPerspectiveCursor_ + 1u) % kAerialPerspectiveRingSize;
    ID3D12GraphicsCommandList* commandList = context.cmdList;
    ID3D12DescriptorHeap* heaps[] = {context.srvHeap};
    commandList->SetDescriptorHeaps(1u, heaps);
    commandList->SetComputeRootSignature(computeRoot_.Get());
    Transition(commandList, volume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(aerialPerspectivePso_.Get());
    commandList->SetComputeRootConstantBufferView(0u, constantsGpu);
    commandList->SetComputeRootDescriptorTable(1u, transmittanceGpu_.srv.gpu);
    commandList->SetComputeRootDescriptorTable(2u, multiScatteringGpu_.srv.gpu);
    commandList->SetComputeRootDescriptorTable(3u, volume.uav.gpu);
    commandList->SetComputeRoot32BitConstants(4u, sizeof(view) / sizeof(uint32_t), &view, 0u);
    commandList->Dispatch((kAerialPerspectiveResolution + kLutThreadGroupSize - 1u) / kLutThreadGroupSize,
                          (kAerialPerspectiveResolution + kLutThreadGroupSize - 1u) / kLutThreadGroupSize, 1u);
    const auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(volume.resource.Get());
    commandList->ResourceBarrier(1u, &uavBarrier);
    Transition(commandList, volume, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return volume.srv.gpu;
}

} // namespace HIKARI::ATMOSPHERE
