#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI::SHADOW::INTERNAL {

    bool CommitShadowMaterialDataFrame(ShadowRendererState& state, const GFX::Context& graphics) {
        ShadowFrameResources& frame = GetActiveShadowFrameResources(state);
        ID3D12GraphicsCommandList* commandList = graphics.cmdList;
        ID3D12Resource* defaultResource = frame.materialDataBuffer.Get();
        D3D12_RESOURCE_STATES& defaultState = frame.materialDataState;
        const auto& table = state.materialDataFrameTable;
        const size_t materialCount = table.count;
        if (commandList == nullptr || defaultResource == nullptr ||
            !table.complete || materialCount == 0 || materialCount > frame.materialDataStaging.size()) return false;
        const size_t byteCount = sizeof(MESHRENDERER::MaterialGpuData) * materialCount;
        frame.frameUploads.ResetForFrame(graphics.currentFrameRetireFenceValue);
        const DX::UploadAllocation upload = frame.frameUploads.Allocate(byteCount);
        if (!upload) return false;
        std::memcpy(upload.cpuAddress, frame.materialDataStaging.data(), byteCount);
        const auto snapshot = [&](const void* data, size_t bytes, D3D12_GPU_VIRTUAL_ADDRESS& address) {
            void* destination = frame.frameUploads.Allocate(bytes, address);
            if (destination == nullptr) return false;
            std::memcpy(destination, data, bytes);
            return true;
        };
        const MATH::Vec4 emptyParameter{};
        const uint32_t emptyResource = MESHRENDERER::kInvalidTextureDescriptorIndex;
        if (!snapshot(table.programParameters.empty() ? &emptyParameter : table.programParameters.data(),
                (std::max)(size_t(1), table.programParameters.size()) * sizeof(MATH::Vec4),
                frame.materialProgramParametersAddress) ||
            !snapshot(table.programResources.empty() ? &emptyResource : table.programResources.data(),
                (std::max)(size_t(1), table.programResources.size()) * sizeof(uint32_t),
                frame.materialProgramResourcesAddress) ||
            !snapshot(&state.materialCamera, sizeof(MESHRENDERER::CameraCB), frame.materialCameraAddress)) {
            return false;
        }

        if (defaultState != D3D12_RESOURCE_STATE_COPY_DEST) {
            const auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
                defaultResource,
                defaultState,
                D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, &toCopyDest);
            defaultState = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        commandList->CopyBufferRegion(defaultResource, 0, upload.resource, upload.offset, byteCount);

        const D3D12_RESOURCE_STATES shaderState =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const auto toShader = CD3DX12_RESOURCE_BARRIER::Transition(
            defaultResource,
            D3D12_RESOURCE_STATE_COPY_DEST,
            shaderState);
        commandList->ResourceBarrier(1, &toShader);
        defaultState = shaderState;
        return true;
    }

    void ActivateShadowFrameResources(ShadowRendererState& state, uint32_t frameIndex) {
        state.activeFrameResourceIndex = frameIndex % GFX::kFrameResourceCount;
    }

    void ReleaseShadowFrameResources(ShadowRendererState& state, const GFX::Context& graphics) {
        for (auto& descriptor : state.frameDescriptors)
            RENDER3D::RetireRenderResourceDescriptor(descriptor, graphics, "Shadow.FrameDescriptor");
        state.frameDescriptors.clear();
        state.surfaceGpuSceneBuffer.Finalize(graphics);
        for (auto& frame : state.frameResources) {
            frame.frameUploads.Finalize(&graphics);
            GFX::RetireD3D12ObjectForFrame(frame.materialDataBuffer, graphics, "Shadow.MaterialData");
            frame = {};
        }
        state.initialized = false;
    }

    bool CreateShadowFrameResources(ShadowRendererState& state, const GFX::Context& graphics) {
        ID3D12Device* device = graphics.device;
        if (device == nullptr || graphics.srvHeap == nullptr) return false;
        RENDER3D::UpdateRenderResourceDescriptorPoolContext(graphics);
        // Scene と Material は renderer ごとに所有する。固定 SystemSrv を上書きしない。
        auto descriptors = RENDER3D::AllocateRenderResourceDescriptorRange(GFX::kFrameResourceCount * 2u);
        if (descriptors.size() != GFX::kFrameResourceCount * 2u) return false;
        ReleaseShadowFrameResources(state, graphics);
        state.frameDescriptors = std::move(descriptors);
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> sceneCpu{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> sceneGpu{};
        for (uint32_t index = 0; index < GFX::kFrameResourceCount; ++index) {
            sceneCpu[index] = state.frameDescriptors[index].cpu;
            sceneGpu[index] = state.frameDescriptors[index].gpu;
        }
        const auto fail = [&] {
            ReleaseShadowFrameResources(state, graphics);
            return false;
        };
        if (!state.surfaceGpuSceneBuffer.Initialize(device, sceneCpu, sceneGpu) ||
            !state.sandSurfaceGpuFrameBuffer.Initialize(device)) return fail();

        const UINT materialDataBytes = sizeof(MESHRENDERER::MaterialGpuData) * MESHRENDERER::kMaxMaterialDataCount;
        D3D12_SHADER_RESOURCE_VIEW_DESC materialDataSrv{};
        materialDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        materialDataSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        materialDataSrv.Buffer.NumElements = MESHRENDERER::kMaxMaterialDataCount;
        materialDataSrv.Buffer.StructureByteStride = sizeof(MESHRENDERER::MaterialGpuData);
        for (uint32_t index = 0; index < GFX::kFrameResourceCount; ++index) {
            auto& frame = state.frameResources[index];
            try { frame.materialDataStaging.resize(MESHRENDERER::kMaxMaterialDataCount); }
            catch (...) { return fail(); }
            if (!frame.frameUploads.Init(device, 65536u) ||
                !GFX::D3D12_BUFFER::CreateDefaultBuffer(device, materialDataBytes, frame.materialDataBuffer))
                return fail();
            const auto& descriptor = state.frameDescriptors[GFX::kFrameResourceCount + index];
            frame.materialDataSrvGpu = descriptor.gpu;
            device->CreateShaderResourceView(frame.materialDataBuffer.Get(), &materialDataSrv, descriptor.cpu);
            GFX::SetD3D12Name(frame.materialDataBuffer.Get(), L"Shadow MaterialData Buffer");
        }
        ActivateShadowFrameResources(state, graphics.frameIndex);
        return true;
    }

} // namespace HIKARI::SHADOW::INTERNAL
