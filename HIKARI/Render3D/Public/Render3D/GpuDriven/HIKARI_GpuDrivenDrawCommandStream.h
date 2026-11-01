#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <d3d12.h>

#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuCommandBuildResult.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

struct GpuDrivenTraditionalIndirectView;

enum class GpuDrivenCommandProducerKind : uint32_t {
    None,
    GpuCommandBuilder,
    GpuSceneRegistry,
    GpuCompactedIndirect,
};

constexpr size_t kGpuDrivenDrawCommandBackendSlotCount = 3u;

size_t ToDrawCommandBackendSlot(GeometryBackendKind backend);

// 1 パス内だけで使う非所有ビュー。検証とバケットのアドレス計算を共有する。
struct GpuDrivenIndirectBucketStream {
    ID3D12Resource* arguments = nullptr;
    ID3D12Resource* counters = nullptr;
    ID3D12CommandSignature* signature = nullptr;
    UINT64 argumentOffset = 0, counterOffset = 0;
    UINT64 argumentStride = 0, counterStride = 0;
    UINT commandLimit = 0;
    size_t bucketCount = 0;
    void Execute(ID3D12GraphicsCommandList* cmd, size_t bucket) const;
};

struct GpuDrivenDrawCommandRange {
    GpuDrivenPassKind pass = GpuDrivenPassKind::ForwardOpaque;
    GpuDrivenPassKind sourcePass = GpuDrivenPassKind::ForwardOpaque;
    GeometryBackendKind backend = GeometryBackendKind::GpuDrivenTraditionalVsPs;
    GpuDrivenCommandProducerKind producer = GpuDrivenCommandProducerKind::None;

    const GpuDrivenCommandPassLayout* gpuCommandLayout = nullptr;
    const GpuDrivenTraditionalIndirectView* traditionalIndirect = nullptr;
    ID3D12Resource* argumentBuffer = nullptr;
    ID3D12Resource* skinnedArgumentBuffer = nullptr;
    ID3D12Resource* counterBuffer = nullptr;
    ID3D12CommandSignature* commandSignature = nullptr;
    ID3D12CommandSignature* skinnedCommandSignature = nullptr;
    UINT64 argumentBufferOffset = 0;
    UINT64 skinnedArgumentBufferOffset = 0;
    UINT64 counterBufferOffset = 0;
    UINT64 skinnedCounterBufferOffset = 0;
    UINT64 argumentBucketStride = 0;
    UINT64 skinnedArgumentBucketStride = 0;
    UINT64 counterBucketStride = 0;

    uint32_t gpuSceneBaseIndex = 0;
    size_t commandCount = 0;
    size_t staticCommandCount = 0;
    size_t skinnedCommandCount = 0;
    size_t visibleCommandCount = 0;
    size_t visibleCommandOverflowCount = 0;
    size_t commandBucketCapacity = 0;
    size_t commandBucketCount = 0;
    size_t recordCount = 0;
    size_t instanceCount = 0;
    bool consumable = false;
    bool gpuAuthored = false;
    bool gpuCounterBacked = false;
    bool visibleCommandCountKnown = false;

    void Reset();
    bool IsActive() const;
    bool HasGpuCommandLayout() const;
    std::optional<GpuDrivenIndirectBucketStream> TraditionalStream(bool skinned) const;
};

struct GpuDrivenPassDrawCommandStream {
    std::array<GpuDrivenDrawCommandRange, kGpuDrivenDrawCommandBackendSlotCount> ranges{};

    void Reset(GpuDrivenPassKind pass);
    GpuDrivenDrawCommandRange& GetOrCreateRange(GeometryBackendKind backend);
    const GpuDrivenDrawCommandRange* FindRange(GeometryBackendKind backend) const;
    GpuDrivenDrawCommandRange* FindRange(GeometryBackendKind backend);
};

struct GpuDrivenDrawCommandStream {
    std::array<GpuDrivenPassDrawCommandStream, kGpuDrivenPassCount> passes{};

    void Reset();
    GpuDrivenPassDrawCommandStream& GetPass(GpuDrivenPassKind pass);
    const GpuDrivenPassDrawCommandStream& GetPass(GpuDrivenPassKind pass) const;
    GpuDrivenDrawCommandRange& SetRange(const GpuDrivenDrawCommandRange& range);
    const GpuDrivenDrawCommandRange* FindRange(GpuDrivenPassKind pass, GeometryBackendKind backend) const;
    size_t CountGpuAuthoredCommands() const;
    size_t CountTraditionalIndirectCommands() const;
};

} // namespace HIKARI::RENDER3D::GPUDRIVEN
