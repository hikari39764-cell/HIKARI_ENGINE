#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

#if defined(HIKARI_WITH_EDITOR)

#include <array>

#include "HIKARI_Services.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::MESHRENDERER::INTERNAL {

    namespace {

        template <typename T>
        bool CreateMappedConstantBuffer(
            ID3D12Device* device,
            Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
            T*& mapped) {

            return GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                device,
                GFX::AlignD3D12ConstantBufferByteSize(sizeof(T)),
                resource,
                reinterpret_cast<void**>(&mapped));
        }

    } // namespace

    void ResetEditorInteractiveResources() {
        for (EditorInteractiveMeshFrameResources& frame :
            gMeshRendererState.editorInteractive.frames) {

            if (frame.cameraCB != nullptr && frame.cameraMapped != nullptr) {
                frame.cameraCB->Unmap(0, nullptr);
            }
            if (frame.lightCB != nullptr && frame.lightMapped != nullptr) {
                frame.lightCB->Unmap(0, nullptr);
            }
            if (frame.shadowCB != nullptr && frame.shadowMapped != nullptr) {
                frame.shadowCB->Unmap(0, nullptr);
            }
            if (frame.skyEnvironmentCB != nullptr &&
                frame.skyEnvironmentMapped != nullptr) {
                frame.skyEnvironmentCB->Unmap(0, nullptr);
            }
            frame = {};
        }

        gMeshRendererState.editorInteractive.gpuDrivenLayer = {};
        gMeshRendererState.editorInteractive.traditionalCommandStreamBuffer = {};
        gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer = {};
        gMeshRendererState.editorInteractive.sceneResidency.Reset();
        for (RENDER3D::RenderResourceView& view :
            gMeshRendererState.editorInteractive.surfaceGpuSceneViews) {

            if (view.IsValid()) {
                (void)RENDER3D::ReleaseRenderResourceDescriptor(view);
            }
            view = {};
        }
        gMeshRendererState.editorInteractive.initialized = false;
    }

    bool EnsureEditorInteractiveResources() {
        if (gMeshRendererState.editorInteractive.initialized) {
            return true;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        if (device == nullptr ||
            GetStaticRootSignature(gMeshRendererState.pipelines) == nullptr ||
            GetSkinnedRootSignature(gMeshRendererState.pipelines) == nullptr) {
            return false;
        }

        for (EditorInteractiveMeshFrameResources& frame :
            gMeshRendererState.editorInteractive.frames) {

            if (!CreateMappedConstantBuffer(
                    device,
                    frame.cameraCB,
                    frame.cameraMapped) ||
                !CreateMappedConstantBuffer(
                    device,
                    frame.lightCB,
                    frame.lightMapped) ||
                !CreateMappedConstantBuffer(
                    device,
                    frame.shadowCB,
                    frame.shadowMapped) ||
                !CreateMappedConstantBuffer(
                    device,
                    frame.skyEnvironmentCB,
                    frame.skyEnvironmentMapped)) {

                ResetEditorInteractiveResources();
                return false;
            }
        }

        RENDER3D::UpdateRenderResourceDescriptorPoolContext(SERVICES::gCtx);
        std::array<
            D3D12_CPU_DESCRIPTOR_HANDLE,
            GFX::kFrameResourceCount> sceneSrvCpu{};
        std::array<
            D3D12_GPU_DESCRIPTOR_HANDLE,
            GFX::kFrameResourceCount> sceneSrvGpu{};
        for (uint32_t frameIndex = 0;
            frameIndex < GFX::kFrameResourceCount;
            ++frameIndex) {

            RENDER3D::RenderResourceView& view =
                gMeshRendererState.editorInteractive.surfaceGpuSceneViews[frameIndex];
            view = RENDER3D::AllocateRenderResourceDescriptor();
            if (!view.IsValid()) {
                ResetEditorInteractiveResources();
                return false;
            }
            sceneSrvCpu[frameIndex] = view.cpu;
            sceneSrvGpu[frameIndex] = view.gpu;
        }

        if (!gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer.Initialize(
                device,
                sceneSrvCpu,
                sceneSrvGpu)) {

            ResetEditorInteractiveResources();
            return false;
        }

        if (!gMeshRendererState.editorInteractive.traditionalCommandStreamBuffer.Initialize(
                device,
                GetStaticRootSignature(gMeshRendererState.pipelines),
                ROOT_PARAM::SurfaceGpuSceneControl,
                4u) ||
            !gMeshRendererState.editorInteractive.traditionalCommandStreamBuffer
                .InitializeSkinnedCommandStream(
                    device,
                    GetSkinnedRootSignature(gMeshRendererState.pipelines),
                    ROOT_PARAM::SurfaceGpuSceneControl,
                    ROOT_PARAM::JointPalette)) {

            ResetEditorInteractiveResources();
            return false;
        }

        gMeshRendererState.editorInteractive.gpuDrivenLayer.Attach(
            &gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer,
            &gMeshRendererState.editorInteractive.traditionalCommandStreamBuffer,
            nullptr);
        if (!gMeshRendererState.editorInteractive.gpuDrivenLayer.Initialize(
                device,
                GetStaticRootSignature(gMeshRendererState.pipelines),
                ROOT_PARAM::SurfaceGpuSceneControl,
                4u)) {

            ResetEditorInteractiveResources();
            return false;
        }

        gMeshRendererState.editorInteractive.initialized = true;
        return true;
    }

} // namespace HIKARI::MESHRENDERER::INTERNAL

#endif
