#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Sand/HIKARI_SandSurfaceGpuData.h"

namespace HIKARI::RENDER3D::SAND {

    // SandSurfaceGpuData records と高さ float 配列をフレームスロットで
    // リングアップロードする。CPU グリッドが唯一の変異元であり、
    // Render3D は毎フレームの snapshot を memcpy するだけ。
    class SandSurfaceGpuFrameBuffer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            std::size_t dataCapacity = kSandSurfaceMaxGpuDataRecords,
            std::size_t heightCapacity = kSandSurfaceMaxHeightCells);

        bool UploadFrame(
            uint32_t frameIndex,
            const std::vector<SandSurfaceGpuData>* data,
            const std::vector<float>* heights);

        D3D12_GPU_VIRTUAL_ADDRESS GetDataGpuVirtualAddress() const;
        D3D12_GPU_VIRTUAL_ADDRESS GetHeightGpuVirtualAddress() const;
        std::size_t GetUploadedHeightCount() const;
        bool IsInitialized() const;

    private:
        struct FrameSlot {
            Microsoft::WRL::ComPtr<ID3D12Resource> dataBuffer{};
            SandSurfaceGpuData* dataMapped = nullptr;
            Microsoft::WRL::ComPtr<ID3D12Resource> heightBuffer{};
            float* heightMapped = nullptr;
            std::size_t uploadedHeightCount = 0u;
        };

        FrameSlot* ActiveSlot();
        const FrameSlot* ActiveSlot() const;

        std::array<FrameSlot, GFX::kFrameResourceCount> slots_{};
        uint32_t activeSlotIndex_ = 0u;
        std::size_t dataCapacity_ = 0u;
        std::size_t heightCapacity_ = 0u;
    };

} // namespace HIKARI::RENDER3D::SAND
