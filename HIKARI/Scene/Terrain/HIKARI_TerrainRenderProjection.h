#pragma once

#include <map>
#include <memory>
#include <vector>
#include "Scene/HIKARI_SceneRenderCacheSync.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceQueryService.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceBake.h"

namespace HIKARI {
class TerrainSurfaceComponent;
class ModelAsset;
namespace RENDER3D::RUNTIME {
struct SceneRenderObjectDesc;
}
namespace TERRAIN {
struct TerrainSurface;
}
} // namespace HIKARI
namespace HIKARI::TERRAIN::RENDER {
struct TerrainProjectionLayout {
    int32_t minKeyX = 0;
    int32_t minKeyZ = 0;
    int32_t maxKeyX = 0;
    int32_t maxKeyZ = 0;
    float chunkSize = 0.0f;
    uint32_t authoredCells = 0u;
    uint32_t gridResolution = 0u;
    uint32_t layerCount = 1u;

    bool operator==(const TerrainProjectionLayout&) const = default;
};

struct TerrainProjectionState {
    struct AuthoredChunkState {
        uint64_t revision = 0u;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
    };

    RuntimeObjectHandle sourceObject{};
    TerrainProjectionLayout layout{};
    bool layoutValid = false;
    uint64_t authoredRevision = 0u;
    std::map<TERRAIN::TerrainChunkKey, AuthoredChunkState> authoredChunks{};
    std::map<TERRAIN::TerrainChunkKey, uint64_t> runtimeRevisions{};
    std::map<TERRAIN::TerrainChunkKey, TERRAIN::TerrainChunkRuntimeSnapshotPtr> runtimeByChunk{};
    std::vector<TERRAIN::TerrainChunkKey> changedChunks{};
    // runtime 応答込みの地形高さ。頂点焼き込みの入力で GPU へは渡さない。
    std::vector<float> sourceHeights{};
    // render bridge と共有する焼き込み済み field。revision ごとに値だけ更新する。
    std::shared_ptr<RENDER3D::TERRAIN::TerrainSurfaceProjectedFields> fields{};
    TerrainSurfaceBakeInputs bakeInputs{};
    bool bakeInputsValid = false;
    uint64_t projectionRevision = 0u;
    float authoredMinHeight = 0.0f;
    float authoredMaxHeight = 0.0f;
    float stableBoundsMinY = 0.0f;
    float stableBoundsMaxY = 0.0f;
    bool stableBoundsValid = false;
    ProceduralMeshSettings warmSettings{};
    std::filesystem::path warmProjectRoot{};
    bool warmSettingsValid = false;
    bool warmPending = false;
};

struct TerrainProjectionOutput {
    const ModelAsset* warmedModel = nullptr;
    std::string warmedClusteredGeometryPath{};
    ProceduralMeshSettings meshSettings{};
    float stableBoundsMinY = 0.0f;
    float stableBoundsMaxY = 0.0f;
};

void BakeTerrainChunksForRender(const TERRAIN::TerrainSurface& surface, const TerrainSurfaceComponent& terrain,
                                RuntimeObjectHandle objectHandle,
                                const TERRAIN::TerrainSurfaceQueryService* terrainSurfaceQuery,
                                const std::filesystem::path& projectRoot, float chunkSize, uint32_t cells,
                                TerrainProjectionState& state, SceneRenderCacheSync::DebugStats& stats,
                                TerrainProjectionOutput& output, RENDER3D::RUNTIME::SceneRenderObjectDesc& desc);

} // namespace HIKARI::TERRAIN::RENDER
