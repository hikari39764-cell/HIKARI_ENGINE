#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Shadow/Pipeline/HIKARI_ShadowRootParameters.h"

namespace HIKARI::SHADOW::INTERNAL {

    void BindShadowGpuDrivenFrameResources(
        ShadowRendererState& state, const GFX::Context& graphics,
        ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* meshletVisibleRangeBuffer,
        bool skinnedRoot) {

        ID3D12RootSignature* rootSig =
            skinnedRoot ? state.skinnedRootSig.Get() : state.rootSig.Get();
        if (cmd == nullptr || rootSig == nullptr) {
            return;
        }

        ShadowFrameResources& frame = GetActiveShadowFrameResources(state);
        cmd->SetGraphicsRootSignature(rootSig);
        cmd->SetGraphicsRootShaderResourceView(
            PIPELINE::kShadowStaticRootParamMaterialProgramParameters, frame.materialProgramParametersAddress);
        cmd->SetGraphicsRootShaderResourceView(
            PIPELINE::kShadowStaticRootParamMaterialProgramResources, frame.materialProgramResourcesAddress);
        cmd->SetGraphicsRootConstantBufferView(
            PIPELINE::kShadowStaticRootParamMaterialCamera, frame.materialCameraAddress);
        cmd->SetGraphicsRootConstantBufferView(
            PIPELINE::kShadowStaticRootParamCamera,
            GetActiveShadowCameraGpuAddress(state));
        cmd->SetGraphicsRootConstantBufferView(
            PIPELINE::kShadowStaticRootParamCullingCamera,
            GetActiveShadowCameraGpuAddress(state));
        if (frame.materialDataSrvGpu.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(
                PIPELINE::kShadowStaticRootParamMaterialData,
                frame.materialDataSrvGpu);
        }
        if (state.surfaceGpuSceneBuffer.GetSrv().ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(
                PIPELINE::kShadowStaticRootParamSurfaceGpuScene,
                state.surfaceGpuSceneBuffer.GetSrv());
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE texturePoolSrv =
            RENDER3D::GetMaterialTexturePoolSrvGpuHandle(graphics);
        if (texturePoolSrv.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(
                PIPELINE::kShadowStaticRootParamTexturePool,
                texturePoolSrv);
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE clusterPoolSrv =
            RENDER3D::GetClusterGeometryPoolSrvGpuHandle(graphics);
        if (clusterPoolSrv.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(
                PIPELINE::kShadowStaticRootParamClusterGeometryPool,
                clusterPoolSrv);
        }
        if (meshletVisibleRangeBuffer != nullptr) {
            cmd->SetGraphicsRootShaderResourceView(
                PIPELINE::kShadowStaticRootParamMeshletVisibleRanges,
                meshletVisibleRangeBuffer->GetGPUVirtualAddress());
        }
        if (frame.meshJointPaletteAddress != 0u) {
            cmd->SetGraphicsRootShaderResourceView(
                PIPELINE::kShadowStaticRootParamDeformationPalettes,
                frame.meshJointPaletteAddress);
        }
        const D3D12_GPU_VIRTUAL_ADDRESS sandDataAddress =
            state.sandSurfaceGpuFrameBuffer.
                GetRecordGpuVirtualAddress();
        if (sandDataAddress != 0u) {
            cmd->SetGraphicsRootShaderResourceView(
                PIPELINE::kShadowStaticRootParamSandSurfaceData,
                sandDataAddress);
        }
        const D3D12_GPU_VIRTUAL_ADDRESS sandHeightAddress =
            state.sandSurfaceGpuFrameBuffer.
                GetHeightGpuVirtualAddress();
        if (sandHeightAddress != 0u) {
            cmd->SetGraphicsRootShaderResourceView(
                PIPELINE::kShadowStaticRootParamSandSurfaceHeights,
                sandHeightAddress);
        }
        cmd->SetGraphicsRoot32BitConstant(
            PIPELINE::kShadowStaticRootParamMaterialIndex,
            0u,
            0);
    }

} // namespace HIKARI::SHADOW::INTERNAL
