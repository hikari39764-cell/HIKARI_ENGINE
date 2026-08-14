#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Water/HIKARI_WaterSurfaceGpuData.h"

namespace HIKARI::RENDER3D::WATER {

    // SurfaceGpuScene の最大 8192 records に index 0 sentinel を加えた容量。
    constexpr std::size_t kDefaultWaterSurfaceGpuDataCapacity = 8193u;

    class WaterSurfaceGpuFrameBuffer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            std::size_t capacity = kDefaultWaterSurfaceGpuDataCapacity);

        // 毎 frame slot に index 0 sentinel を必ず書く。空 scene も有効な SRV になる。
        bool UploadFrame(
            uint32_t frameIndex,
            const std::vector<WaterSurfaceGpuData>* source);

        D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const;
        std::size_t GetUploadedCount() const;
        std::size_t GetCapacity() const;
        bool IsInitialized() const;

    private:
        struct FrameSlot {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer{};
            WaterSurfaceGpuData* mapped = nullptr;
            std::size_t uploadedCount = 0u;
        };

        FrameSlot* ActiveSlot();
        const FrameSlot* ActiveSlot() const;

        std::array<FrameSlot, GFX::kFrameResourceCount> slots_{};
        uint32_t activeSlotIndex_ = 0u;
        std::size_t capacity_ = 0u;
    };

} // namespace HIKARI::RENDER3D::WATER
