#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::VOLUMETRIC {

    namespace {
        using Microsoft::WRL::ComPtr;
        constexpr DXGI_FORMAT kVolumeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        struct alignas(16) VolumetricConstants {
            MATH::Mat4 invViewProj{};
            MATH::Mat4 prevViewProj{};
            MATH::Mat4 lightViewProj{};
            MATH::Vec4 cameraPos{};
            MATH::Vec4 prevCameraPos{};
            MATH::Vec4 fogColorDensity{};
            MATH::Vec4 fogDistances{};
            MATH::Vec4 directionalDirIntensity{};
            MATH::Vec4 directionalColorShadow{};
            MATH::Vec4 ambientColorIntensity{};
            MATH::Vec4 volumeSize{};
            MATH::Vec4 frameParams{};
            MATH::Vec4 debugParams{};
            MATH::Vec4 pointLightPosRange[8]{};
            MATH::Vec4 pointLightColorIntensity[8]{};
        };

        struct VolumeResource {
            ComPtr<ID3D12Resource> texture{};
            RenderResourceView srv{};
            RenderResourceView uav{};
            D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;
        };

        struct StageState {
            GFX::Context context{};
            ComPtr<ID3D12RootSignature> computeRoot{};
            ComPtr<ID3D12RootSignature> compositeRoot{};
            ComPtr<ID3D12PipelineState> computePso{};
            ComPtr<ID3D12PipelineState> compositePso{};
            ComPtr<ID3D12Resource> constantBuffer{};
            uint8_t* constantMapped = nullptr;
            uint32_t constantStride = 0;
            uint32_t constantFrameSlot = 0;
            VolumeResource volumes[2]{};
            RenderResourceView sceneColorUav{};
            RenderResourceView sceneDepthSrv{};
            ID3D12Resource* sceneColorResource = nullptr;
            ID3D12Resource* sceneDepthResource = nullptr;
            VolumetricConstants constants{};
            VolumetricLightingStats stats{};
            uint32_t writeIndex = 0;
            uint64_t preparedFrameIndex = 0;
            RenderDebugView debugView = RenderDebugView::None;
            bool pipelineReady = false;
            bool resourcesReady = false;
            bool prepared = false;
            bool volumeHistoryValid = false;
        };

        StageState& State() {
            static StageState state{};
            return state;
        }

        struct QualityPolicy {
            uint32_t froxelPixelSize = 16;
            uint32_t sliceCount = 64;
        };

        QualityPolicy ResolveQualityPolicy(VolumetricLightingQuality quality) {
            switch (quality) {
            case VolumetricLightingQuality::Low: return { 24u, 48u };
            case VolumetricLightingQuality::High: return { 12u, 80u };
            case VolumetricLightingQuality::Balanced:
            default: return { 16u, 64u };
            }
        }

        uint32_t ResolveDebugMode(RenderDebugView view) {
            switch (view) {
            case RenderDebugView::VolumetricScattering: return 1u;
            case RenderDebugView::VolumetricTransmittance: return 2u;
            case RenderDebugView::VolumetricDepthSlice: return 3u;
            default: return 0u;
            }
        }

        void ReleaseView(RenderResourceView& view) {
            if (view.IsValid()) {
                (void)ReleaseRenderResourceDescriptor(view);
                view = {};
            }
        }

        void RetireView(StageState& state, RenderResourceView& view, const char* debugName) {
            if (!view.IsValid()) return;
            const RenderResourceView retired = view;
            view = {};
            if (state.context.deferredReleaseQueue != nullptr &&
                state.context.currentFrameRetireFenceValue != 0) {
                state.context.deferredReleaseQueue->Enqueue(
                    state.context.currentFrameRetireFenceValue,
                    [retired]() {
                        (void)ReleaseRenderResourceDescriptor(retired);
                    },
                    debugName != nullptr ? debugName : "VolumetricLighting.Descriptor");
                return;
            }
            (void)ReleaseRenderResourceDescriptor(retired);
        }

        void ReleaseVolume(VolumeResource& volume) {
            ReleaseView(volume.srv);
            ReleaseView(volume.uav);
            volume.texture.Reset();
            volume.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        }

        void RetireVolume(StageState& state, VolumeResource& volume) {
            RetireView(state, volume.srv, "VolumetricLighting.VolumeSRV");
            RetireView(state, volume.uav, "VolumetricLighting.VolumeUAV");
            GFX::RetireD3D12ObjectForFrame(
                volume.texture,
                state.context,
                "VolumetricLighting.Volume");
            volume.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        }

        bool CreateRootSignature(
            ID3D12Device* device,
            bool injection,
            ID3D12RootSignature** output) {
            std::array<D3D12_DESCRIPTOR_RANGE, 4> ranges{};
            const uint32_t tableCount = injection ? 4u : 3u;
            for (uint32_t index = 0; index < tableCount; ++index) {
                ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                ranges[index].NumDescriptors = 1;
                if (injection) {
                    ranges[index].RangeType = index == 3
                        ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
                        : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    ranges[index].BaseShaderRegister = index == 3 ? 0u : index;
                } else {
                    ranges[index].RangeType = index == 2
                        ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
                        : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    ranges[index].BaseShaderRegister = index == 2
                        ? 1u
                        : (4u + index);
                }
                ranges[index].OffsetInDescriptorsFromTableStart = 0;
            }

            std::array<D3D12_ROOT_PARAMETER, 5> params{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].Descriptor.ShaderRegister = 0;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            for (uint32_t index = 0; index < tableCount; ++index) {
                params[index + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                params[index + 1].DescriptorTable.NumDescriptorRanges = 1;
                params[index + 1].DescriptorTable.pDescriptorRanges = &ranges[index];
                params[index + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            }

            std::array<D3D12_STATIC_SAMPLER_DESC, 2> samplers{};
            samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
            samplers[0].ShaderRegister = 0;
            samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            samplers[1] = samplers[0];
            samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
            samplers[1].ShaderRegister = 1;

            D3D12_ROOT_SIGNATURE_DESC desc{};
            desc.NumParameters = 1u + tableCount;
            desc.pParameters = params.data();
            desc.NumStaticSamplers = injection ? 2u : 1u;
            desc.pStaticSamplers = samplers.data();
            desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> errors;
            HRESULT hr = D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1,
                blob.GetAddressOf(), errors.GetAddressOf());
            if (FAILED(hr)) {
                if (errors) DEBUGLOG::PushRenderError(
                    static_cast<const char*>(errors->GetBufferPointer()));
                return false;
            }
            return HIKARI_DX_CHECK(
                device->CreateRootSignature(
                    0, blob->GetBufferPointer(), blob->GetBufferSize(),
                    IID_PPV_ARGS(output)),
                "VolumetricLighting::CreateRootSignature");
        }

        bool EnsurePipeline(StageState& state) {
            if (state.pipelineReady) return true;
            ID3D12Device* device = state.context.device;
            if (!device) return false;
            if (!CreateRootSignature(device, true, state.computeRoot.GetAddressOf()) ||
                !CreateRootSignature(device, false, state.compositeRoot.GetAddressOf())) {
                return false;
            }
            ComPtr<ID3DBlob> cs;
            ComPtr<ID3DBlob> compositeCs;
            const wchar_t* shader = L"HIKARI/Shaders/Render3D_VolumetricLighting.hlsl";
            if (!GFX::CompileShaderFileSm6(shader, "InjectAndIntegrateCS", GFX::ShaderStage::Compute, cs.GetAddressOf()) ||
                !GFX::CompileShaderFileSm6(shader, "CompositeCS", GFX::ShaderStage::Compute, compositeCs.GetAddressOf())) {
                return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc{};
            computeDesc.pRootSignature = state.computeRoot.Get();
            computeDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
            if (!HIKARI_DX_CHECK(
                    device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(state.computePso.GetAddressOf())),
                    "VolumetricLighting::CreateComputePSO")) return false;

            D3D12_COMPUTE_PIPELINE_STATE_DESC compositeDesc{};
            compositeDesc.pRootSignature = state.compositeRoot.Get();
            compositeDesc.CS = {
                compositeCs->GetBufferPointer(),
                compositeCs->GetBufferSize()
            };
            if (!HIKARI_DX_CHECK(
                    device->CreateComputePipelineState(
                        &compositeDesc,
                        IID_PPV_ARGS(state.compositePso.GetAddressOf())),
                    "VolumetricLighting::CreateCompositePSO")) return false;

            state.constantStride = (sizeof(VolumetricConstants) + 255u) & ~255u;
            const UINT cbSize = state.constantStride * GFX::kFrameResourceCount;
            const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(cbSize);
            if (!HIKARI_DX_CHECK(
                    device->CreateCommittedResource(
                        &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                        IID_PPV_ARGS(state.constantBuffer.GetAddressOf())),
                    "VolumetricLighting::CreateConstantBuffer")) return false;
            if (!HIKARI_DX_CHECK(
                    state.constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&state.constantMapped)),
                    "VolumetricLighting::MapConstantBuffer")) return false;
            state.pipelineReady = true;
            return true;
        }

        bool CreateVolume(
            StageState& state,
            VolumeResource& volume,
            uint32_t width,
            uint32_t height,
            uint32_t depth) {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = static_cast<UINT16>(depth);
            desc.MipLevels = 1;
            desc.Format = kVolumeFormat;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            if (!HIKARI_DX_CHECK(
                    state.context.device->CreateCommittedResource(
                        &heap, D3D12_HEAP_FLAG_NONE, &desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                        IID_PPV_ARGS(volume.texture.GetAddressOf())),
                    "VolumetricLighting::CreateVolume")) return false;
            volume.srv = AllocateTexture3DSrvDescriptor(volume.texture.Get(), kVolumeFormat);
            volume.uav = AllocateTexture3DUavDescriptor(
                volume.texture.Get(), kVolumeFormat, 0, 0, depth);
            volume.state = D3D12_RESOURCE_STATE_GENERIC_READ;
            return volume.srv.IsValid() && volume.uav.IsValid();
        }

        bool EnsureResources(
            StageState& state,
            uint32_t renderWidth,
            uint32_t renderHeight,
            uint32_t pixelSize,
            uint32_t sliceCount) {
            const uint32_t width = (renderWidth + pixelSize - 1u) / pixelSize;
            const uint32_t height = (renderHeight + pixelSize - 1u) / pixelSize;
            if (state.resourcesReady &&
                state.stats.renderWidth == renderWidth &&
                state.stats.renderHeight == renderHeight &&
                state.stats.froxelWidth == width &&
                state.stats.froxelHeight == height &&
                state.stats.froxelDepth == sliceCount) return true;

            for (VolumeResource& volume : state.volumes) RetireVolume(state, volume);
            if (!CreateVolume(state, state.volumes[0], width, height, sliceCount) ||
                !CreateVolume(state, state.volumes[1], width, height, sliceCount)) {
                state.resourcesReady = false;
                return false;
            }
            state.stats.renderWidth = renderWidth;
            state.stats.renderHeight = renderHeight;
            state.stats.froxelWidth = width;
            state.stats.froxelHeight = height;
            state.stats.froxelDepth = sliceCount;
            state.stats.froxelPixelSize = pixelSize;
            state.stats.workingSetBytes =
                static_cast<uint64_t>(width) * height * sliceCount * 8u * 2u;
            ++state.stats.resizeCount;
            ++state.stats.historyResetCount;
            state.volumeHistoryValid = false;
            state.resourcesReady = true;
            return true;
        }

        bool EnsureSceneViews(StageState& state, RenderTarget2D& scene) {
            if (state.sceneColorResource == scene.GetResource() &&
                state.sceneDepthResource == scene.GetDepthResource() &&
                state.sceneColorUav.IsValid() && state.sceneDepthSrv.IsValid()) return true;
            RetireView(state, state.sceneColorUav, "VolumetricLighting.SceneColorUAV");
            RetireView(state, state.sceneDepthSrv, "VolumetricLighting.SceneDepthSRV");
            state.sceneColorResource = scene.GetResource();
            state.sceneDepthResource = scene.GetDepthResource();
            if (!scene.AllowsUnorderedAccess() ||
                state.sceneColorResource == nullptr ||
                state.sceneDepthResource == nullptr) {
                return false;
            }
            state.sceneColorUav = AllocateTexture2DUavDescriptor(
                state.sceneColorResource, scene.GetFormat());
            state.sceneDepthSrv = AllocateTexture2DSrvDescriptor(
                state.sceneDepthResource, DXGI_FORMAT_R32_FLOAT);
            return state.sceneColorUav.IsValid() && state.sceneDepthSrv.IsValid();
        }

        void TransitionVolume(
            ID3D12GraphicsCommandList* cmd,
            VolumeResource& volume,
            D3D12_RESOURCE_STATES next) {
            if (volume.state == next) return;
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                volume.texture.Get(), volume.state, next);
            cmd->ResourceBarrier(1, &barrier);
            volume.state = next;
        }
    }

    bool PrepareVolumetricLightingFrame(
        const GFX::Context& context,
        const TEMPORAL::TemporalFrameState& frame,
        const SceneEnvironment& environment,
        VolumetricLightingQuality quality,
        RenderDebugView debugView) {
        StageState& state = State();
        state.context = context;
        state.stats.requested = environment.fog.enabled && environment.fog.volumetric;
        state.stats.active = false;
        state.prepared = false;
        if (!state.stats.requested || environment.fog.density <= 0.0f ||
            !frame.camera.valid || !context.device || !context.cmdList || !context.srvHeap) {
            return false;
        }
        UpdateRenderResourceDescriptorPoolContext(context);
        const QualityPolicy policy = ResolveQualityPolicy(quality);
        const uint32_t pixelSize = policy.froxelPixelSize;
        const uint32_t slices = policy.sliceCount;
        if (!EnsurePipeline(state) ||
            !EnsureResources(state, frame.renderWidth, frame.renderHeight, pixelSize, slices)) {
            return false;
        }
        if (frame.resetHistory && state.volumeHistoryValid) {
            state.volumeHistoryValid = false;
            ++state.stats.historyResetCount;
        }

        VolumetricConstants constants{};
        // The volume is indexed in the jittered render grid, so both depth
        // reconstruction and history reprojection must use that same grid.
        constants.invViewProj = frame.camera.invViewProj;
        constants.prevViewProj = frame.camera.prevViewProj;
        constants.lightViewProj = SHADOW::GetDirectionalLightViewProj();
        constants.cameraPos = frame.camera.cameraPos;
        constants.prevCameraPos = frame.camera.prevCameraPos;
        MATH::Vec3 fogColor = environment.fog.useSkyHorizonColor
            ? environment.sky.horizonColor
            : environment.fog.color;
        constants.fogColorDensity = {
            fogColor.x, fogColor.y, fogColor.z,
            (std::max)(0.0f, environment.fog.density)
        };
        constants.fogDistances = {
            (std::max)(0.01f, environment.fog.startDistance),
            (std::max)(environment.fog.startDistance + 0.01f, environment.fog.endDistance),
            (std::max)(0.0f, environment.fog.heightFalloff),
            std::clamp(environment.fog.anisotropy, -0.8f, 0.8f)
        };
        constants.directionalDirIntensity = {
            environment.directional.direction.x,
            environment.directional.direction.y,
            environment.directional.direction.z,
            environment.directional.enabled ? (std::max)(0.0f, environment.directional.intensity) : 0.0f
        };
        constants.directionalColorShadow = {
            environment.directional.color.x,
            environment.directional.color.y,
            environment.directional.color.z,
            std::clamp(environment.directionalShadow.strength, 0.0f, 1.0f)
        };
        constants.ambientColorIntensity = {
            environment.ambient.color.x,
            environment.ambient.color.y,
            environment.ambient.color.z,
            (std::max)(0.0f, environment.ambient.intensity)
        };
        const bool historyValid = state.volumeHistoryValid && frame.historyValid && !frame.resetHistory;
        constants.volumeSize = {
            static_cast<float>(state.stats.froxelWidth),
            static_cast<float>(state.stats.froxelHeight),
            static_cast<float>(state.stats.froxelDepth),
            historyValid ? 1.0f : 0.0f
        };
        const bool shadowed = SHADOW::IsDirectionalShadowEnabled() &&
            environment.directionalShadow.enabled &&
            SHADOW::GetDirectionalShadowSrv().ptr != 0;
        uint32_t pointCount = 0;
        for (const PointLight& light : environment.pointLights) {
            if (!light.enabled || light.range <= 0.0f || pointCount >= 8u) continue;
            constants.pointLightPosRange[pointCount] = {
                light.position.x, light.position.y, light.position.z, light.range
            };
            constants.pointLightColorIntensity[pointCount] = {
                light.color.x, light.color.y, light.color.z,
                (std::max)(0.0f, light.intensity)
            };
            ++pointCount;
        }
        constants.frameParams = {
            historyValid ? std::clamp(environment.fog.temporalWeight, 0.0f, 0.98f) : 0.0f,
            static_cast<float>(frame.frameIndex & 0xffffu),
            shadowed ? 1.0f : 0.0f,
            static_cast<float>(pointCount)
        };
        constants.debugParams = {
            static_cast<float>(ResolveDebugMode(debugView)),
            0.0f,
            0.0f,
            0.0f
        };
        state.constants = constants;
        state.preparedFrameIndex = frame.frameIndex;
        state.constantFrameSlot = static_cast<uint32_t>(frame.frameIndex % GFX::kFrameResourceCount);
        state.writeIndex = static_cast<uint32_t>(frame.frameIndex & 1u);
        state.debugView = debugView;
        state.stats.active = true;
        state.stats.historyValid = historyValid;
        state.stats.shadowed = shadowed;
        state.stats.pointLightCount = pointCount;
        state.stats.qualityLevel = static_cast<uint32_t>(quality);
        state.stats.temporalWeight = constants.frameParams.x;
        state.prepared = true;
        return true;
    }

    RenderTarget2D* ExecuteVolumetricLightingStage(RenderTarget2D& scene) {
        StageState& state = State();
        if (!state.prepared || !state.stats.active ||
            !EnsureSceneViews(state, scene) || !scene.BeginDepthShaderRead()) return &scene;
        ID3D12GraphicsCommandList* cmd = state.context.cmdList;
        ID3D12DescriptorHeap* heap = state.context.srvHeap;
        if (!cmd || !heap) {
            scene.EndDepthShaderRead();
            return &scene;
        }
        const uint64_t constantOffset =
            static_cast<uint64_t>(state.constantFrameSlot) * state.constantStride;
        std::memcpy(
            state.constantMapped + constantOffset,
            &state.constants,
            sizeof(state.constants));
        const D3D12_GPU_VIRTUAL_ADDRESS constantAddress =
            state.constantBuffer->GetGPUVirtualAddress() + constantOffset;
        VolumeResource& output = state.volumes[state.writeIndex];
        VolumeResource& history = state.volumes[state.writeIndex ^ 1u];

        GFX::PIX::ScopedGpuEvent event(
            cmd, GFX::PIX::kColorPost, "VolumetricLighting");
        GFX::GPU_PROFILE::ScopedGpuTimer timer(
            cmd, GFX::GPU_PROFILE::Pass::VolumetricLighting);
        cmd->SetDescriptorHeaps(1, &heap);
        TransitionVolume(cmd, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->SetComputeRootSignature(state.computeRoot.Get());
        cmd->SetPipelineState(state.computePso.Get());
        cmd->SetComputeRootConstantBufferView(0, constantAddress);
        cmd->SetComputeRootDescriptorTable(1, state.sceneDepthSrv.gpu);
        const D3D12_GPU_DESCRIPTOR_HANDLE shadow = state.stats.shadowed
            ? SHADOW::GetDirectionalShadowSrv()
            : state.sceneDepthSrv.gpu;
        cmd->SetComputeRootDescriptorTable(2, shadow);
        cmd->SetComputeRootDescriptorTable(3, history.srv.gpu);
        cmd->SetComputeRootDescriptorTable(4, output.uav.gpu);
        cmd->Dispatch(
            (state.stats.froxelWidth + 7u) / 8u,
            (state.stats.froxelHeight + 7u) / 8u,
            1u);
        const auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(output.texture.Get());
        cmd->ResourceBarrier(1, &uavBarrier);
        TransitionVolume(cmd, output, D3D12_RESOURCE_STATE_GENERIC_READ);

        scene.TransitionColor(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->SetDescriptorHeaps(1, &heap);
        cmd->SetComputeRootSignature(state.compositeRoot.Get());
        cmd->SetPipelineState(state.compositePso.Get());
        cmd->SetComputeRootConstantBufferView(0, constantAddress);
        cmd->SetComputeRootDescriptorTable(1, state.sceneDepthSrv.gpu);
        cmd->SetComputeRootDescriptorTable(2, output.srv.gpu);
        cmd->SetComputeRootDescriptorTable(3, state.sceneColorUav.gpu);
        cmd->Dispatch(
            (state.stats.renderWidth + 7u) / 8u,
            (state.stats.renderHeight + 7u) / 8u,
            1u);
        const auto sceneUavBarrier =
            CD3DX12_RESOURCE_BARRIER::UAV(scene.GetResource());
        cmd->ResourceBarrier(1, &sceneUavBarrier);
        scene.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        scene.EndDepthShaderRead();

        state.volumeHistoryValid = true;
        ++state.stats.dispatchCount;
        return &scene;
    }

    bool IsVolumetricLightingActive() {
        return State().prepared && State().stats.active;
    }

    bool IsVolumetricLightingDebugViewActive() {
        const StageState& state = State();
        return state.prepared && state.stats.active &&
            IsVolumetricRenderDebugView(state.debugView);
    }

    const VolumetricLightingStats& GetVolumetricLightingStats() {
        return State().stats;
    }

    void ShutdownVolumetricLightingStage() {
        StageState& state = State();
        for (VolumeResource& volume : state.volumes) ReleaseVolume(volume);
        ReleaseView(state.sceneColorUav);
        ReleaseView(state.sceneDepthSrv);
        if (state.constantBuffer && state.constantMapped) state.constantBuffer->Unmap(0, nullptr);
        state.constantMapped = nullptr;
        state.constantBuffer.Reset();
        state.constantStride = 0;
        state.constantFrameSlot = 0;
        state.computePso.Reset();
        state.compositePso.Reset();
        state.computeRoot.Reset();
        state.compositeRoot.Reset();
        state.sceneColorResource = nullptr;
        state.sceneDepthResource = nullptr;
        state.constants = {};
        state.stats = {};
        state.writeIndex = 0;
        state.preparedFrameIndex = 0;
        state.debugView = RenderDebugView::None;
        state.pipelineReady = false;
        state.resourcesReady = false;
        state.prepared = false;
        state.volumeHistoryValid = false;
        state.context = {};
    }

} // namespace HIKARI::RENDER3D::VOLUMETRIC
