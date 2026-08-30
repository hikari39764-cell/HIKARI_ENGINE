#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#if defined(HIKARI_WITH_EDITOR)

#include <array>

#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::MESHRENDERER::INTERNAL {

    namespace {

        constexpr size_t kEditorPreviewClusterPageTaskCapacity = 131072u;
        constexpr size_t kEditorPreviewVisibleRangeCapacity = 131072u;
        constexpr size_t kEditorPreviewCandidateCommandCapacity = 131072u;

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

        template <typename T>
        bool CreateMappedArrayBuffer(
            ID3D12Device* device,
            uint32_t count,
            Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
            T*& mapped) {

            return GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                device,
                static_cast<UINT64>(sizeof(T)) * count,
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
            if (frame.localLightBuffer != nullptr &&
                frame.localLightMapped != nullptr) {
                frame.localLightBuffer->Unmap(0, nullptr);
            }
            if (frame.localLightClusterHeaderBuffer != nullptr &&
                frame.localLightClusterHeaderMapped != nullptr) {
                frame.localLightClusterHeaderBuffer->Unmap(
                    0,
                    nullptr);
            }
            if (frame.localLightClusterIndexBuffer != nullptr &&
                frame.localLightClusterIndexMapped != nullptr) {
                frame.localLightClusterIndexBuffer->Unmap(
                    0,
                    nullptr);
            }
            if (frame.shadowCB != nullptr && frame.shadowMapped != nullptr) {
                frame.shadowCB->Unmap(0, nullptr);
            }
            if (frame.skyEnvironmentCB != nullptr &&
                frame.skyEnvironmentMapped != nullptr) {
                frame.skyEnvironmentCB->Unmap(0, nullptr);
            }
            if (frame.jointPaletteCB != nullptr &&
                frame.jointPaletteMapped != nullptr) {
                frame.jointPaletteCB->Unmap(0, nullptr);
            }
            frame = {};
        }

        gMeshRendererState.editorInteractive.gpuDrivenLayer = {};
        gMeshRendererState.editorInteractive.clusterGpuDrivenProducer.Attach(
            nullptr);
        gMeshRendererState.editorInteractive.clusterGpuCullingPass.Reset();
        gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer = {};
        gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer = {};
        gMeshRendererState.editorInteractive.sceneResidency.Reset();
        for (RENDER3D::RenderResourceView& view :
            gMeshRendererState.editorInteractive.surfaceGpuSceneViews) {

            if (view.IsValid()) {
                (void)RENDER3D::ReleaseRenderResourceDescriptor(view);
            }
            view = {};
        }
        for (RENDER3D::RenderResourceView& view :
            gMeshRendererState.editorInteractive.massGpuSceneNullViews) {
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

        ID3D12Device* device = GFX::GetCurrentContext().device;
        if (device == nullptr ||
            GetStaticRootSignature(gMeshRendererState.pipelines) == nullptr) {
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
                !CreateMappedArrayBuffer(
                    device,
                    RENDER3D::LIGHTING::kMaximumLocalLightCount,
                    frame.localLightBuffer,
                    frame.localLightMapped) ||
                !CreateMappedArrayBuffer(
                    device,
                    RENDER3D::LIGHTING::
                        kMaximumLocalLightClusterCount,
                    frame.localLightClusterHeaderBuffer,
                    frame.localLightClusterHeaderMapped) ||
                !CreateMappedArrayBuffer(
                    device,
                    RENDER3D::LIGHTING::
                        kMaximumLocalLightClusterIndexCount,
                    frame.localLightClusterIndexBuffer,
                    frame.localLightClusterIndexMapped) ||
                !CreateMappedConstantBuffer(
                    device,
                    frame.shadowCB,
                    frame.shadowMapped) ||
                !CreateMappedConstantBuffer(
                    device,
                    frame.skyEnvironmentCB,
                    frame.skyEnvironmentMapped) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    static_cast<UINT64>(
                        GFX::AlignD3D12ConstantBufferByteSize(
                            sizeof(JointPaletteCB))) *
                        kMaxObjectCount,
                    frame.jointPaletteCB,
                    reinterpret_cast<void**>(
                        &frame.jointPaletteMapped))) {

                ResetEditorInteractiveResources();
                return false;
            }
        }

        RENDER3D::UpdateRenderResourceDescriptorPoolContext(GFX::GetCurrentContext());
        std::array<
            D3D12_CPU_DESCRIPTOR_HANDLE,
            GFX::kFrameResourceCount> sceneSrvCpu{};
        std::array<
            D3D12_GPU_DESCRIPTOR_HANDLE,
            GFX::kFrameResourceCount> sceneSrvGpu{};
        const UINT descriptorSize =
            device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (uint32_t frameIndex = 0;
            frameIndex < GFX::kFrameResourceCount;
            ++frameIndex) {

            RENDER3D::RenderResourceView& view =
                gMeshRendererState.editorInteractive.surfaceGpuSceneViews[frameIndex];
            view = RENDER3D::AllocateRenderResourceDescriptor();
            RENDER3D::RenderResourceView& massNullView =
                gMeshRendererState.editorInteractive.
                    massGpuSceneNullViews[frameIndex];
            massNullView = RENDER3D::AllocateRenderResourceDescriptor();
            if (!view.IsValid() || !massNullView.IsValid() ||
                massNullView.gpu.ptr != view.gpu.ptr + descriptorSize) {
                ResetEditorInteractiveResources();
                return false;
            }
            sceneSrvCpu[frameIndex] = view.cpu;
            sceneSrvGpu[frameIndex] = view.gpu;
            D3D12_SHADER_RESOURCE_VIEW_DESC nullMassSrv{};
            nullMassSrv.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullMassSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            nullMassSrv.Format = DXGI_FORMAT_UNKNOWN;
            nullMassSrv.Buffer.NumElements = 1u;
            nullMassSrv.Buffer.StructureByteStride = sizeof(
                RENDER3D::RUNTIME::MassRenderProxyGpuRecord);
            device->CreateShaderResourceView(
                nullptr,
                &nullMassSrv,
                massNullView.cpu);
        }

        if (!gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer.Initialize(
                device,
                sceneSrvCpu,
                sceneSrvGpu)) {

            ResetEditorInteractiveResources();
            return false;
        }
        if (!gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer.Initialize(
                device)) {

            ResetEditorInteractiveResources();
            return false;
        }

        (void)gMeshRendererState.editorInteractive.clusterGpuCullingPass.Initialize(
            GFX::GetCurrentContext(),
            GetStaticRootSignature(gMeshRendererState.pipelines),
            ROOT_PARAM::SurfaceGpuSceneControl,
            4u,
            kEditorPreviewClusterPageTaskCapacity,
            kEditorPreviewVisibleRangeCapacity,
            kEditorPreviewCandidateCommandCapacity);
        gMeshRendererState.editorInteractive.clusterGpuDrivenProducer.Attach(
            &gMeshRendererState.editorInteractive.clusterGpuCullingPass);
        gMeshRendererState.editorInteractive.gpuDrivenLayer.Attach(
            &gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer,
            nullptr,
            nullptr,
            &gMeshRendererState.editorInteractive.clusterGpuDrivenProducer,
            &gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer,
            &gMeshRendererState.sandSurfaceGpuFrameBuffer);
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
