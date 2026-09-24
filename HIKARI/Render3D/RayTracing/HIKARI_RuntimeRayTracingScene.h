#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuDrivenSceneSource;
}
namespace HIKARI::RENDER3D::MATERIAL {
struct GpuMaterialData;
}

namespace HIKARI::RENDER3D::RAYTRACING {
enum class RuntimeRayTracingStatus : uint8_t {
    Disabled,
    UnsupportedDevice,
    InvalidInput,
    Building,
    IncompleteScene,
    BudgetExceeded,
    BuildFailed,
    Ready
};

struct RuntimeRayTracingBudget {
    uint64_t maximumBytes = 128ull * 1024 * 1024;
    uint32_t trianglesPerChunk = 131072;
    uint32_t rangesPerChunk = 128;
    uint32_t chunksPerAsset = 1024;
    uint32_t buildsPerFrame = 2;
    uint32_t geometryUploadsPerFrame = 1;
    uint32_t maximumInstances = 32768;
    uint32_t maximumHitRecords = 262144;
};

// InstanceID + GeometryIndex で参照する。BLAS は材質/オブジェクト間で共有する。
// firstPrimitive は分割 BLAS 内の PrimitiveIndex を HCMESH 全体へ戻す基点。
struct RuntimeRayTracingHitRecord {
    uint32_t geometryDescriptor = 0;
    uint32_t metadataDescriptor = 0;
    uint32_t materialIndex = 0xffffffffu;
    uint32_t flags = 0; // bit0: double sided, bit1: casts shadow
    uint32_t firstPrimitive = 0;
    uint32_t firstCluster = 0;
    uint32_t clusterCount = 0;
    uint32_t primitiveCount = 0;
};
static_assert(sizeof(RuntimeRayTracingHitRecord) == 32);

struct RuntimeRayTracingStats {
    RuntimeRayTracingStatus status = RuntimeRayTracingStatus::Disabled;
    uint32_t sourceSurfaces = 0;
    uint32_t unsupportedSurfaces = 0;
    uint32_t missingSurfaces = 0;
    uint32_t sharedAssets = 0;
    uint32_t readyChunks = 0;
    uint32_t pendingChunks = 0;
    uint32_t builtChunks = 0;
    uint32_t geometryUploads = 0;
    uint32_t instances = 0;
    uint32_t hitRecords = 0;
    uint64_t residentBytes = 0;
    uint64_t retiringBytes = 0;
    bool reusedTopLevel = false;
};

struct RuntimeRayTracingMaterialVersion {
    uint64_t data = 0;
    uint64_t bindings = 0;
    friend bool operator==(const RuntimeRayTracingMaterialVersion&, const RuntimeRayTracingMaterialVersion&) = default;
};

struct RuntimeRayTracingFrame {
    D3D12_GPU_VIRTUAL_ADDRESS accelerationStructure = 0;
    D3D12_GPU_VIRTUAL_ADDRESS hitRecords = 0;
    uint32_t hitRecordCount = 0;
    uint64_t version = 0;
    uint64_t retireFenceValue = 0;
    bool complete = false;
    bool empty = false;
    // 借用 resource の有効範囲と scene/material の由来。consumer は使用前に照合する。
    ID3D12Fence* frameFence = nullptr;
    uint64_t deviceGeneration = 0;
    const void* sceneIdentity = nullptr;
    uint64_t sceneLayoutVersion = 0;
    RuntimeRayTracingMaterialVersion materialVersion{};
};

bool IsRuntimeRayTracingFrameCurrent(const RuntimeRayTracingFrame& frame, const GFX::Context& graphics,
                                     RuntimeRayTracingMaterialVersion materials) noexcept;

// Presentation thread owner。View culling と独立し、可視 scene 全体を記録する。
// 材質は既存 registry の slot を借用し、ロード/評価/照明をここへ重複実装しない。
// 返却値は同じ command stream/fence 内の consumer 用。不完全な scene は非公開。
class RuntimeRayTracingScene final {
public:
    RuntimeRayTracingScene();
    ~RuntimeRayTracingScene();
    RuntimeRayTracingScene(const RuntimeRayTracingScene&) = delete;
    RuntimeRayTracingScene& operator=(const RuntimeRayTracingScene&) = delete;
    RuntimeRayTracingFrame Prepare(const GFX::Context& graphics, const GPUDRIVEN::GpuDrivenSceneSource& source,
                                   std::span<const uint32_t> materialSlots,
                                   std::span<const MATERIAL::GpuMaterialData> materials,
                                   const RuntimeRayTracingBudget& budget,
                                   RuntimeRayTracingMaterialVersion materialVersion = {});
    void Reset(const GFX::Context& graphics);
    const RuntimeRayTracingStats& GetStats() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace HIKARI::RENDER3D::RAYTRACING
