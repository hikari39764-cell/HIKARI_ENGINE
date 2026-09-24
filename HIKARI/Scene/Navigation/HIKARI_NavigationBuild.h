#pragma once

#include <array>
#include <compare>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
class World;
class TerrainSurfaceComponent;
} // namespace HIKARI
namespace HIKARI::NAVIGATION::BUILD {
template <auto Free>
struct Deleter {
    template <class T>
    void operator()(T* value) const noexcept {
        Free(value);
    }
};
using NavMeshPtr = std::unique_ptr<dtNavMesh, Deleter<dtFreeNavMesh>>;
using NavMeshQueryPtr = std::unique_ptr<dtNavMeshQuery, Deleter<dtFreeNavMeshQuery>>;
using TileDataPtr = std::unique_ptr<unsigned char, Deleter<dtFree>>;
inline constexpr uint16_t kWalkableFlag = 1u;

struct NavigationGeometry {
    std::vector<float> vertices{};
    std::vector<int> triangles{};
    std::array<float, 3u> boundsMin{(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(),
                                    (std::numeric_limits<float>::max)()};
    std::array<float, 3u> boundsMax{(std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)(),
                                    (std::numeric_limits<float>::lowest)()};
};

struct NavigationChunkKey {
    uint64_t object = 0u;
    int32_t x = 0;
    int32_t z = 0;

    auto operator<=>(const NavigationChunkKey&) const = default;
};

struct NavigationChunkSource {
    RuntimeObjectHandle object{};
    TerrainSurfaceComponent* terrain = nullptr;
    TERRAIN::TerrainChunkKey key{};
    uint64_t revision = 0u;
};

struct CurrentNavigationSources {
    std::map<NavigationChunkKey, NavigationChunkSource> chunks;
    uint64_t revision = 0u;
};

struct NavigationSourceStamp {
    std::map<uint64_t, uint64_t> objectSignatures{};
    std::map<uint64_t, uint64_t> collisionRevisions{};
    uint64_t revision = 0u;
    bool hasTerrain = false;
};

struct CachedNavigationChunk {
    uint64_t revision = 0u;
    NavigationGeometry geometry{};
};

using NavigationChunkCache = std::map<NavigationChunkKey, CachedNavigationChunk>;

struct NavigationGrid {
    std::array<float, 3u> origin{};
    int tilesX = 0;
    int tilesZ = 0;
    float tileWorldSize = 0.0f;
};

struct NavigationBuildOutput {
    NavMeshPtr navMesh{};
    NavMeshQueryPtr query{};
    NavigationChunkCache chunks{};
    NavigationGrid grid{};
    uint32_t tileCount = 0u;
};

using DirtyTileSet = std::set<std::pair<int, int>>;

struct PendingTileUpdate {
    int x = 0, z = 0;
    TileDataPtr newData;
    int newDataSize = 0;
    dtTileRef oldReference = 0;
    TileDataPtr oldBackup;
    int oldDataSize = 0;
    bool applied = false, newInstalled = false;
};
struct PendingTileUpdates {
    std::vector<PendingTileUpdate> values;
};

float NavigationBorderWorldSize() noexcept;

NavigationSourceStamp GatherNavigationSourceStamp(World& world);

CurrentNavigationSources GatherCurrentSources(World& world, const NavigationSourceStamp& stamp);

bool AppendNavigationGeometry(const NavigationGeometry& source, NavigationGeometry& out, std::string& outDiagnostic);

bool BuildChunkGeometry(const NavigationChunkSource& source, NavigationGeometry& out, std::string& outDiagnostic);

bool GatherGeometry(const CurrentNavigationSources& sources, NavigationGeometry& out, NavigationChunkCache& outChunks,
                    std::string& outDiagnostic);

bool BuildNavigation(NavigationGeometry& geometry, NavigationBuildOutput& out, std::string& outDiagnostic);

bool GeometryFitsGrid(const NavigationGeometry& geometry, const NavigationGrid& grid) noexcept;

void MarkGeometryTiles(const NavigationGeometry& geometry, const NavigationGrid& grid, float borderWorldSize,
                       DirtyTileSet& outTiles);

bool PrepareIncrementalNavigation(const NavigationGrid& grid, const NavigationChunkCache& currentChunks,
                                  const NavigationChunkCache& updatedChunks,
                                  const std::set<NavigationChunkKey>& changedChunks, const DirtyTileSet& dirtyTiles,
                                  PendingTileUpdates& pending, std::string& outDiagnostic);

bool CommitIncrementalNavigation(dtNavMesh& navMesh, PendingTileUpdates& pending, uint32_t currentTileCount,
                                 uint32_t& outTileCount, std::string& outDiagnostic);

} // namespace HIKARI::NAVIGATION::BUILD
