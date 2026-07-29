#include "Render3D/Color/HIKARI_AutomaticExposure.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>

#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render2D/HIKARI_RenderTarget2D.h"

namespace HIKARI::RENDER3D::COLOR {

    using Microsoft::WRL::ComPtr;

    namespace {
        constexpr uint32_t kDescriptorCount = 2u;
        constexpr uint32_t kMeasurementValueCount = 4u;
        constexpr uint64_t kMeasurementBytes =
            sizeof(float) * kMeasurementValueCount;

        struct MeasurementResult {
            float geometricLuminance = 0.18f;
            float arithmeticLuminance = 0.18f;
            float sampleWeight = 0.0f;
            float padding = 0.0f;
        };

        bool IsValidLuminance(float value) noexcept {
            return std::isfinite(value) &&
                value > 1e-6f &&
                value < 1e8f;
        }
    }

    struct AutomaticExposureMeter::Impl {
        struct ReadbackSlot {
            ComPtr<ID3D12Resource> buffer{};
            uint64_t fenceValue = 0;
            uint64_t frameIndex = 0;
            bool pending = false;
        };

        GFX::Context context{};
        ComPtr<ID3D12RootSignature> rootSignature{};
        ComPtr<ID3D12PipelineState> pipelineState{};
        ComPtr<ID3D12DescriptorHeap> descriptorHeap{};
        ComPtr<ID3D12Resource> measurementBuffer{};
        std::array<ReadbackSlot, GFX::kFrameResourceCount> readbackSlots{};
        D3D12_RESOURCE_STATES measurementState =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        size_t nextReadbackSlot = 0;
        float latestGeometricLuminance = 0.18f;
        float latestArithmeticLuminance = 0.18f;
        float currentExposure = 1.0f;
        uint64_t latestMeasuredFrame = 0;
        bool latestMeasurementValid = false;
        bool automaticModeActive = false;
        AutomaticExposureDebugState debug{};

        bool IsInitialized() const noexcept {
            return rootSignature != nullptr &&
                pipelineState != nullptr &&
                descriptorHeap != nullptr &&
                measurementBuffer != nullptr &&
                readbackSlots[0].buffer != nullptr;
        }

        void ReleaseResources() {
            GFX::RetireD3D12ObjectForFrame(
                rootSignature,
                context,
                "AutomaticExposure.RootSignature");
            GFX::RetireD3D12ObjectForFrame(
                pipelineState,
                context,
                "AutomaticExposure.PipelineState");
            GFX::RetireD3D12ObjectForFrame(
                descriptorHeap,
                context,
                "AutomaticExposure.DescriptorHeap");
            GFX::RetireD3D12ObjectForFrame(
                measurementBuffer,
                context,
                "AutomaticExposure.MeasurementBuffer");
            for (ReadbackSlot& slot : readbackSlots) {
                GFX::RetireD3D12ObjectForFrame(
                    slot.buffer,
                    context,
                    "AutomaticExposure.Readback");
                slot = {};
            }
            measurementState =
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            nextReadbackSlot = 0;
            debug.initialized = false;
            debug.measurementQueued = false;
        }

        bool CreateRootSignature(ID3D12Device* device) {
            D3D12_DESCRIPTOR_RANGE ranges[2]{};
            ranges[0].RangeType =
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[0].NumDescriptors = 1;
            ranges[0].BaseShaderRegister = 0;
            ranges[0].RegisterSpace = 0;
            ranges[0].OffsetInDescriptorsFromTableStart = 0;
            ranges[1].RangeType =
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            ranges[1].NumDescriptors = 1;
            ranges[1].BaseShaderRegister = 0;
            ranges[1].RegisterSpace = 0;
            ranges[1].OffsetInDescriptorsFromTableStart = 1;

            D3D12_ROOT_PARAMETER parameters[2]{};
            parameters[0].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[0].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            parameters[0].DescriptorTable.NumDescriptorRanges = 2;
            parameters[0].DescriptorTable.pDescriptorRanges = ranges;
            parameters[1].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[1].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            parameters[1].Constants.ShaderRegister = 0;
            parameters[1].Constants.RegisterSpace = 0;
            parameters[1].Constants.Num32BitValues = 4;

            D3D12_ROOT_SIGNATURE_DESC description{};
            description.NumParameters =
                static_cast<UINT>(std::size(parameters));
            description.pParameters = parameters;
            description.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ComPtr<ID3DBlob> signature{};
            ComPtr<ID3DBlob> errors{};
            const HRESULT serializeResult =
                D3D12SerializeRootSignature(
                    &description,
                    D3D_ROOT_SIGNATURE_VERSION_1,
                    signature.GetAddressOf(),
                    errors.GetAddressOf());
            if (!HIKARI_DX_CHECK(
                    serializeResult,
                    "AutomaticExposure::SerializeRootSignature")) {
                return false;
            }

            const HRESULT createResult =
                device->CreateRootSignature(
                    0,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(rootSignature.GetAddressOf()));
            if (!HIKARI_DX_CHECK(
                    createResult,
                    "AutomaticExposure::CreateRootSignature")) {
                return false;
            }
            GFX::SetD3D12Name(
                rootSignature.Get(),
                L"Automatic Exposure Root Signature");
            return true;
        }

        bool CreatePipeline(ID3D12Device* device) {
            ComPtr<ID3DBlob> shader{};
            if (!GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Color_AutomaticExposureCS.hlsl",
                    "main",
                    GFX::ShaderStage::Compute,
                    shader.GetAddressOf())) {
                return false;
            }

            D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
            description.pRootSignature = rootSignature.Get();
            description.CS = {
                shader->GetBufferPointer(),
                shader->GetBufferSize()
            };
            const HRESULT result =
                device->CreateComputePipelineState(
                    &description,
                    IID_PPV_ARGS(pipelineState.GetAddressOf()));
            if (!HIKARI_DX_CHECK(
                    result,
                    "AutomaticExposure::CreatePipelineState")) {
                return false;
            }
            GFX::SetD3D12Name(
                pipelineState.Get(),
                L"Automatic Exposure Meter PSO");
            return true;
        }

        bool CreateResources(ID3D12Device* device) {
            D3D12_DESCRIPTOR_HEAP_DESC descriptorDescription{};
            descriptorDescription.Type =
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            descriptorDescription.NumDescriptors =
                kDescriptorCount;
            descriptorDescription.Flags =
                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (!HIKARI_DX_CHECK(
                    device->CreateDescriptorHeap(
                        &descriptorDescription,
                        IID_PPV_ARGS(
                            descriptorHeap.GetAddressOf())),
                    "AutomaticExposure::CreateDescriptorHeap")) {
                return false;
            }
            GFX::SetD3D12Name(
                descriptorHeap.Get(),
                L"Automatic Exposure Descriptor Heap");

            const auto defaultHeap =
                CD3DX12_HEAP_PROPERTIES(
                    D3D12_HEAP_TYPE_DEFAULT);
            const auto measurementDescription =
                CD3DX12_RESOURCE_DESC::Buffer(
                    kMeasurementBytes,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (!HIKARI_DX_CHECK(
                    device->CreateCommittedResource(
                        &defaultHeap,
                        D3D12_HEAP_FLAG_NONE,
                        &measurementDescription,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        nullptr,
                        IID_PPV_ARGS(
                            measurementBuffer.GetAddressOf())),
                    "AutomaticExposure::CreateMeasurementBuffer")) {
                return false;
            }
            GFX::SetD3D12Name(
                measurementBuffer.Get(),
                L"Automatic Exposure Measurement Buffer");

            const auto readbackHeap =
                CD3DX12_HEAP_PROPERTIES(
                    D3D12_HEAP_TYPE_READBACK);
            const auto readbackDescription =
                CD3DX12_RESOURCE_DESC::Buffer(
                    kMeasurementBytes);
            for (ReadbackSlot& slot : readbackSlots) {
                if (!HIKARI_DX_CHECK(
                        device->CreateCommittedResource(
                            &readbackHeap,
                            D3D12_HEAP_FLAG_NONE,
                            &readbackDescription,
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            nullptr,
                            IID_PPV_ARGS(
                                slot.buffer.GetAddressOf())),
                        "AutomaticExposure::CreateReadbackBuffer")) {
                    return false;
                }
                GFX::SetD3D12Name(
                    slot.buffer.Get(),
                    L"Automatic Exposure Readback");
            }

            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(
                descriptorHeap->
                    GetCPUDescriptorHandleForHeapStart(),
                1,
                descriptorSize);
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = DXGI_FORMAT_UNKNOWN;
            uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.FirstElement = 0;
            uav.Buffer.NumElements = 1;
            uav.Buffer.StructureByteStride =
                static_cast<UINT>(kMeasurementBytes);
            device->CreateUnorderedAccessView(
                measurementBuffer.Get(),
                nullptr,
                &uav,
                uavHandle);
            return true;
        }

        bool EnsureInitialized() {
            if (IsInitialized()) {
                return true;
            }
            ReleaseResources();
            if (context.device == nullptr ||
                !CreateRootSignature(context.device) ||
                !CreatePipeline(context.device) ||
                !CreateResources(context.device)) {
                ReleaseResources();
                return false;
            }
            debug.initialized = true;
            return true;
        }

        void CollectMeasurements() {
            bool collected = false;
            for (ReadbackSlot& slot : readbackSlots) {
                if (!slot.pending ||
                    slot.buffer == nullptr ||
                    slot.fenceValue == 0 ||
                    !GFX::IsCurrentFenceComplete(
                        slot.fenceValue)) {
                    continue;
                }

                const D3D12_RANGE readRange{
                    0,
                    static_cast<SIZE_T>(
                        kMeasurementBytes)
                };
                void* mapped = nullptr;
                if (SUCCEEDED(slot.buffer->Map(
                        0,
                        &readRange,
                        &mapped)) &&
                    mapped != nullptr) {
                    MeasurementResult result{};
                    std::memcpy(
                        &result,
                        mapped,
                        sizeof(result));
                    const D3D12_RANGE writeRange{ 0, 0 };
                    slot.buffer->Unmap(0, &writeRange);
                    if (IsValidLuminance(
                            result.geometricLuminance) &&
                        result.sampleWeight > 0.0f &&
                        (!collected ||
                            slot.frameIndex >=
                                latestMeasuredFrame)) {
                        latestGeometricLuminance =
                            result.geometricLuminance;
                        latestArithmeticLuminance =
                            IsValidLuminance(
                                result.arithmeticLuminance)
                                ? result.arithmeticLuminance
                                : result.geometricLuminance;
                        latestMeasuredFrame = slot.frameIndex;
                        latestMeasurementValid = true;
                        collected = true;
                    }
                }
                slot.pending = false;
                slot.fenceValue = 0;
            }
        }
    };

    AutomaticExposureMeter::AutomaticExposureMeter()
        : impl_(std::make_unique<Impl>()) {
    }

    AutomaticExposureMeter::~AutomaticExposureMeter() {
        Shutdown();
    }

    void AutomaticExposureMeter::UpdateContext(
        const GFX::Context& context) {

        if (impl_->context.device != nullptr &&
            impl_->context.device != context.device) {
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

    void AutomaticExposureMeter::Reset(
        float exposureMultiplier) {

        impl_->latestGeometricLuminance = 0.18f;
        impl_->latestArithmeticLuminance = 0.18f;
        impl_->currentExposure =
            std::isfinite(exposureMultiplier) &&
                exposureMultiplier > 0.0f
            ? exposureMultiplier
            : 1.0f;
        impl_->latestMeasuredFrame = 0;
        impl_->latestMeasurementValid = false;
        impl_->automaticModeActive = false;
        impl_->debug.measurementValid = false;
        impl_->debug.meteredLuminance = 0.18f;
        impl_->debug.arithmeticLuminance = 0.18f;
        impl_->debug.currentExposure =
            impl_->currentExposure;
        impl_->debug.targetExposure =
            impl_->currentExposure;
    }

    ExposureFrameState AutomaticExposureMeter::BeginFrame(
        ExposureMode mode,
        float manualExposure,
        const AutomaticExposureSettings& settings,
        float deltaTime) {

        impl_->CollectMeasurements();
        if (mode != ExposureMode::Automatic) {
            impl_->automaticModeActive = false;
            const ExposureFrameState state =
                BuildManualExposureFrame(manualExposure);
            impl_->currentExposure =
                state.exposureMultiplier;
            impl_->debug.measurementValid =
                impl_->latestMeasurementValid;
            impl_->debug.meteredLuminance =
                impl_->latestGeometricLuminance;
            impl_->debug.arithmeticLuminance =
                impl_->latestArithmeticLuminance;
            impl_->debug.currentExposure =
                state.exposureMultiplier;
            impl_->debug.targetExposure =
                state.targetExposureMultiplier;
            impl_->debug.measuredFrameIndex =
                impl_->latestMeasuredFrame;
            return state;
        }

        if (!impl_->automaticModeActive) {
            impl_->currentExposure =
                std::isfinite(manualExposure) &&
                    manualExposure > 0.0f
                ? manualExposure
                : 1.0f;
            impl_->automaticModeActive = true;
        }
        ExposureFrameState state =
            BuildAutomaticExposureFrame(
                settings,
                impl_->latestGeometricLuminance,
                impl_->currentExposure,
                deltaTime,
                impl_->latestMeasurementValid);
        impl_->currentExposure =
            state.exposureMultiplier;
        impl_->debug.measurementValid =
            state.meteringValid;
        impl_->debug.meteredLuminance =
            state.meteredLuminance;
        impl_->debug.arithmeticLuminance =
            impl_->latestArithmeticLuminance;
        impl_->debug.currentExposure =
            state.exposureMultiplier;
        impl_->debug.targetExposure =
            state.targetExposureMultiplier;
        impl_->debug.measuredFrameIndex =
            impl_->latestMeasuredFrame;
        return state;
    }

    bool AutomaticExposureMeter::QueueMeasurement(
        RenderTarget2D& sceneColor) {

        impl_->debug.measurementQueued = false;
        if (!impl_->automaticModeActive ||
            impl_->context.device == nullptr ||
            impl_->context.cmdList == nullptr ||
            impl_->context.frameFence == nullptr ||
            impl_->context.currentFrameRetireFenceValue == 0 ||
            sceneColor.GetResource() == nullptr ||
            sceneColor.GetWidth() <= 0 ||
            sceneColor.GetHeight() <= 0 ||
            !impl_->EnsureInitialized()) {
            return false;
        }

        Impl::ReadbackSlot* selectedSlot = nullptr;
        for (size_t offset = 0;
             offset < impl_->readbackSlots.size();
             ++offset) {
            const size_t index =
                (impl_->nextReadbackSlot + offset) %
                impl_->readbackSlots.size();
            Impl::ReadbackSlot& slot =
                impl_->readbackSlots[index];
            if (!slot.pending) {
                selectedSlot = &slot;
                impl_->nextReadbackSlot =
                    (index + 1u) %
                    impl_->readbackSlots.size();
                break;
            }
        }
        if (selectedSlot == nullptr ||
            selectedSlot->buffer == nullptr) {
            return false;
        }

        ID3D12Device* device = impl_->context.device;
        const UINT descriptorSize =
            device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
            impl_->descriptorHeap->
                GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = sceneColor.GetFormat();
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;
        device->CreateShaderResourceView(
            sceneColor.GetResource(),
            &srv,
            srvHandle);
        (void)descriptorSize;

        ID3D12GraphicsCommandList* commandList =
            impl_->context.cmdList;
        const D3D12_RESOURCE_STATES previousSceneState =
            sceneColor.GetColorState();
        const D3D12_RESOURCE_STATES computeReadState =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        sceneColor.TransitionColor(computeReadState);

        ID3D12DescriptorHeap* heaps[] = {
            impl_->descriptorHeap.Get()
        };
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetComputeRootSignature(
            impl_->rootSignature.Get());
        commandList->SetPipelineState(
            impl_->pipelineState.Get());
        commandList->SetComputeRootDescriptorTable(
            0,
            impl_->descriptorHeap->
                GetGPUDescriptorHandleForHeapStart());
        const uint32_t constants[4] = {
            static_cast<uint32_t>(sceneColor.GetWidth()),
            static_cast<uint32_t>(sceneColor.GetHeight()),
            0u,
            0u
        };
        commandList->SetComputeRoot32BitConstants(
            1,
            static_cast<UINT>(std::size(constants)),
            constants,
            0);
        commandList->Dispatch(1, 1, 1);

        const D3D12_RESOURCE_BARRIER uavBarrier =
            CD3DX12_RESOURCE_BARRIER::UAV(
                impl_->measurementBuffer.Get());
        commandList->ResourceBarrier(1, &uavBarrier);
        const D3D12_RESOURCE_BARRIER toCopy =
            CD3DX12_RESOURCE_BARRIER::Transition(
                impl_->measurementBuffer.Get(),
                impl_->measurementState,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->ResourceBarrier(1, &toCopy);
        impl_->measurementState =
            D3D12_RESOURCE_STATE_COPY_SOURCE;
        commandList->CopyBufferRegion(
            selectedSlot->buffer.Get(),
            0,
            impl_->measurementBuffer.Get(),
            0,
            kMeasurementBytes);
        const D3D12_RESOURCE_BARRIER toUav =
            CD3DX12_RESOURCE_BARRIER::Transition(
                impl_->measurementBuffer.Get(),
                impl_->measurementState,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->ResourceBarrier(1, &toUav);
        impl_->measurementState =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        sceneColor.TransitionColor(previousSceneState);

        selectedSlot->fenceValue =
            impl_->context.currentFrameRetireFenceValue;
        selectedSlot->frameIndex =
            impl_->context.frameIndex;
        selectedSlot->pending = true;
        impl_->debug.measurementQueued = true;
        return true;
    }

    const AutomaticExposureDebugState&
        AutomaticExposureMeter::GetDebugState() const {

        return impl_->debug;
    }

} // namespace HIKARI::RENDER3D::COLOR
