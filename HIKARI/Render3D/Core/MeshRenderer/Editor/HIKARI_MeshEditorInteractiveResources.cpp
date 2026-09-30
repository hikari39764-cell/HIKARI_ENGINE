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

} // namespace

void ResetEditorInteractiveResources() {
    gMeshRendererState.editorInteractive.worldSpritePass.Shutdown();
    gMeshRendererState.editorInteractive.worldTextPass.Shutdown();
    for (EditorInteractiveMeshFrameResources& frame : gMeshRendererState.editorInteractive.frames) {
        frame = {};
    }

    gMeshRendererState.editorInteractive.gpuDrivenLayer = {};
    gMeshRendererState.editorInteractive.clusterGpuDrivenProducer.Attach(nullptr);
    gMeshRendererState.editorInteractive.clusterGpuCullingPass.Reset();
    gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer = {};
    gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer = {};
    gMeshRendererState.editorInteractive.sceneResidency.Reset();
    for (RENDER3D::RenderResourceView& view : gMeshRendererState.editorInteractive.surfaceGpuSceneViews) {
        if (view.IsValid()) {
            (void)RENDER3D::ReleaseRenderResourceDescriptor(view);
        }
        view = {};
    }
    for (RENDER3D::RenderResourceView& view : gMeshRendererState.editorInteractive.massGpuSceneNullViews) {
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
    if (device == nullptr || GetStaticRootSignature(gMeshRendererState.pipelines) == nullptr) {
        return false;
    }

    for (EditorInteractiveMeshFrameResources& frame : gMeshRendererState.editorInteractive.frames) {
        if (!frame.view.Initialize(device)) {
            ResetEditorInteractiveResources();
            return false;
        }
    }

    RENDER3D::UpdateRenderResourceDescriptorPoolContext(GFX::GetCurrentContext());
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> sceneSrvCpu{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> sceneSrvGpu{};
    for (uint32_t frameIndex = 0; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
        const std::vector<RENDER3D::RenderResourceView> range = RENDER3D::AllocateRenderResourceDescriptorRange(2u);
        if (range.size() != 2u) {
            ResetEditorInteractiveResources();
            return false;
        }
        RENDER3D::RenderResourceView& view = gMeshRendererState.editorInteractive.surfaceGpuSceneViews[frameIndex];
        view = range[0];
        RENDER3D::RenderResourceView& massNullView =
            gMeshRendererState.editorInteractive.massGpuSceneNullViews[frameIndex];
        massNullView = range[1];
        if (!view.IsValid() || !massNullView.IsValid()) {
            ResetEditorInteractiveResources();
            return false;
        }
        sceneSrvCpu[frameIndex] = view.cpu;
        sceneSrvGpu[frameIndex] = view.gpu;
        D3D12_SHADER_RESOURCE_VIEW_DESC nullMassSrv{};
        nullMassSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullMassSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        nullMassSrv.Format = DXGI_FORMAT_UNKNOWN;
        nullMassSrv.Buffer.NumElements = 1u;
        nullMassSrv.Buffer.StructureByteStride = sizeof(RENDER3D::RUNTIME::MassRenderProxyGpuRecord);
        device->CreateShaderResourceView(nullptr, &nullMassSrv, massNullView.cpu);
    }

    if (!gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer.Initialize(device, sceneSrvCpu, sceneSrvGpu)) {
        ResetEditorInteractiveResources();
        return false;
    }
    if (!gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer.Initialize(device)) {
        ResetEditorInteractiveResources();
        return false;
    }

    (void)gMeshRendererState.editorInteractive.clusterGpuCullingPass.Initialize(
        GFX::GetCurrentContext(), GetStaticRootSignature(gMeshRendererState.pipelines),
        ROOT_PARAM::SurfaceGpuSceneControl, 4u, kEditorPreviewClusterPageTaskCapacity,
        kEditorPreviewVisibleRangeCapacity, kEditorPreviewCandidateCommandCapacity);
    gMeshRendererState.editorInteractive.clusterGpuDrivenProducer.Attach(
        &gMeshRendererState.editorInteractive.clusterGpuCullingPass);
    gMeshRendererState.editorInteractive.gpuDrivenLayer.Attach(
        &gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer, nullptr, nullptr,
        &gMeshRendererState.editorInteractive.clusterGpuDrivenProducer,
        &gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer,
        &gMeshRendererState.terrainSurfaceGpuFrameBuffer);
    if (!gMeshRendererState.editorInteractive.gpuDrivenLayer.Initialize(
            device, GetStaticRootSignature(gMeshRendererState.pipelines), ROOT_PARAM::SurfaceGpuSceneControl, 4u)) {
        ResetEditorInteractiveResources();
        return false;
    }
    gMeshRendererState.editorInteractive.initialized = true;
    return true;
}

} // namespace HIKARI::MESHRENDERER::INTERNAL

#endif
