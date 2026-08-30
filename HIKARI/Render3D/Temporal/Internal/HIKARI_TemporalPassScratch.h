#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

namespace HIKARI::RENDER3D::TEMPORAL::DETAIL {

    inline constexpr uint32_t kTemporalMaximumGeometryDrawCount = 65'536u;
    inline constexpr uint32_t kTemporalMaximumSkinnedDrawCount = 2'048u;
    inline constexpr uint64_t kTemporalMaximumPassUploadBytes =
        64ull * 1024ull * 1024ull;

    enum class TemporalConstantUploadKind : uint8_t {
        MotionVector,
        TaaResolve,
    };

    struct TemporalMappedUploadSlot {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer{};
        uint8_t* mapped = nullptr;
        uint64_t capacityBytes = 0u;
        TemporalResourceFrame claimedFrame{};
        uint64_t retireFenceValue = 0u;
    };

    struct TemporalGeometryUploadSlot {
        Microsoft::WRL::ComPtr<ID3D12Resource> geometryBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> skinBuffer{};
        uint8_t* geometryMapped = nullptr;
        uint8_t* skinMapped = nullptr;
        uint64_t geometryCapacityBytes = 0u;
        uint64_t skinCapacityBytes = 0u;
        TemporalResourceFrame claimedFrame{};
        uint64_t retireFenceValue = 0u;
    };

    struct TemporalGeometryHistoryKey {
        uint64_t objectId = 0u;
        uint64_t objectLifetimeGeneration = 0u;
        uint64_t vertexBufferAddress = 0u;
        uint64_t indexBufferAddress = 0u;
        uint32_t sourceSurfaceInstanceIndex = UINT32_MAX;
        uint32_t startIndex = 0u;
        uint32_t indexCount = 0u;
        int32_t baseVertex = 0;

        bool IsValid() const noexcept {
            return objectId != 0u &&
                objectLifetimeGeneration != 0u &&
                vertexBufferAddress != 0u &&
                indexBufferAddress != 0u &&
                sourceSurfaceInstanceIndex != UINT32_MAX &&
                indexCount != 0u;
        }

        bool operator==(const TemporalGeometryHistoryKey&) const noexcept =
            default;
    };

    struct TemporalGeometryHistoryKeyHash {
        size_t operator()(
            const TemporalGeometryHistoryKey& key) const noexcept;
    };

    struct TemporalGeometryObjectHistory {
        MATH::Mat4 world{};
        std::vector<MATH::Mat4> jointPalette{};
        uint64_t frameIndex = 0u;
        uint64_t objectVersion = 0u;
    };

    struct TemporalPassScratchState {
        std::array<
            TemporalMappedUploadSlot,
            GFX::kFrameResourceCount> motionConstants{};
        std::array<
            TemporalMappedUploadSlot,
            GFX::kFrameResourceCount> taaConstants{};
        std::array<
            TemporalGeometryUploadSlot,
            GFX::kFrameResourceCount> geometryUploads{};
        std::unordered_map<
            TemporalGeometryHistoryKey,
            TemporalGeometryObjectHistory,
            TemporalGeometryHistoryKeyHash> geometryHistory{};
    };

    struct TemporalPassUploadSlice {
        uint8_t* mapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0u;
        uint64_t capacityBytes = 0u;
    };

    struct TemporalGeometryUploadSlice {
        uint8_t* geometryMapped = nullptr;
        uint8_t* skinMapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS geometryGpuAddress = 0u;
        D3D12_GPU_VIRTUAL_ADDRESS skinGpuAddress = 0u;
        uint64_t geometryCapacityBytes = 0u;
        uint64_t skinCapacityBytes = 0u;
    };

    struct TemporalResourceState;

    bool CanReuseTemporalPassSlot(
        TemporalResourceFrame claimedFrame,
        uint64_t retireFenceValue,
        TemporalResourceFrame requestedFrame,
        uint64_t completedFenceValue) noexcept;

    bool TryAcquireTemporalConstantUpload(
        const GFX::Context& graphics,
        TemporalResourceFrame frame,
        TemporalConstantUploadKind kind,
        uint64_t requiredBytes,
        const wchar_t* debugName,
        TemporalPassUploadSlice& outSlice);

    bool TryAcquireTemporalGeometryUpload(
        const GFX::Context& graphics,
        TemporalResourceFrame frame,
        uint64_t geometryBytes,
        uint64_t skinBytes,
        TemporalGeometryUploadSlice& outSlice);

    void ClearTemporalGeometryHistory(
        TemporalResourceState& state) noexcept;

    void ReleaseTemporalPassScratch(
        TemporalResourceState& state,
        TemporalResourceReleaseMode mode);

} // namespace HIKARI::RENDER3D::TEMPORAL::DETAIL
