#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Render3D/Runtime/HIKARI_MassRenderProxy.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI {
class ModelAsset;
}

namespace HIKARI::RENDER3D::RUNTIME {
class RenderModelCache;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {

inline constexpr size_t kMassGpuSceneDirtyHistoryCapacity = 8u;
inline constexpr size_t kMassGpuSceneMaximumSourceInstanceCount = 131072u;

struct MassRenderProxyTemplateRequest {
    uint64_t typeId = 0u;
    const ModelAsset* model = nullptr;
    uint32_t meshIndex = 0u;
    uint32_t primitiveIndex = 0u;
    uint32_t materialIndex = 0u;
    bool receiveShadow = true;
    bool rayTraced = true;
    std::string clusteredGeometryPath{};
};

struct MassGpuSceneStoreInput {
    MassRenderProxyTemplateRequest proxy{};
    std::span<const RUNTIME::MassRenderTransform> transforms{};
    std::span<const RUNTIME::MassRenderDirtyRange> dirtyRanges{};
    uint64_t layoutVersion = 0u;
    uint64_t sourceVersion = 0u;
    uint64_t dirtyBaseSourceVersion = 0u;
};

struct MassGpuSceneVersionDelta {
    uint64_t baseSourceVersion = 0u;
    uint64_t sourceVersion = 0u;
    std::vector<RUNTIME::MassRenderDirtyRange> dirtyRanges{};
};

// 種類ごとの形（materialSources と同じ並び）。光追は種類ごとに形の BLAS と hit 記録を一つ持ち、個体はそれを共有する。
struct MassGpuSceneShape {
    ClusterGeometryResourceHandle clusterGeometry{};
    uint32_t nodeIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    uint32_t meshIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    uint32_t primitiveIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    bool doubleSided = false;
    bool rayTraced = true; // false の種類は光線追跡の場景へ入れない
};

struct MassGpuSceneSource {
    std::vector<RUNTIME::MassRenderProxyGpuRecord> instances{};
    std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> materialSources{};
    std::vector<MassGpuSceneShape> shapes{};
    std::vector<MassGpuSceneVersionDelta> dirtyHistory{};
    uint64_t worldIdentity = 0u;
    uint64_t layoutVersion = 0u;
    uint64_t sourceVersion = 0u;
    uint64_t materialVersion = 0u;
    uint64_t publicationFrameIndex = 0u;
    uint64_t publicationGeneration = 0u;

    void Reset();
    bool HasInstances() const noexcept { return !instances.empty(); }
    bool CollectDirtyRangesSince(uint64_t baseSourceVersion,
                                 std::vector<RUNTIME::MassRenderDirtyRange>& outRanges) const;
};

struct MassGpuSceneRegistryStats {
    uint32_t storeCount = 0u;
    uint32_t templateCount = 0u;
    uint32_t missingTemplateCount = 0u;
    uint32_t unsupportedTemplateCount = 0u;
    uint32_t sourceInstanceCount = 0u;
    uint32_t dirtyRangeCount = 0u;
    uint32_t fullRebuildCount = 0u;
    uint32_t incrementalPatchCount = 0u;
    uint32_t stableReuseCount = 0u;
};

class MassGpuSceneRegistry final {
public:
    MassGpuSceneRegistry();
    ~MassGpuSceneRegistry();
    MassGpuSceneRegistry(const MassGpuSceneRegistry&) = delete;
    MassGpuSceneRegistry& operator=(const MassGpuSceneRegistry&) = delete;

    void Clear();
    void BeginSync(uint64_t worldIdentity, uint64_t publicationFrameIndex, uint64_t publicationGeneration);
    bool AddStore(const MassGpuSceneStoreInput& input);
    bool EndSync(RUNTIME::RenderModelCache& renderModelCache);

    const MassGpuSceneSource& GetSource() const noexcept;
    const MassGpuSceneRegistryStats& GetStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace HIKARI::RENDER3D::GPUDRIVEN
