#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Color/HIKARI_AutomaticExposure.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>

#include "RenderContracts/Shader/HIKARI_AutomaticExposureShaderContract.h"
#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"

namespace HIKARI::RENDER3D::COLOR {

using Microsoft::WRL::ComPtr;

namespace {
constexpr uint32_t kDescriptorCount = 2u;
constexpr uint32_t kMeasurementValueCount = 4u;
constexpr uint64_t kMeasurementBytes = sizeof(float) * kMeasurementValueCount;

struct MeasurementResult {
    float meteredLuminance = 0.18f;
    float windowWeight = 0.0f;
    float totalWeight = 0.0f;
    float highlightLuminance = 0.18f;
};

bool IsValidLuminance(float value) noexcept { return std::isfinite(value) && value > 1e-6f && value < 1e8f; }
} // namespace

struct AutomaticExposureMeter::Impl {
    struct ReadbackSlot {
        ComPtr<ID3D12Resource> buffer{};
        uint64_t fenceValue = 0;
        uint64_t frameIndex = 0;
        bool discard = false;
    };

    GFX::Context context{};
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipelineState{};
    ASSETS::SHADER::ShaderAssetEntryLease shaderLease{};
    ComPtr<ID3D12DescriptorHeap> descriptorHeap{};
    ComPtr<ID3D12Resource> measurementBuffer{};
    std::array<ReadbackSlot, GFX::kFrameResourceCount> readbackSlots{};
    D3D12_RESOURCE_STATES measurementState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    size_t nextReadbackSlot = 0;
    float latestMeteredLuminance = 0.18f;
    float latestHighlightLuminance = 0.18f;
    float currentExposure = 1.0f;
    float currentPreExposure = 0.0f;
    uint64_t latestMeasuredFrame = 0;
    bool latestMeasurementValid = false;
    bool automaticModeActive = false;
    // 自動露出を始めた直後は、最初の有効な測光で目標へ直接合わせる（手動の値から長く慣らさない）。
    bool snapToFirstMeasurement = false;
    AutomaticExposureDebugState debug{};

    bool IsInitialized() const noexcept {
        return rootSignature != nullptr && pipelineState != nullptr && static_cast<bool>(shaderLease) &&
               descriptorHeap != nullptr && measurementBuffer != nullptr && readbackSlots[0].buffer != nullptr;
    }

    void ReleaseResources() {
        GFX::RetireD3D12ObjectForFrame(rootSignature, context, "AutomaticExposure.RootSignature");
        GFX::RetireD3D12ObjectForFrame(pipelineState, context, "AutomaticExposure.PipelineState");
        GFX::RetireD3D12ObjectForFrame(descriptorHeap, context, "AutomaticExposure.DescriptorHeap");
        GFX::RetireD3D12ObjectForFrame(measurementBuffer, context, "AutomaticExposure.MeasurementBuffer");
        shaderLease = {};
        for (ReadbackSlot& slot : readbackSlots) {
            GFX::RetireD3D12ObjectForFrame(slot.buffer, context, "AutomaticExposure.Readback");
            slot = {};
        }
        measurementState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        nextReadbackSlot = 0;
        latestMeasurementValid = false;
        latestMeasuredFrame = 0;
        debug.initialized = false;
        debug.measurementQueued = false;
    }

    bool CreateRootSignature(ID3D12Device* device) {
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].RegisterSpace = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].RegisterSpace = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;

        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[0].DescriptorTable.NumDescriptorRanges = 2;
        parameters[0].DescriptorTable.pDescriptorRanges = ranges;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].Constants.ShaderRegister = 0;
        parameters[1].Constants.RegisterSpace = 0;
        parameters[1].Constants.Num32BitValues = 4;

        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = static_cast<UINT>(std::size(parameters));
        description.pParameters = parameters;
        description.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        HRESULT createResult = GFX::CreateRootSignature(device, description, rootSignature.GetAddressOf());
        if (!HIKARI_DX_CHECK(createResult, "AutomaticExposure::CreateRootSignature")) {
            return false;
        }
        GFX::SetD3D12Name(rootSignature.Get(), L"Automatic Exposure Root Signature");
        return true;
    }

    bool CreatePipeline(ID3D12Device* device) {
        const GFX::ShaderDeviceCapabilities capabilities = GFX::QueryShaderDeviceCapabilities(device);
        const ASSETS::SHADER::ShaderAssetContractExpectation contract =
            ASSETS::SHADER::MakeAutomaticExposureShaderContract(
                {capabilities.shaderModel, capabilities.rootSignatureVersion, capabilities.featureMask});
        ASSETS::SHADER::ShaderAssetResolveResult resolved = ASSETS::SHADER::ResolveBuiltinShaderEntry(
            "HIKARI.EngineShader.Color_AutomaticExposureCS", {"Main", ASSETS::SHADER::ShaderStage::Compute, "cs_6_0"},
            contract);
        const ASSETS::SHADER::ShaderCookedArtifact* artifact = resolved ? resolved.lease.GetArtifact() : nullptr;
        const ASSETS::SHADER::ShaderCookedEntry* entry = resolved ? resolved.lease.GetEntry() : nullptr;
        std::string diagnostic{};
        if (!resolved || artifact == nullptr || entry == nullptr ||
            !ASSETS::SHADER::ValidateAutomaticExposureShaderArtifact(*artifact, *entry, diagnostic)) {
            return false;
        }
        const std::span<const uint8_t> shader = resolved.lease.GetDxil();
        if (shader.empty()) {
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature.Get();
        description.CS = {shader.data(), shader.size()};
        const HRESULT result =
            device->CreateComputePipelineState(&description, IID_PPV_ARGS(pipelineState.GetAddressOf()));
        if (!HIKARI_DX_CHECK(result, "AutomaticExposure::CreatePipelineState")) {
            return false;
        }
        GFX::SetD3D12Name(pipelineState.Get(), L"Automatic Exposure Meter PSO");
        shaderLease = std::move(resolved.lease);
        return true;
    }

    bool CreateResources(ID3D12Device* device) {
        D3D12_DESCRIPTOR_HEAP_DESC descriptorDescription{};
        descriptorDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        descriptorDescription.NumDescriptors = kDescriptorCount * GFX::kFrameResourceCount;
        descriptorDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!HIKARI_DX_CHECK(
                device->CreateDescriptorHeap(&descriptorDescription, IID_PPV_ARGS(descriptorHeap.GetAddressOf())),
                "AutomaticExposure::CreateDescriptorHeap")) {
            return false;
        }
        GFX::SetD3D12Name(descriptorHeap.Get(), L"Automatic Exposure Descriptor Heap");

        const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto measurementDescription =
            CD3DX12_RESOURCE_DESC::Buffer(kMeasurementBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                                 device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &measurementDescription,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, GFX::VIDEO_MEMORY::Owner::Post,
                                 IID_PPV_ARGS(measurementBuffer.GetAddressOf())),
                             "AutomaticExposure::CreateMeasurementBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(measurementBuffer.Get(), L"Automatic Exposure Measurement Buffer");

        const auto readbackHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const auto readbackDescription = CD3DX12_RESOURCE_DESC::Buffer(kMeasurementBytes);
        for (ReadbackSlot& slot : readbackSlots) {
            if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                                     device, &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDescription,
                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr, GFX::VIDEO_MEMORY::Owner::Post,
                                     IID_PPV_ARGS(slot.buffer.GetAddressOf())),
                                 "AutomaticExposure::CreateReadbackBuffer")) {
                return false;
            }
            GFX::SetD3D12Name(slot.buffer.Get(), L"Automatic Exposure Readback");
        }

        const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.FirstElement = 0;
        uav.Buffer.NumElements = 1;
        uav.Buffer.StructureByteStride = static_cast<UINT>(kMeasurementBytes);
        for (UINT index = 0; index < GFX::kFrameResourceCount; ++index) {
            const CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                          index * kDescriptorCount + 1u, descriptorSize);
            device->CreateUnorderedAccessView(measurementBuffer.Get(), nullptr, &uav, uavHandle);
        }
        return true;
    }

    bool EnsureInitialized() {
        if (IsInitialized()) {
            return true;
        }
        ReleaseResources();
        if (context.device == nullptr || context.deviceGeneration == 0u || !CreateRootSignature(context.device) ||
            !CreatePipeline(context.device) || !CreateResources(context.device)) {
            ReleaseResources();
            return false;
        }
        debug.initialized = true;
        return true;
    }

    void CollectMeasurements() {
        for (ReadbackSlot& slot : readbackSlots) {
            if (slot.buffer == nullptr || slot.fenceValue == 0 || !GFX::IsCurrentFenceComplete(slot.fenceValue)) {
                continue;
            }

            const D3D12_RANGE readRange{0, static_cast<SIZE_T>(kMeasurementBytes)};
            void* mapped = nullptr;
            if (!slot.discard && SUCCEEDED(slot.buffer->Map(0, &readRange, &mapped)) && mapped != nullptr) {
                MeasurementResult result{};
                std::memcpy(&result, mapped, sizeof(result));
                const D3D12_RANGE writeRange{0, 0};
                slot.buffer->Unmap(0, &writeRange);
                if (IsValidLuminance(result.meteredLuminance) && result.windowWeight > 0.0f &&
                    (!latestMeasurementValid || slot.frameIndex > latestMeasuredFrame)) {
                    latestMeteredLuminance = result.meteredLuminance;
                    latestHighlightLuminance = IsValidLuminance(result.highlightLuminance) ? result.highlightLuminance
                                                                                           : result.meteredLuminance;
                    latestMeasuredFrame = slot.frameIndex;
                    latestMeasurementValid = true;
                }
            }
            slot.fenceValue = 0;
        }
    }
};

AutomaticExposureMeter::AutomaticExposureMeter() : impl_(std::make_unique<Impl>()) {}

AutomaticExposureMeter::~AutomaticExposureMeter() { Shutdown(); }

void AutomaticExposureMeter::UpdateContext(const GFX::Context& context) {
    if (impl_->context.device != nullptr &&
        (impl_->context.device != context.device || impl_->context.deviceGeneration != context.deviceGeneration)) {
        impl_->ReleaseResources();
    }
    impl_->context = context;
}

void AutomaticExposureMeter::Shutdown() {
    if (!impl_) {
        return;
    }
    impl_->ReleaseResources();
    impl_->context = {};
}

ExposureFrameState AutomaticExposureMeter::BeginFrame(const ExposureSettings& settings,
                                                      const ExposureIllumination& illumination, float deltaTime) {
    impl_->CollectMeasurements();
    ExposureFrameState state{};
    const float manualExposure = ResolveManualExposure(settings);
    if (settings.mode != ExposureMode::Automatic) {
        impl_->automaticModeActive = false;
        impl_->latestMeasurementValid = false;
        // 旧い測光は切り替え後に採用しない。GPU が完了するまで slot と descriptor は再使用しない。
        for (auto& slot : impl_->readbackSlots)
            slot.discard = true;
        state = BuildManualExposureFrame(manualExposure, impl_->currentPreExposure);
        impl_->debug.measurementValid = impl_->latestMeasurementValid;
        impl_->debug.meteredLuminance = impl_->latestMeteredLuminance;
    } else {
        if (!impl_->automaticModeActive) {
            impl_->automaticModeActive = true;
            impl_->snapToFirstMeasurement = impl_->currentPreExposure <= 0.0f;
        }
        // 固定解除では現在の露出から順応を再開する。測光待ちに屋外の推定値へ飛ぶと、室内で一瞬暗転する。
        if (!impl_->latestMeasurementValid && !impl_->snapToFirstMeasurement) {
            state = BuildManualExposureFrame(impl_->currentExposure, impl_->currentPreExposure);
            state.mode = ExposureMode::Automatic;
        } else {
            state = BuildAutomaticExposureFrame(settings, illumination, impl_->latestMeteredLuminance,
                                                impl_->latestHighlightLuminance, impl_->currentExposure,
                                                impl_->currentPreExposure, deltaTime, impl_->latestMeasurementValid);
        }
        if (impl_->snapToFirstMeasurement && state.meteringValid) {
            state.exposure = state.targetExposure;
            state.preExposure = state.exposure;
            impl_->snapToFirstMeasurement = false;
        }
        impl_->debug.measurementValid = state.meteringValid;
        impl_->debug.meteredLuminance = state.meteredLuminance;
    }
    impl_->currentExposure = state.exposure;
    impl_->currentPreExposure = state.preExposure;
    impl_->debug.currentExposure = state.exposure;
    impl_->debug.targetExposure = state.targetExposure;
    impl_->debug.measuredFrameIndex = impl_->latestMeasuredFrame;
    return state;
}

bool AutomaticExposureMeter::QueueMeasurement(RenderTarget2D& sceneColor, float preExposure, uint64_t frameIndex) {
    impl_->debug.measurementQueued = false;
    if (!impl_->automaticModeActive || impl_->context.device == nullptr || impl_->context.cmdList == nullptr ||
        impl_->context.frameFence == nullptr || impl_->context.currentFrameRetireFenceValue == 0 ||
        sceneColor.GetResource() == nullptr || sceneColor.GetWidth() <= 0 || sceneColor.GetHeight() <= 0 ||
        !std::isfinite(preExposure) || preExposure <= 0.0f || !impl_->EnsureInitialized()) {
        return false;
    }

    Impl::ReadbackSlot* selectedSlot = nullptr;
    for (size_t offset = 0; offset < impl_->readbackSlots.size(); ++offset) {
        const size_t index = (impl_->nextReadbackSlot + offset) % impl_->readbackSlots.size();
        Impl::ReadbackSlot& slot = impl_->readbackSlots[index];
        if (slot.fenceValue == 0) {
            selectedSlot = &slot;
            impl_->nextReadbackSlot = (index + 1u) % impl_->readbackSlots.size();
            break;
        }
    }
    if (selectedSlot == nullptr || selectedSlot->buffer == nullptr) {
        return false;
    }

    ID3D12Device* device = impl_->context.device;
    const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // 測光と readback の fence が終わるまで、同じ SRV を書き換えない（HDR 入力はフレームごとに入れ替わる）。
    const UINT descriptorOffset = static_cast<UINT>(selectedSlot - impl_->readbackSlots.data()) * kDescriptorCount;
    const CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(impl_->descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                  descriptorOffset, descriptorSize);
    const CD3DX12_GPU_DESCRIPTOR_HANDLE table(impl_->descriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                                              descriptorOffset, descriptorSize);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = sceneColor.GetFormat();
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(sceneColor.GetResource(), &srv, srvHandle);

    ID3D12GraphicsCommandList* commandList = impl_->context.cmdList;
    const D3D12_RESOURCE_STATES previousSceneState = sceneColor.GetColorState();
    const D3D12_RESOURCE_STATES computeReadState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    sceneColor.TransitionColor(computeReadState);

    ID3D12DescriptorHeap* heaps[] = {impl_->descriptorHeap.Get()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(impl_->rootSignature.Get());
    commandList->SetPipelineState(impl_->pipelineState.Get());
    commandList->SetComputeRootDescriptorTable(0, table);
    const uint32_t constants[4] = {static_cast<uint32_t>(sceneColor.GetWidth()),
                                   static_cast<uint32_t>(sceneColor.GetHeight()),
                                   std::bit_cast<uint32_t>(1.0f / preExposure), 0u};
    commandList->SetComputeRoot32BitConstants(1, static_cast<UINT>(std::size(constants)), constants, 0);
    commandList->Dispatch(1, 1, 1);

    const D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(impl_->measurementBuffer.Get());
    commandList->ResourceBarrier(1, &uavBarrier);
    const D3D12_RESOURCE_BARRIER toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        impl_->measurementBuffer.Get(), impl_->measurementState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(1, &toCopy);
    impl_->measurementState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commandList->CopyBufferRegion(selectedSlot->buffer.Get(), 0, impl_->measurementBuffer.Get(), 0, kMeasurementBytes);
    const D3D12_RESOURCE_BARRIER toUav = CD3DX12_RESOURCE_BARRIER::Transition(
        impl_->measurementBuffer.Get(), impl_->measurementState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(1, &toUav);
    impl_->measurementState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    sceneColor.TransitionColor(previousSceneState);

    selectedSlot->fenceValue = impl_->context.currentFrameRetireFenceValue;
    selectedSlot->frameIndex = frameIndex;
    selectedSlot->discard = false;
    impl_->debug.measurementQueued = true;
    return true;
}

const AutomaticExposureDebugState& AutomaticExposureMeter::GetDebugState() const { return impl_->debug; }

} // namespace HIKARI::RENDER3D::COLOR
