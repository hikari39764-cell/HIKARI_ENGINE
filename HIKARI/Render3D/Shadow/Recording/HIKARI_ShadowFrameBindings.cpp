#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include "HIKARI_Services.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Shadow/Pipeline/HIKARI_ShadowRootParameters.h"

namespace HIKARI::SHADOW::INTERNAL {

    void BindShadowGpuDrivenFrameResources(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* meshletVisibleRangeBuffer,
        ID3D12Resource* meshletVisibleClusterListBuffer,
        bool skinnedRoot) {

        ID3D12RootSignature* rootSig =
            skinnedRoot ? gShadowRendererState.skinnedRootSig.Get() : gShadowRendererState.rootSig.Get();
        if (cmd == nullptr || rootSig == nullptr) {
            return;
        }

        ShadowFrameResources& frame = GetActiveShadowFrameResources();
        cmd->SetGraphicsRootSignature(rootSig);
        cmd->SetGraphicsRootConstantBufferView(
            PIPELINE::kShadowStaticRootParamCamera,
            frame.cameraCB != nullptr
                ? frame.cameraCB->GetGPUVirtualAddress()
                : 0u);
        cmd->SetGraphicsRootConstantBufferView(
            PIPELINE::kShadowStaticRootParamCullingCamera,
            frame.cameraCB != nullptr
                ? frame.cameraCB->GetGPUVirtualAddress()
                : 0u);
        if (frame.materialDataSrvGpu.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(
                PIPELINE::kShadowStaticRootParamMaterialData,
                frame.materialDataSrvGpu);
        }
        if (gShadowRendererState.surfaceGpuSceneBuffer.GetSrv().ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(
                PIPELINE::kShadowStaticRootParamSurfaceGpuScene,
                gShadowRendererState.surfaceGpuSceneBuffer.GetSrv());
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE texturePoolSrv =
            RENDER3D::GetMaterialTexturePoolSrvGpuHandle(SERVICES::gCtx);
        if (texturePoolSrv.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(
                PIPELINE::kShadowStaticRootParamTexturePool,
                texturePoolSrv);
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE clusterPoolSrv =
            RENDER3D::GetClusterGeometryPoolSrvGpuHandle(SERVICES::gCtx);
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
        if (meshletVisibleClusterListBuffer != nullptr) {
            cmd->SetGraphicsRootShaderResourceView(
                PIPELINE::kShadowStaticRootParamMeshletVisibleClusterList,
                meshletVisibleClusterListBuffer->GetGPUVirtualAddress());
        }
        if (frame.jointPaletteCB != nullptr) {
            cmd->SetGraphicsRootShaderResourceView(
                PIPELINE::kShadowStaticRootParamDeformationPalettes,
                frame.jointPaletteCB->GetGPUVirtualAddress());
        }
        cmd->SetGraphicsRoot32BitConstant(
            PIPELINE::kShadowStaticRootParamMaterialIndex,
            0u,
            0);
    }

} // namespace HIKARI::SHADOW::INTERNAL
