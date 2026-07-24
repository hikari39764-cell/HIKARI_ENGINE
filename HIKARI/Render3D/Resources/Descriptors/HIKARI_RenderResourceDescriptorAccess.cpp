#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"

#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D {
    namespace {
        D3D12_GPU_DESCRIPTOR_HANDLE GetTextureResourcePoolSrvGpuHandle(
            const GFX::Context& context,
            UINT descriptorIndex) {

            ID3D12DescriptorHeap* heap = GetTextureResourceSrvHeap();
            if (context.device == nullptr || heap == nullptr) {
                return {};
            }

            const UINT descriptorSize =
                context.device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return GFX::DESCRIPTOR::GpuAt(
                heap,
                descriptorSize,
                descriptorIndex);
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetMaterialTexturePoolSrvGpuHandle(
        const GFX::Context& context) {

        return GetTextureResourcePoolSrvGpuHandle(
            context,
            GFX::DESCRIPTOR::kUserSrvBegin);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetClusterGeometryPoolSrvGpuHandle(
        const GFX::Context& context) {

        return GetTextureResourcePoolSrvGpuHandle(
            context,
            GFX::DESCRIPTOR::kSystemSrvDynamicBegin);
    }

} // namespace HIKARI::RENDER3D
