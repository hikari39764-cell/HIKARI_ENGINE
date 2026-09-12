#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI::SHADOW::INTERNAL {

    D3D12_GPU_VIRTUAL_ADDRESS ResolveShadowJointPaletteAddress(
        size_t objectIndex) {

        ShadowFrameResources& frame = GetActiveShadowFrameResources();
        if (frame.jointPaletteCB == nullptr ||
            objectIndex >= kMaxShadowCasterObjects) {
            return 0;
        }

        return frame.jointPaletteCB->GetGPUVirtualAddress() +
            static_cast<UINT64>(
                GFX::AlignD3D12ConstantBufferByteSize(
                    sizeof(ShadowJointPaletteCB))) *
                objectIndex;
    }

    size_t UploadShadowJointPalette(
        size_t objectIndex,
        const std::vector<MATH::Mat4>& palette) {

        ShadowFrameResources& frame = GetActiveShadowFrameResources();
        if (frame.jointPaletteMapped == nullptr ||
            objectIndex >= kMaxShadowCasterObjects) {
            return 0;
        }

        ShadowJointPaletteCB cb{};
        for (MATH::Mat4& matrix : cb.jointMatrices) {
            matrix = MATH::Mat4::Identity();
        }
        const size_t uploadCount = std::min(palette.size(), kMaxShadowJointPaletteMatrices);
        for (size_t i = 0; i < uploadCount; ++i) {
            cb.jointMatrices[i] = palette[i];
        }

        uint8_t* dst = reinterpret_cast<uint8_t*>(frame.jointPaletteMapped) +
            static_cast<size_t>(
                GFX::AlignD3D12ConstantBufferByteSize(
                    sizeof(ShadowJointPaletteCB))) *
                objectIndex;
        std::memcpy(dst, &cb, sizeof(cb));
        return uploadCount;
    }

    bool CommitShadowMaterialDataFrame(const GFX::Context& graphics) {
        ShadowFrameResources& frame = GetActiveShadowFrameResources();
        ID3D12GraphicsCommandList* commandList = graphics.cmdList;
        ID3D12Resource* defaultResource = frame.materialDataBuffer.Get();
        D3D12_RESOURCE_STATES& defaultState = frame.materialDataState;
        const size_t materialCount = gShadowRendererState.materialDataFrameTable.count;
        if (commandList == nullptr || defaultResource == nullptr ||
            materialCount == 0 || materialCount > frame.materialDataStaging.size()) return false;
        const size_t byteCount = sizeof(MESHRENDERER::MaterialGpuData) * materialCount;
        frame.materialUploads.ResetForFrame(graphics.currentFrameRetireFenceValue);
        const DX::UploadAllocation upload = frame.materialUploads.Allocate(byteCount);
        if (!upload) return false;
        std::memcpy(upload.cpuAddress, frame.materialDataStaging.data(), byteCount);

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

    void ActivateShadowFrameResources(uint32_t frameIndex) {
        gShadowRendererState.activeFrameResourceIndex = frameIndex % GFX::kFrameResourceCount;
    }

    bool CreateShadowFrameResources(const GFX::Context& graphics) {
        ID3D12Device* device = graphics.device;
        const UINT cameraBytes =
            ShadowCameraConstantStride() * kMaxDirectionalShadowCascades;
        const UINT materialDataBytes =
            static_cast<UINT>(sizeof(MESHRENDERER::MaterialGpuData) * MESHRENDERER::kMaxMaterialDataCount);
        const UINT paletteBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(ShadowJointPaletteCB)) * kMaxShadowCasterObjects;

        ID3D12DescriptorHeap* srvHeap = graphics.srvHeap;
        if (device == nullptr || srvHeap == nullptr) {
            return false;
        }
        const UINT descriptorSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT surfaceGpuSceneSrvIndex =
            GFX::DESCRIPTOR::ToFrameIndex(
                GFX::DESCRIPTOR::SystemSrv::ShadowSurfaceGpuSceneFrame0,
                0u);
        const D3D12_CPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvCpu =
            GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);
        const D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvGpu =
            GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);

        D3D12_SHADER_RESOURCE_VIEW_DESC materialDataSrv{};
        materialDataSrv.Format = DXGI_FORMAT_UNKNOWN;
        materialDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        materialDataSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        materialDataSrv.Buffer.FirstElement = 0;
        materialDataSrv.Buffer.NumElements = MESHRENDERER::kMaxMaterialDataCount;
        materialDataSrv.Buffer.StructureByteStride = sizeof(MESHRENDERER::MaterialGpuData);
        materialDataSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        if (!gShadowRendererState.surfaceGpuSceneBuffer.Initialize(
            device,
            surfaceGpuSceneSrvCpu,
            surfaceGpuSceneSrvGpu,
            descriptorSize)) {
            DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] SurfaceGpuScene buffer initialization failed. GPU-driven shadow pass will be unavailable.");
        }
        if (!gShadowRendererState.sandSurfaceGpuFrameBuffer.Initialize(device)) {
            DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] SandSurface buffers initialization failed. Sand displacement will be unavailable in shadows.");
        }

        for (uint32_t frameIndex = 0; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
            ShadowFrameResources& frame = gShadowRendererState.frameResources[frameIndex];
            try {
                frame.materialDataStaging.resize(MESHRENDERER::kMaxMaterialDataCount);
            } catch (...) {
                return false;
            }
            if (!frame.materialUploads.Init(device, 65536u)) return false;
            if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                device,
                cameraBytes,
                frame.cameraCB,
                reinterpret_cast<void**>(&frame.cameraMapped)) ||
                !GFX::D3D12_BUFFER::CreateDefaultBuffer(
                    device,
                    materialDataBytes,
                    frame.materialDataBuffer) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    paletteBytes,
                    frame.jointPaletteCB,
                    reinterpret_cast<void**>(&frame.jointPaletteMapped))) {
                return false;
            }
            frame.materialDataState = D3D12_RESOURCE_STATE_COMMON;

            const UINT materialDataSrvIndex =
                GFX::DESCRIPTOR::ToFrameIndex(
                    GFX::DESCRIPTOR::SystemSrv::ShadowMaterialDataFrame0,
                    frameIndex);
            const D3D12_CPU_DESCRIPTOR_HANDLE materialDataSrvCpu =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, materialDataSrvIndex);
            frame.materialDataSrvGpu =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, materialDataSrvIndex);
            device->CreateShaderResourceView(
                frame.materialDataBuffer.Get(),
                &materialDataSrv,
                materialDataSrvCpu);
            GFX::SetD3D12Name(frame.materialDataBuffer.Get(), L"Shadow MaterialData Buffer");
        }

        ActivateShadowFrameResources(graphics.frameIndex);
        return true;
    }

} // namespace HIKARI::SHADOW::INTERNAL
