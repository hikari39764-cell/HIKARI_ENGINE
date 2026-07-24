#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    constexpr size_t kDefaultSurfaceGpuSceneInstanceCapacity = 8192u;

    struct SurfaceGpuSceneFrameBufferStats {
        size_t capacity = 0;
        size_t requestedInstanceCount = 0;
        size_t uploadedInstanceCount = 0;
        size_t committedInstanceCount = 0;
        size_t committedBytes = 0;
        size_t overflowInstanceCount = 0;
        size_t uploadCallCount = 0;
        size_t materialPatchCount = 0;
        size_t materialPatchChangedCount = 0;
        size_t materialPatchUnchangedCount = 0;
        size_t materialBindingVisitCount = 0;
        size_t materialBindingMissingCount = 0;
        uint64_t materialBindingVersion = 0;
        bool initialized = false;
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    };

    class SurfaceGpuSceneFrameBuffer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpu,
            UINT descriptorSize,
            size_t capacity = kDefaultSurfaceGpuSceneInstanceCapacity);
        bool Initialize(
            ID3D12Device* device,
            const std::array<
                D3D12_CPU_DESCRIPTOR_HANDLE,
                GFX::kFrameResourceCount>& srvCpu,
            const std::array<
                D3D12_GPU_DESCRIPTOR_HANDLE,
                GFX::kFrameResourceCount>& srvGpu,
            size_t capacity = kDefaultSurfaceGpuSceneInstanceCapacity);

        void BeginFrame(uint32_t frameIndex);
        void ResetFrame();
        bool CanReuseFrame(
            size_t residentInstanceCount,
            uint64_t layoutVersion,
            uint64_t sourceVersion) const;
        bool CanPatchFrame(
            size_t residentInstanceCount,
            uint64_t layoutVersion,
            uint64_t baseSourceVersion) const;
        void ReuseFrame(size_t residentInstanceCount);
        void Upload(const RUNTIME::SurfaceGpuSceneInstance* instances, size_t count);
        void Upload(const std::vector<RUNTIME::SurfaceGpuSceneInstance>& instances);
        bool UpdateRange(
            size_t firstInstance,
            const RUNTIME::SurfaceGpuSceneInstance* instances,
            size_t count);
        bool PatchMaterialDataIndex(size_t instanceIndex, uint32_t materialDataIndex);
        bool PatchMaterialDataIndexChecked(
            size_t instanceIndex,
            uint32_t materialDataIndex,
            uint32_t expectedSourceRecordIndex,
            uint32_t expectedSourceSurfaceInstanceIndex);
        void ApplyMaterialBindings(
            std::span<const uint32_t> materialSlotBySourceRecord,
            uint64_t bindingVersion);
        bool HasMaterialDataIndex(size_t instanceIndex) const;
        void MarkResident(uint64_t layoutVersion, uint64_t sourceVersion, size_t instanceCount);
        void CommitFrame(ID3D12GraphicsCommandList* commandList);

        D3D12_GPU_DESCRIPTOR_HANDLE GetSrv() const;
        D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const;
        const SurfaceGpuSceneFrameBufferStats& GetStats() const;

    private:
        struct FrameSlot {
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuffer;
            RUNTIME::SurfaceGpuSceneInstance* mapped = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
            D3D12_RESOURCE_STATES defaultState = D3D12_RESOURCE_STATE_COMMON;
            size_t cursor = 0;
            size_t residentInstanceCount = 0;
            size_t dirtyFirstInstance = 0;
            size_t dirtyEndInstance = 0;
            uint64_t layoutVersion = 0;
            uint64_t sourceVersion = 0;
            uint64_t materialBindingVersion = 0;
            bool resident = false;
            bool dirty = false;
        };

        FrameSlot* ActiveSlot();
        const FrameSlot* ActiveSlot() const;
        void MarkDirtyRange(FrameSlot& slot, size_t firstInstance, size_t endInstance);

        std::array<FrameSlot, GFX::kFrameResourceCount> slots_{};
        uint32_t activeSlotIndex_ = 0;
        size_t capacity_ = 0;
        SurfaceGpuSceneFrameBufferStats stats_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
