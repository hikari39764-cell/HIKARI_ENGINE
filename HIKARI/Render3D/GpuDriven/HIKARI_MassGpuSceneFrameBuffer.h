#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuScene.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    inline constexpr size_t kMassGpuSceneMinimumCapacity = 16384u;
    inline constexpr size_t kMassGpuSceneMaximumCapacity = 131072u;
    static_assert(kMassGpuSceneMaximumCapacity ==
        kMassGpuSceneMaximumSourceInstanceCount);

    struct MassGpuSceneFrameBufferStats {
        size_t capacity = 0u;
        size_t requestedInstanceCount = 0u;
        size_t residentInstanceCount = 0u;
        size_t uploadedInstanceCount = 0u;
        size_t uploadRangeCount = 0u;
        size_t uploadedBytes = 0u;
        size_t overflowInstanceCount = 0u;
        size_t materialBindingVisitCount = 0u;
        size_t materialBindingMissingCount = 0u;
        uint32_t resourceGrowthCount = 0u;
        uint64_t publicationFrameIndex = 0u;
        uint64_t worldIdentity = 0u;
        uint64_t layoutVersion = 0u;
        uint64_t sourceVersion = 0u;
        uint64_t publicationGeneration = 0u;
        bool initialized = false;
        bool sceneResident = false;
        bool reusedResidentFrame = false;
        bool patchedDirtyRanges = false;
        bool uploadedFullScene = false;
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0u;
    };

    class MassGpuSceneFrameBuffer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            ID3D12DescriptorHeap* srvHeap);
        void Shutdown();
        void InvalidateResidency() noexcept;

        bool UploadFrame(
            uint32_t frameIndex,
            ID3D12GraphicsCommandList* commandList,
            const MassGpuSceneSource* source,
            std::span<const uint32_t> materialBindings,
            uint64_t materialBindingVersion);

        D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const noexcept;
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrv() const noexcept;
        const MassGpuSceneFrameBufferStats& GetStats() const noexcept;

    private:
        struct FrameSlot {
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer{};
            Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuffer{};
            RUNTIME::MassRenderProxyGpuRecord* mapped = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
            D3D12_RESOURCE_STATES defaultState =
                D3D12_RESOURCE_STATE_COMMON;
            uint64_t worldIdentity = 0u;
            uint64_t layoutVersion = 0u;
            uint64_t sourceVersion = 0u;
            uint64_t materialBindingVersion = 0u;
            size_t residentInstanceCount = 0u;
            size_t capacity = 0u;
            bool resident = false;
        };

        bool EnsureCapacity(size_t requestedCapacity);
        bool CreateSlot(
            size_t capacity,
            FrameSlot& outSlot);
        void RetireSlot(FrameSlot& slot) noexcept;
        static size_t GrowCapacity(size_t requestedCapacity) noexcept;
        FrameSlot* ActiveSlot() noexcept;
        const FrameSlot* ActiveSlot() const noexcept;
        void ResetStatsForFrame();

        ID3D12Device* device_ = nullptr;
        ID3D12DescriptorHeap* srvHeap_ = nullptr;
        UINT descriptorSize_ = 0u;
        uint32_t activeSlotIndex_ = 0u;
        std::array<FrameSlot, GFX::kFrameResourceCount> slots_{};
        std::vector<RUNTIME::MassRenderDirtyRange> uploadRanges_{};
        MassGpuSceneFrameBufferStats stats_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
