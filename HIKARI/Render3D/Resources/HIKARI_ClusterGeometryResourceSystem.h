#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Cluster/HIKARI_ClusterGeometryPacked.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuData.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D {

using ClusterGeometryGpuLayout = CLUSTER::ClusterGeometryPackedLayout;

struct ClusterGeometryResourceRecord {
    ClusterGeometryResourceHandle handle{};
    std::string sourceKey{};
    std::filesystem::path sourcePath{};
    std::filesystem::file_time_type sourceWriteTime{};
    uint64_t sourceRevision = 0;
    uint64_t sourceFileSize = 0;
    uint64_t sourceValidationFenceValue = 0;
    ClusterGeometryGpuLayout layout{};
    std::vector<CLUSTER::ClusterGeometrySurfaceRange> surfaceRanges{};
    std::vector<CLUSTER::ClusterGeometrySurfaceLodRange> surfaceLodRanges{};
    std::vector<CLUSTER::ClusterGeometrySurfaceSection> surfaceSections{};
    RenderResourceView srv{};
    RenderResourceView metadataSrv{};
    Microsoft::WRL::ComPtr<ID3D12Resource> metadataBuffer{};
    uint64_t metadataBufferBytes = 0;
    bool ready = false;
    // ディスクファイルではなくメモリ登録済み packed データから作られた資源。
    bool memoryResident = false;
};

struct ClusterGeometryResourceSystemStats {
    bool initialized = false;
    uint32_t requestCount = 0;
    uint32_t hitCount = 0;
    uint32_t missCount = 0;
    uint32_t staleReloadCount = 0;
    uint32_t loadedCount = 0;
    uint32_t failedCount = 0;
    uint32_t missingDeviceCount = 0;
    uint32_t resourceCount = 0;
    uint32_t readyResourceCount = 0;
    uint32_t shaderVisibleResourceCount = 0;
    uint32_t missingDescriptorCount = 0;
    uint32_t descriptorAllocationFailedCount = 0;
    uint32_t fileReadCount = 0;
    uint32_t gpuResourceCreateCount = 0;
    uint32_t uploadSubmitCount = 0;
    uint32_t fenceWaitCount = 0;
    uint32_t descriptorAllocationCount = 0;
    uint32_t descriptorArenaBegin = 0;
    uint32_t descriptorArenaCapacity = 0;
    uint32_t descriptorArenaUsed = 0;
    uint32_t descriptorArenaFree = 0;
    uint32_t ownerThreadViolationCount = 0;
    uint32_t upgradedVirtualHandleCount = 0;
    uint32_t surfaceCount = 0;
    uint32_t surfaceLodRangeCount = 0;
    uint32_t surfaceSectionCount = 0;
    uint32_t clusterCount = 0;
    uint32_t pageCount = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t meshletPrimitiveCount = 0;
    uint32_t surfaceRangeCount = 0;
    uint64_t gpuBufferBytes = 0;
};

void UpdateClusterGeometryResourceContext(const GFX::Context& ctx);
void ShutdownClusterGeometryResourceSystem();

// This resource system is presentation-owner-thread only. Registration,
// loading, lookup, range queries, stats, and shutdown must not race.
// プロシージャル等の実行時生成ジオメトリを、ファイルを介さず
// packed データのまま登録する。path は LoadClusterGeometryResource の
// hcmeshPath と一致させる。
void RegisterClusterGeometryPackedSource(const std::string& path, CLUSTER::ClusterGeometryPackedBytes packed);
// Removal makes the record unavailable immediately. Its GPU allocation
// stays retained until replacement or shutdown so in-flight frames remain
// valid under deferred-release rules.
void UnregisterClusterGeometryPackedSource(const std::string& path);

// HCMESH resource identity is owned here. Callers must not derive a
// per-object or per-surface alias for the same packed artifact.
std::string BuildClusterGeometryResourceSourceKey(const std::filesystem::path& hcmeshPath);

ClusterGeometryResourceHandle LoadClusterGeometryResource(const std::filesystem::path& hcmeshPath);

const ClusterGeometryResourceRecord* GetClusterGeometryResource(ClusterGeometryResourceHandle handle);

const CLUSTER::ClusterGeometrySurfaceRange* FindClusterGeometrySurfaceRange(ClusterGeometryResourceHandle handle,
                                                                            uint32_t nodeIndex, uint32_t meshIndex,
                                                                            uint32_t primitiveIndex);

const CLUSTER::ClusterGeometrySurfaceLodRange* FindClusterGeometrySurfaceLodRange(ClusterGeometryResourceHandle handle,
                                                                                  uint32_t firstLodRange,
                                                                                  uint32_t lodRangeCount,
                                                                                  uint32_t lodIndex);

ClusterGeometryResourceSystemStats GetClusterGeometryResourceSystemStats();

} // namespace HIKARI::RENDER3D
