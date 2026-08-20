#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <d3dx12.h>

#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::ATMOSPHERE {

    using Microsoft::WRL::ComPtr;

    namespace {

        constexpr DXGI_FORMAT kLutFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr uint32_t kLutThreadGroupSize = 8u;

        uint32_t SanitizeDimension(uint32_t value, uint32_t fallback) noexcept {
            return std::clamp(value == 0u ? fallback : value, 1u, 4096u);
        }

        bool EqualFloat(float lhs, float rhs) noexcept {
            return lhs == rhs ||
                (!std::isfinite(lhs) && !std::isfinite(rhs));
        }

        bool EqualVec3(const MATH::Vec3& lhs, const MATH::Vec3& rhs) noexcept {
            return EqualFloat(lhs.x, rhs.x) &&
                EqualFloat(lhs.y, rhs.y) &&
                EqualFloat(lhs.z, rhs.z);
        }

        bool EqualSettings(
            const AtmosphereSettings& lhs,
            const AtmosphereSettings& rhs) noexcept {
            return lhs.enabled == rhs.enabled &&
                EqualFloat(lhs.planetRadius, rhs.planetRadius) &&
                EqualFloat(lhs.atmosphereHeight, rhs.atmosphereHeight) &&
                EqualFloat(lhs.rayleighScaleHeight, rhs.rayleighScaleHeight) &&
                EqualFloat(lhs.mieScaleHeight, rhs.mieScaleHeight) &&
                EqualVec3(lhs.rayleighScattering, rhs.rayleighScattering) &&
                EqualVec3(lhs.mieScattering, rhs.mieScattering) &&
                EqualVec3(lhs.ozoneAbsorption, rhs.ozoneAbsorption) &&
                EqualFloat(lhs.ozoneCenterHeight, rhs.ozoneCenterHeight) &&
                EqualFloat(lhs.ozoneWidth, rhs.ozoneWidth) &&
                EqualFloat(lhs.mieAnisotropy, rhs.mieAnisotropy) &&
                EqualFloat(lhs.sunIlluminance, rhs.sunIlluminance) &&
                 EqualFloat(lhs.sunExposure, rhs.sunExposure) &&
                 EqualFloat(lhs.groundAlbedo, rhs.groundAlbedo) &&
                 EqualFloat(lhs.aerialPerspectiveStrength, rhs.aerialPerspectiveStrength) &&
                 EqualFloat(lhs.sunAngularRadius, rhs.sunAngularRadius) &&
                lhs.transmittanceLutWidth == rhs.transmittanceLutWidth &&
                lhs.transmittanceLutHeight == rhs.transmittanceLutHeight &&
                 lhs.multiScatteringLutSize == rhs.multiScatteringLutSize &&
                 lhs.skyViewLutWidth == rhs.skyViewLutWidth &&
                 lhs.skyViewLutHeight == rhs.skyViewLutHeight;
        }

        bool HasRetireFence(const GFX::Context& context) noexcept {
            return context.deferredReleaseQueue != nullptr &&
                context.currentFrameRetireFenceValue != 0u &&
                (context.frameFence == nullptr ||
                    context.frameFence->GetCompletedValue() <
                        context.currentFrameRetireFenceValue);
        }

    } // namespace

    AtmosphereLutOwner::~AtmosphereLutOwner() {
        Reset();
    }

    bool AtmosphereLutOwner::Configure(const AtmosphereSettings& settings) {
        const AtmosphereLutDimensions next{
            SanitizeDimension(settings.transmittanceLutWidth, 256u),
            SanitizeDimension(settings.transmittanceLutHeight, 64u),
            SanitizeDimension(settings.multiScatteringLutSize, 32u),
            SanitizeDimension(settings.skyViewLutWidth, 192u),
            SanitizeDimension(settings.skyViewLutHeight, 108u),
        };
        const bool dimensionsChanged =
            dimensions_.transmittanceWidth != next.transmittanceWidth ||
            dimensions_.transmittanceHeight != next.transmittanceHeight ||
            dimensions_.multiScatteringSize != next.multiScatteringSize ||
            dimensions_.skyViewWidth != next.skyViewWidth ||
            dimensions_.skyViewHeight != next.skyViewHeight;
        const bool settingsChanged = !hasSettings_ ||
            !EqualSettings(settings_, settings);
        if (dimensionsChanged) {
            Invalidate();
            dimensions_ = next;
        } else if (settingsChanged) {
            // Parameter edits only require a fresh dispatch. Recreating all
            // three textures and six descriptors for every Inspector drag
            // can exhaust the deferred descriptor pool while the GPU still
            // owns the preceding frames.
            lastDispatchSignature_ = 0u;
            gpuLutsReady_ = false;
        }
        if (settingsChanged || dimensionsChanged) {
            settings_ = settings;
            hasSettings_ = true;
        }
        configured_ = settings.enabled;
        if (!configured_) {
            gpuLutsReady_ = false;
        }
        return configured_;
    }

    void AtmosphereLutOwner::RetireView(
        const GFX::Context& context,
        RENDER3D::RenderResourceView& view,
        const char* debugName) {
        if (!view.IsValid()) {
            return;
        }
        const RENDER3D::RenderResourceView retired = view;
        view = {};
        if (HasRetireFence(context)) {
            context.deferredReleaseQueue->Enqueue(
                context.currentFrameRetireFenceValue,
                [retired]() {
                    (void)RENDER3D::ReleaseRenderResourceDescriptor(retired);
                },
                debugName != nullptr ? debugName :
                    "AtmosphereLutOwner.Descriptor");
            return;
        }
        (void)RENDER3D::ReleaseRenderResourceDescriptor(retired);
    }

    void AtmosphereLutOwner::RetireResource(
        const GFX::Context& context,
        ComPtr<ID3D12Resource>& resource,
        const char* debugName) {
        if (!resource) {
            return;
        }
        if (HasRetireFence(context)) {
            GFX::RetireD3D12ObjectForFrame(
                resource,
                context,
                debugName != nullptr ? debugName :
                    "AtmosphereLutOwner.Resource");
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
        RetireResource(context, transmittanceGpu_.resource, "Atmosphere.Transmittance");
        RetireResource(context, multiScatteringGpu_.resource, "Atmosphere.MultiScattering");
        RetireResource(context, skyViewGpu_.resource, "Atmosphere.SkyView");
        transmittanceGpu_ = {};
        multiScatteringGpu_ = {};
        skyViewGpu_ = {};
        resourcesReady_ = false;
        gpuLutsReady_ = false;
        lastDispatchSignature_ = 0u;
    }

    void AtmosphereLutOwner::Reset() {
        Invalidate();
        // Do not retain a pointer to the frame-deferred queue after the owner
        // has been torn down. The static SkyRenderer state can outlive the
        // RuntimeServices descriptor pool during process shutdown.
        lastContext_ = {};
        configured_ = false;
        hasSettings_ = false;
        pipelineReady_ = false;
        computeRoot_.Reset();
        transmittancePso_.Reset();
        multiScatteringPso_.Reset();
        skyViewPso_.Reset();
        transmittanceCs_.Reset();
        multiScatteringCs_.Reset();
        skyViewCs_.Reset();
    }

    bool AtmosphereLutOwner::EnsurePipeline(const GFX::Context& context) {
        if (pipelineReady_) {
            return true;
        }
        if (context.device == nullptr ||
            !GFX::SupportsShaderModel6(context.device)) {
            return false;
        }

        std::array<D3D12_DESCRIPTOR_RANGE, 3u> ranges{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1u;
        ranges[0].BaseShaderRegister = 0u;
        ranges[0].OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        ranges[1] = ranges[0];
        ranges[1].BaseShaderRegister = 1u;
        ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[2].NumDescriptors = 1u;
        ranges[2].BaseShaderRegister = 0u;
        ranges[2].OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        std::array<D3D12_ROOT_PARAMETER, 4u> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[0].Descriptor.ShaderRegister = 0u;
        for (uint32_t index = 0u; index < 3u; ++index) {
            parameters[index + 1u].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[index + 1u].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            parameters[index + 1u].DescriptorTable.NumDescriptorRanges = 1u;
            parameters[index + 1u].DescriptorTable.pDescriptorRanges =
                &ranges[index];
        }

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

        ComPtr<ID3DBlob> signature{};
        ComPtr<ID3DBlob> errors{};
        if (FAILED(D3D12SerializeRootSignature(
                &rootDesc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                signature.GetAddressOf(),
                errors.GetAddressOf())) ||
            FAILED(context.device->CreateRootSignature(
                0u,
                signature->GetBufferPointer(),
                signature->GetBufferSize(),
                IID_PPV_ARGS(computeRoot_.ReleaseAndGetAddressOf())))) {
            return false;
        }

        if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_AtmosphereTransmittanceCS.hlsl",
                "main",
                GFX::ShaderStage::Compute,
                transmittanceCs_.ReleaseAndGetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_AtmosphereMultiScatteringCS.hlsl",
                "main",
                GFX::ShaderStage::Compute,
                multiScatteringCs_.ReleaseAndGetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_AtmosphereSkyViewCS.hlsl",
                "main",
                GFX::ShaderStage::Compute,
                skyViewCs_.ReleaseAndGetAddressOf())) {
            computeRoot_.Reset();
            transmittanceCs_.Reset();
            multiScatteringCs_.Reset();
            skyViewCs_.Reset();
            return false;
        }

        const std::array<ID3DBlob*, 3u> blobs{
            transmittanceCs_.Get(),
            multiScatteringCs_.Get(),
            skyViewCs_.Get(),
        };
        std::array<ComPtr<ID3D12PipelineState>, 3u> pipelines{};
        for (uint32_t index = 0u; index < pipelines.size(); ++index) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = computeRoot_.Get();
            psoDesc.CS = {
                blobs[index]->GetBufferPointer(),
                blobs[index]->GetBufferSize()
            };
            if (FAILED(context.device->CreateComputePipelineState(
                    &psoDesc,
                    IID_PPV_ARGS(pipelines[index].GetAddressOf())))) {
                computeRoot_.Reset();
                transmittancePso_.Reset();
                multiScatteringPso_.Reset();
                skyViewPso_.Reset();
                transmittanceCs_.Reset();
                multiScatteringCs_.Reset();
                skyViewCs_.Reset();
                return false;
            }
        }
        transmittancePso_ = std::move(pipelines[0]);
        multiScatteringPso_ = std::move(pipelines[1]);
        skyViewPso_ = std::move(pipelines[2]);
        pipelineReady_ = true;
        return true;
    }

    bool AtmosphereLutOwner::CreateLut(
        const GFX::Context& context,
        GpuLut& lut,
        uint32_t width,
        uint32_t height,
        const wchar_t* debugName) {
        if (context.device == nullptr ||
            context.srvHeap == nullptr ||
            width == 0u || height == 0u) {
            return false;
        }
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1u;
        desc.MipLevels = 1u;
        desc.Format = kLutFormat;
        desc.SampleDesc.Count = 1u;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(context.device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(lut.resource.GetAddressOf())))) {
            return false;
        }
        if (debugName != nullptr) {
            lut.resource->SetName(debugName);
        }
        lut.srv = RENDER3D::AllocateTexture2DSrvDescriptor(
            lut.resource.Get(),
            kLutFormat,
            0u,
            1u);
        lut.uav = RENDER3D::AllocateTexture2DUavDescriptor(
            lut.resource.Get(),
            kLutFormat,
            0u);
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
        if (!configured_ || context.device == nullptr ||
            context.cmdList == nullptr || context.srvHeap == nullptr) {
            return false;
        }
        if (resourcesReady_ &&
            transmittanceGpu_.resource != nullptr &&
            multiScatteringGpu_.resource != nullptr &&
            skyViewGpu_.resource != nullptr &&
            transmittanceGpu_.srv.IsValid() &&
            multiScatteringGpu_.srv.IsValid() &&
            skyViewGpu_.srv.IsValid()) {
            return true;
        }
        if (!EnsurePipeline(context) ||
            !CreateLut(
                context,
                transmittanceGpu_,
                dimensions_.transmittanceWidth,
                dimensions_.transmittanceHeight,
                L"HIKARI.Atmosphere.Transmittance") ||
            !CreateLut(
                context,
                multiScatteringGpu_,
                dimensions_.multiScatteringSize,
                dimensions_.multiScatteringSize,
                L"HIKARI.Atmosphere.MultiScattering") ||
            !CreateLut(
                context,
                skyViewGpu_,
                dimensions_.skyViewWidth,
                dimensions_.skyViewHeight,
                L"HIKARI.Atmosphere.SkyView")) {
            Invalidate();
            return false;
        }
        resourcesReady_ = true;
        gpuLutsReady_ = false;
        return true;
    }

    void AtmosphereLutOwner::Transition(
        ID3D12GraphicsCommandList* commandList,
        GpuLut& lut,
        D3D12_RESOURCE_STATES next) {
        if (commandList == nullptr || lut.resource == nullptr ||
            lut.state == next) {
            return;
        }
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            lut.resource.Get(),
            lut.state,
            next);
        commandList->ResourceBarrier(1u, &barrier);
        lut.state = next;
    }

    bool AtmosphereLutOwner::Dispatch(
        const GFX::Context& context,
        D3D12_GPU_VIRTUAL_ADDRESS constantsGpu,
        uint64_t parameterSignature) {
        lastContext_ = context;
        if (!EnsureGpuResources(context) || constantsGpu == 0u ||
            context.cmdList == nullptr ||
            !computeRoot_ || !transmittancePso_ ||
            !multiScatteringPso_ || !skyViewPso_) {
            return false;
        }
        if (gpuLutsReady_ && lastDispatchSignature_ == parameterSignature) {
            return true;
        }

        ID3D12GraphicsCommandList* commandList = context.cmdList;
        ID3D12DescriptorHeap* heaps[] = { context.srvHeap };
        commandList->SetDescriptorHeaps(1u, heaps);
        commandList->SetComputeRootSignature(computeRoot_.Get());

        Transition(
            commandList,
            transmittanceGpu_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetPipelineState(transmittancePso_.Get());
        commandList->SetComputeRootConstantBufferView(0u, constantsGpu);
        commandList->SetComputeRootDescriptorTable(
            3u,
            transmittanceGpu_.uav.gpu);
        commandList->Dispatch(
            (dimensions_.transmittanceWidth + kLutThreadGroupSize - 1u) /
                kLutThreadGroupSize,
            (dimensions_.transmittanceHeight + kLutThreadGroupSize - 1u) /
                kLutThreadGroupSize,
            1u);
        const auto transUavBarrier =
            CD3DX12_RESOURCE_BARRIER::UAV(transmittanceGpu_.resource.Get());
        commandList->ResourceBarrier(1u, &transUavBarrier);
        Transition(
            commandList,
            transmittanceGpu_,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        Transition(
            commandList,
            multiScatteringGpu_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetPipelineState(multiScatteringPso_.Get());
        commandList->SetComputeRootConstantBufferView(0u, constantsGpu);
        commandList->SetComputeRootDescriptorTable(
            1u,
            transmittanceGpu_.srv.gpu);
        commandList->SetComputeRootDescriptorTable(
            3u,
            multiScatteringGpu_.uav.gpu);
        commandList->Dispatch(
            (dimensions_.multiScatteringSize + kLutThreadGroupSize - 1u) /
                kLutThreadGroupSize,
            (dimensions_.multiScatteringSize + kLutThreadGroupSize - 1u) /
                kLutThreadGroupSize,
            1u);
        const auto multiUavBarrier =
            CD3DX12_RESOURCE_BARRIER::UAV(multiScatteringGpu_.resource.Get());
        commandList->ResourceBarrier(1u, &multiUavBarrier);
        Transition(
            commandList,
            multiScatteringGpu_,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        Transition(
            commandList,
            skyViewGpu_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetPipelineState(skyViewPso_.Get());
        commandList->SetComputeRootConstantBufferView(0u, constantsGpu);
        commandList->SetComputeRootDescriptorTable(
            1u,
            transmittanceGpu_.srv.gpu);
        commandList->SetComputeRootDescriptorTable(
            2u,
            multiScatteringGpu_.srv.gpu);
        commandList->SetComputeRootDescriptorTable(
            3u,
            skyViewGpu_.uav.gpu);
        commandList->Dispatch(
            (dimensions_.skyViewWidth + kLutThreadGroupSize - 1u) /
                kLutThreadGroupSize,
            (dimensions_.skyViewHeight + kLutThreadGroupSize - 1u) /
                kLutThreadGroupSize,
            1u);
        const auto skyUavBarrier =
            CD3DX12_RESOURCE_BARRIER::UAV(skyViewGpu_.resource.Get());
        commandList->ResourceBarrier(1u, &skyUavBarrier);
        Transition(
            commandList,
            skyViewGpu_,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        lastDispatchSignature_ = parameterSignature;
        gpuLutsReady_ = true;
        return true;
    }

} // namespace HIKARI::ATMOSPHERE
