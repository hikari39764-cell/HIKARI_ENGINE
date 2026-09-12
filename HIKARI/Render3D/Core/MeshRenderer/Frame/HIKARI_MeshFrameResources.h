#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"

namespace HIKARI::MESHRENDERER {

    struct MeshFrameResources {
        Microsoft::WRL::ComPtr<ID3D12Resource> cameraCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> cullingCameraCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectDataUploadBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialProgramParameterBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialProgramResourceBuffer;
        std::vector<MaterialGpuData> materialDataStaging;
        std::vector<MATH::Vec4> materialProgramParameterStaging;
        std::vector<uint32_t> materialProgramResourceStaging;
        DX::DynamicUploadBuffer materialUploads;
        bool materialUploadNeedsRetry = false;
        bool materialProgramUploadNeedsRetry = false;
        Microsoft::WRL::ComPtr<ID3D12Resource> lightCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> localLightBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> localLightClusterHeaderBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> localLightClusterIndexBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> skyEnvironmentCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> jointPaletteCB;

        CameraCB* cameraMapped = nullptr;
        CameraCB* cullingCameraMapped = nullptr;
        ObjectCB* objectMapped = nullptr;
        ObjectGpuData* objectDataMapped = nullptr;
        LightCB* lightMapped = nullptr;
        RENDER3D::LIGHTING::LocalLightGpuData* localLightMapped = nullptr;
        RENDER3D::LIGHTING::LocalLightClusterHeader*
            localLightClusterHeaderMapped = nullptr;
        uint32_t* localLightClusterIndexMapped = nullptr;
        RENDER3D::LIGHTING::LocalLightListView localLightListView{};
        ShadowCB* shadowMapped = nullptr;
        SkyEnvironmentCB* skyEnvironmentMapped = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;

        D3D12_CPU_DESCRIPTOR_HANDLE objectDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrvGpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE materialDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrvGpu{};
        D3D12_RESOURCE_STATES objectDataState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES materialDataState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES materialProgramParameterState =
            D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES materialProgramResourceState =
            D3D12_RESOURCE_STATE_COMMON;
    };

    class MeshFrameResourceStore final {
    public:
        bool Initialize(
            ID3D12Device* device,
            ID3D12DescriptorHeap* srvHeap);

        void Activate(uint32_t frameIndex);

        uint32_t GetActiveFrameIndex() const {
            return activeFrameIndex_;
        }

        MeshFrameResources& Active() {
            return frames_[activeFrameIndex_];
        }

        const MeshFrameResources& Active() const {
            return frames_[activeFrameIndex_];
        }

        bool HasActiveDrawResources() const;

        bool CommitActiveMaterialRanges(
            ID3D12GraphicsCommandList* commandList,
            const std::vector<RENDER3D::MATERIAL::GpuMaterialUploadRange>& ranges);

        bool CommitActiveMaterialProgramBlockRanges(
            ID3D12GraphicsCommandList* commandList,
            const std::vector<RENDER3D::MATERIAL::GpuMaterialUploadRange>& ranges);

    private:
        std::array<MeshFrameResources, GFX::kFrameResourceCount> frames_{};
        uint32_t activeFrameIndex_ = 0;
    };

} // namespace HIKARI::MESHRENDERER
