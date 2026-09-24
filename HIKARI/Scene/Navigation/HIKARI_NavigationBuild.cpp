#include "HIKARI_NavigationBuild.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <DetourCommon.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

namespace HIKARI::NAVIGATION::BUILD {
constexpr float kCellSize = 0.25f;
constexpr float kCellHeight = 0.10f;
constexpr int kTileSizeCells = 64;
constexpr float kAgentHeight = 1.80f;
constexpr float kAgentRadius = 0.45f;
constexpr float kAgentMaxClimb = 0.45f;
constexpr float kAgentMaxSlopeDegrees = 50.0f;

using HeightfieldPtr = std::unique_ptr<rcHeightfield, Deleter<rcFreeHeightField>>;
using CompactHeightfieldPtr = std::unique_ptr<rcCompactHeightfield, Deleter<rcFreeCompactHeightfield>>;
using ContourSetPtr = std::unique_ptr<rcContourSet, Deleter<rcFreeContourSet>>;
using PolyMeshPtr = std::unique_ptr<rcPolyMesh, Deleter<rcFreePolyMesh>>;
using PolyMeshDetailPtr = std::unique_ptr<rcPolyMeshDetail, Deleter<rcFreePolyMeshDetail>>;

float NavigationBorderWorldSize() noexcept { return (std::ceil(kAgentRadius / kCellSize) + 3.0f) * kCellSize; }

int ClampTileIndex(int value, int count) noexcept { return (std::clamp)(value, 0, (std::max)(count - 1, 0)); }

bool BinTriangles(const NavigationGeometry& geometry, int tilesX, int tilesZ, float tileWorldSize,
                  float borderWorldSize, std::vector<std::vector<int>>& outBins, std::string& outDiagnostic) {
    if (tilesX <= 0 || tilesZ <= 0 ||
        static_cast<uint64_t>(tilesX) * tilesZ > (std::numeric_limits<std::size_t>::max)()) {
        outDiagnostic = "navigation tile grid is invalid";
        return false;
    }
    outBins.clear();
    outBins.resize(static_cast<std::size_t>(tilesX) * static_cast<std::size_t>(tilesZ));
    const int triangleCount = static_cast<int>(geometry.triangles.size() / 3u);
    for (int triangle = 0; triangle < triangleCount; ++triangle) {
        float minimumX = (std::numeric_limits<float>::max)();
        float minimumZ = (std::numeric_limits<float>::max)();
        float maximumX = (std::numeric_limits<float>::lowest)();
        float maximumZ = (std::numeric_limits<float>::lowest)();
        for (int corner = 0; corner < 3; ++corner) {
            const int vertex = geometry.triangles[static_cast<std::size_t>(triangle) * 3u + corner];
            const float* position = &geometry.vertices[static_cast<std::size_t>(vertex) * 3u];
            minimumX = (std::min)(minimumX, position[0]);
            minimumZ = (std::min)(minimumZ, position[2]);
            maximumX = (std::max)(maximumX, position[0]);
            maximumZ = (std::max)(maximumZ, position[2]);
        }
        const int firstX = ClampTileIndex(
            static_cast<int>(std::floor((minimumX - geometry.boundsMin[0] - borderWorldSize) / tileWorldSize)), tilesX);
        const int lastX = ClampTileIndex(
            static_cast<int>(std::floor((maximumX - geometry.boundsMin[0] + borderWorldSize) / tileWorldSize)), tilesX);
        const int firstZ = ClampTileIndex(
            static_cast<int>(std::floor((minimumZ - geometry.boundsMin[2] - borderWorldSize) / tileWorldSize)), tilesZ);
        const int lastZ = ClampTileIndex(
            static_cast<int>(std::floor((maximumZ - geometry.boundsMin[2] + borderWorldSize) / tileWorldSize)), tilesZ);
        for (int z = firstZ; z <= lastZ; ++z) {
            for (int x = firstX; x <= lastX; ++x) {
                std::vector<int>& bin = outBins[static_cast<std::size_t>(z) * tilesX + x];
                const std::size_t source = static_cast<std::size_t>(triangle) * 3u;
                bin.push_back(geometry.triangles[source]);
                bin.push_back(geometry.triangles[source + 1u]);
                bin.push_back(geometry.triangles[source + 2u]);
            }
        }
    }
    return true;
}

bool BuildTileData(rcContext& context, const NavigationGeometry& geometry, const std::vector<int>& triangles, int tileX,
                   int tileZ, TileDataPtr& outNavData, int& outNavDataSize, bool& outTileBuilt,
                   std::string& outDiagnostic) {
    outNavData.reset();
    outNavDataSize = 0;
    outTileBuilt = false;
    if (triangles.empty()) {
        return true;
    }

    rcConfig config{};
    config.cs = kCellSize;
    config.ch = kCellHeight;
    config.walkableSlopeAngle = kAgentMaxSlopeDegrees;
    config.walkableHeight = static_cast<int>(std::ceil(kAgentHeight / config.ch));
    config.walkableClimb = static_cast<int>(std::floor(kAgentMaxClimb / config.ch));
    config.walkableRadius = static_cast<int>(std::ceil(kAgentRadius / config.cs));
    config.maxEdgeLen = static_cast<int>(12.0f / config.cs);
    config.maxSimplificationError = 1.3f;
    config.minRegionArea = rcSqr(8);
    config.mergeRegionArea = rcSqr(20);
    config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    config.tileSize = kTileSizeCells;
    config.borderSize = config.walkableRadius + 3;
    config.width = config.tileSize + config.borderSize * 2;
    config.height = config.tileSize + config.borderSize * 2;
    config.detailSampleDist = config.cs * 6.0f;
    config.detailSampleMaxError = config.ch;

    const float tileWorldSize = static_cast<float>(config.tileSize) * config.cs;
    config.bmin[0] = geometry.boundsMin[0] + static_cast<float>(tileX) * tileWorldSize -
                     static_cast<float>(config.borderSize) * config.cs;
    config.bmin[1] = geometry.boundsMin[1] - kAgentMaxClimb;
    config.bmin[2] = geometry.boundsMin[2] + static_cast<float>(tileZ) * tileWorldSize -
                     static_cast<float>(config.borderSize) * config.cs;
    config.bmax[0] = config.bmin[0] + static_cast<float>(config.width) * config.cs;
    config.bmax[1] = geometry.boundsMax[1] + kAgentHeight;
    config.bmax[2] = config.bmin[2] + static_cast<float>(config.height) * config.cs;

    HeightfieldPtr solid(rcAllocHeightfield());
    if (!solid || !rcCreateHeightfield(&context, *solid, config.width, config.height, config.bmin, config.bmax,
                                       config.cs, config.ch)) {
        outDiagnostic = "Recast failed to allocate a tile heightfield";
        return false;
    }

    const int triangleCount = static_cast<int>(triangles.size() / 3u);
    std::vector<unsigned char> areas(static_cast<std::size_t>(triangleCount), RC_NULL_AREA);
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, geometry.vertices.data(),
                            static_cast<int>(geometry.vertices.size() / 3u), triangles.data(), triangleCount,
                            areas.data());
    if (!rcRasterizeTriangles(&context, geometry.vertices.data(), static_cast<int>(geometry.vertices.size() / 3u),
                              triangles.data(), areas.data(), triangleCount, *solid, config.walkableClimb)) {
        outDiagnostic = "Recast failed to rasterize a terrain tile";
        return false;
    }

    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *solid);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *solid);

    CompactHeightfieldPtr compact(rcAllocCompactHeightfield());
    if (!compact ||
        !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *solid, *compact)) {
        outDiagnostic = "Recast failed to build a compact terrain heightfield";
        return false;
    }
    solid.reset();
    if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact) || !rcBuildDistanceField(&context, *compact) ||
        !rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea)) {
        outDiagnostic = "Recast failed to partition a walkable terrain tile";
        return false;
    }

    ContourSetPtr contours(rcAllocContourSet());
    if (!contours ||
        !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours)) {
        outDiagnostic = "Recast failed to build terrain contours";
        return false;
    }
    if (contours->nconts == 0) {
        return true;
    }

    PolyMeshPtr polyMesh(rcAllocPolyMesh());
    if (!polyMesh || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polyMesh)) {
        outDiagnostic = "Recast failed to build a terrain polygon mesh";
        return false;
    }
    PolyMeshDetailPtr detailMesh(rcAllocPolyMeshDetail());
    if (!detailMesh || !rcBuildPolyMeshDetail(&context, *polyMesh, *compact, config.detailSampleDist,
                                              config.detailSampleMaxError, *detailMesh)) {
        outDiagnostic = "Recast failed to build terrain detail polygons";
        return false;
    }
    for (int polygon = 0; polygon < polyMesh->npolys; ++polygon) {
        if (polyMesh->areas[polygon] == RC_WALKABLE_AREA) {
            polyMesh->areas[polygon] = 0u;
            polyMesh->flags[polygon] = kWalkableFlag;
        }
    }

    dtNavMeshCreateParams params{};
    params.verts = polyMesh->verts;
    params.vertCount = polyMesh->nverts;
    params.polys = polyMesh->polys;
    params.polyAreas = polyMesh->areas;
    params.polyFlags = polyMesh->flags;
    params.polyCount = polyMesh->npolys;
    params.nvp = polyMesh->nvp;
    params.detailMeshes = detailMesh->meshes;
    params.detailVerts = detailMesh->verts;
    params.detailVertsCount = detailMesh->nverts;
    params.detailTris = detailMesh->tris;
    params.detailTriCount = detailMesh->ntris;
    params.walkableHeight = kAgentHeight;
    params.walkableRadius = kAgentRadius;
    params.walkableClimb = kAgentMaxClimb;
    params.tileX = tileX;
    params.tileY = tileZ;
    params.tileLayer = 0;
    rcVcopy(params.bmin, polyMesh->bmin);
    rcVcopy(params.bmax, polyMesh->bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;

    unsigned char* data = nullptr;
    const bool encoded = dtCreateNavMeshData(&params, &data, &outNavDataSize);
    outNavData.reset(data);
    if (!encoded || !outNavData || outNavDataSize <= 0) {
        outDiagnostic = "Detour failed to encode a terrain tile";
        return false;
    }
    outTileBuilt = true;
    return true;
}

bool BuildAndInstallTile(rcContext& context, const NavigationGeometry& geometry, const std::vector<int>& triangles,
                         int tileX, int tileZ, dtNavMesh& navMesh, bool& outTileBuilt, std::string& outDiagnostic) {
    TileDataPtr navData;
    int navDataSize = 0;
    if (!BuildTileData(context, geometry, triangles, tileX, tileZ, navData, navDataSize, outTileBuilt, outDiagnostic)) {
        return false;
    }
    if (!outTileBuilt) {
        return true;
    }
    const dtStatus status = navMesh.addTile(navData.get(), navDataSize, DT_TILE_FREE_DATA, 0, nullptr);
    if (dtStatusFailed(status)) {
        outDiagnostic = "Detour failed to install a terrain tile";
        return false;
    }
    navData.release();
    return true;
}

bool BuildNavigation(NavigationGeometry& geometry, NavigationBuildOutput& out, std::string& outDiagnostic) {
    if (geometry.triangles.empty()) {
        outDiagnostic = "scene has no solid walkable terrain geometry";
        return true;
    }
    if (geometry.vertices.size() / 3u > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        geometry.triangles.size() / 3u > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        outDiagnostic = "navigation source exceeds Recast int capacity";
        return false;
    }

    const float tileWorldSize = static_cast<float>(kTileSizeCells) * kCellSize;
    const float originX = std::floor(geometry.boundsMin[0] / tileWorldSize) * tileWorldSize;
    const float originZ = std::floor(geometry.boundsMin[2] / tileWorldSize) * tileWorldSize;
    constexpr double maxTileCount = 1u << 14u;
    const double width =
        std::max(1.0, std::ceil((static_cast<double>(geometry.boundsMax[0]) - originX) / tileWorldSize));
    const double depth =
        std::max(1.0, std::ceil((static_cast<double>(geometry.boundsMax[2]) - originZ) / tileWorldSize));
    if (!std::isfinite(originX) || !std::isfinite(originZ) || !std::isfinite(width) || !std::isfinite(depth) ||
        width * depth > maxTileCount) {
        outDiagnostic = "navigation tile grid exceeds Detour's 14-bit tile budget";
        return false;
    }
    const int tilesX = static_cast<int>(width);
    const int tilesZ = static_cast<int>(depth);
    const uint64_t requestedTileCount = static_cast<uint64_t>(tilesX) * tilesZ;
    const int tileBits =
        static_cast<int>((std::min)(dtIlog2(dtNextPow2(static_cast<unsigned int>(requestedTileCount))), 14u));
    const int polygonBits = 22 - tileBits;

    NavMeshPtr navMesh(dtAllocNavMesh());
    if (!navMesh) {
        outDiagnostic = "Detour navmesh allocation failed";
        return false;
    }
    dtNavMeshParams navParams{};
    geometry.boundsMin[0] = originX;
    geometry.boundsMin[2] = originZ;
    rcVcopy(navParams.orig, geometry.boundsMin.data());
    navParams.tileWidth = tileWorldSize;
    navParams.tileHeight = tileWorldSize;
    navParams.maxTiles = 1 << tileBits;
    navParams.maxPolys = 1 << polygonBits;
    if (dtStatusFailed(navMesh->init(&navParams))) {
        outDiagnostic = "Detour navmesh initialization failed";
        return false;
    }

    std::vector<std::vector<int>> triangleBins{};
    if (!BinTriangles(geometry, tilesX, tilesZ, tileWorldSize, NavigationBorderWorldSize(), triangleBins,
                      outDiagnostic)) {
        return false;
    }
    rcContext context(false);
    uint32_t builtTileCount = 0u;
    for (int z = 0; z < tilesZ; ++z) {
        for (int x = 0; x < tilesX; ++x) {
            bool tileBuilt = false;
            if (!BuildAndInstallTile(context, geometry, triangleBins[static_cast<std::size_t>(z) * tilesX + x], x, z,
                                     *navMesh, tileBuilt, outDiagnostic)) {
                return false;
            }
            if (tileBuilt) {
                ++builtTileCount;
            }
        }
    }
    if (builtTileCount == 0u) {
        outDiagnostic = "terrain produced no walkable navigation tiles";
        return true;
    }

    NavMeshQueryPtr query(dtAllocNavMeshQuery());
    if (!query || dtStatusFailed(query->init(navMesh.get(), 4096))) {
        outDiagnostic = "Detour query initialization failed";
        return false;
    }
    out.navMesh = std::move(navMesh);
    out.query = std::move(query);
    out.grid.origin = {originX, geometry.boundsMin[1], originZ};
    out.grid.tilesX = tilesX;
    out.grid.tilesZ = tilesZ;
    out.grid.tileWorldSize = tileWorldSize;
    out.tileCount = builtTileCount;
    outDiagnostic.clear();
    return true;
}

bool GeometryFitsGrid(const NavigationGeometry& geometry, const NavigationGrid& grid) noexcept {
    if (geometry.triangles.empty()) {
        return true;
    }
    constexpr float epsilon = 1.0e-3f;
    const float maximumX = grid.origin[0] + static_cast<float>(grid.tilesX) * grid.tileWorldSize;
    const float maximumZ = grid.origin[2] + static_cast<float>(grid.tilesZ) * grid.tileWorldSize;
    return geometry.boundsMin[0] >= grid.origin[0] - epsilon && geometry.boundsMin[2] >= grid.origin[2] - epsilon &&
           geometry.boundsMax[0] <= maximumX + epsilon && geometry.boundsMax[2] <= maximumZ + epsilon;
}

void MarkGeometryTiles(const NavigationGeometry& geometry, const NavigationGrid& grid, float borderWorldSize,
                       DirtyTileSet& outTiles) {
    if (geometry.triangles.empty() || grid.tilesX <= 0 || grid.tilesZ <= 0 || grid.tileWorldSize <= 0.0f) {
        return;
    }
    int firstX =
        static_cast<int>(std::floor((geometry.boundsMin[0] - grid.origin[0] - borderWorldSize) / grid.tileWorldSize));
    int lastX =
        static_cast<int>(std::floor((geometry.boundsMax[0] - grid.origin[0] + borderWorldSize) / grid.tileWorldSize));
    int firstZ =
        static_cast<int>(std::floor((geometry.boundsMin[2] - grid.origin[2] - borderWorldSize) / grid.tileWorldSize));
    int lastZ =
        static_cast<int>(std::floor((geometry.boundsMax[2] - grid.origin[2] + borderWorldSize) / grid.tileWorldSize));
    if (lastX < 0 || lastZ < 0 || firstX >= grid.tilesX || firstZ >= grid.tilesZ) {
        return;
    }
    firstX = ClampTileIndex(firstX, grid.tilesX);
    lastX = ClampTileIndex(lastX, grid.tilesX);
    firstZ = ClampTileIndex(firstZ, grid.tilesZ);
    lastZ = ClampTileIndex(lastZ, grid.tilesZ);
    for (int z = firstZ; z <= lastZ; ++z) {
        for (int x = firstX; x <= lastX; ++x) {
            outTiles.emplace(x, z);
        }
    }
}

bool GeometryIntersectsTile(const NavigationGeometry& geometry, const NavigationGrid& grid, int tileX, int tileZ,
                            float borderWorldSize) noexcept {
    if (geometry.triangles.empty()) {
        return false;
    }
    const float minimumX = grid.origin[0] + static_cast<float>(tileX) * grid.tileWorldSize - borderWorldSize;
    const float minimumZ = grid.origin[2] + static_cast<float>(tileZ) * grid.tileWorldSize - borderWorldSize;
    const float maximumX = minimumX + grid.tileWorldSize + borderWorldSize * 2.0f;
    const float maximumZ = minimumZ + grid.tileWorldSize + borderWorldSize * 2.0f;
    return geometry.boundsMax[0] >= minimumX && geometry.boundsMin[0] <= maximumX &&
           geometry.boundsMax[2] >= minimumZ && geometry.boundsMin[2] <= maximumZ;
}

bool GatherTileGeometry(const NavigationChunkCache& currentChunks, const NavigationChunkCache& updatedChunks,
                        const std::set<NavigationChunkKey>& changedChunks, const NavigationGrid& grid, int tileX,
                        int tileZ, float borderWorldSize, NavigationGeometry& out, std::string& outDiagnostic) {
    for (const auto& [key, chunk] : currentChunks) {
        if (changedChunks.contains(key) ||
            !GeometryIntersectsTile(chunk.geometry, grid, tileX, tileZ, borderWorldSize)) {
            continue;
        }
        if (!AppendNavigationGeometry(chunk.geometry, out, outDiagnostic)) {
            return false;
        }
    }
    for (const auto& [key, chunk] : updatedChunks) {
        (void)key;
        if (!GeometryIntersectsTile(chunk.geometry, grid, tileX, tileZ, borderWorldSize)) {
            continue;
        }
        if (!AppendNavigationGeometry(chunk.geometry, out, outDiagnostic)) {
            return false;
        }
    }
    if (!out.triangles.empty()) {
        out.boundsMin[0] = grid.origin[0];
        out.boundsMin[2] = grid.origin[2];
    }
    return true;
}

bool PrepareIncrementalNavigation(const NavigationGrid& grid, const NavigationChunkCache& currentChunks,
                                  const NavigationChunkCache& updatedChunks,
                                  const std::set<NavigationChunkKey>& changedChunks, const DirtyTileSet& dirtyTiles,
                                  PendingTileUpdates& pending, std::string& outDiagnostic) {
    auto& updates = pending.values;
    updates.reserve(dirtyTiles.size());
    const float borderWorldSize = NavigationBorderWorldSize();
    rcContext context(false);
    for (const auto& [tileX, tileZ] : dirtyTiles) {
        NavigationGeometry geometry{};
        if (!GatherTileGeometry(currentChunks, updatedChunks, changedChunks, grid, tileX, tileZ, borderWorldSize,
                                geometry, outDiagnostic)) {
            return false;
        }
        bool tileBuilt = false;
        PendingTileUpdate update{};
        update.x = tileX;
        update.z = tileZ;
        if (!BuildTileData(context, geometry, geometry.triangles, tileX, tileZ, update.newData, update.newDataSize,
                           tileBuilt, outDiagnostic)) {
            return false;
        }
        if (!tileBuilt) {
            update.newData.reset();
            update.newDataSize = 0;
        }
        updates.push_back(std::move(update));
    }

    return true;
}

bool CommitIncrementalNavigation(dtNavMesh& navMesh, PendingTileUpdates& pending, uint32_t currentTileCount,
                                 uint32_t& outTileCount, std::string& outDiagnostic) {
    auto& updates = pending.values;
    // Back up only the dirty tiles before mutating Detour. This keeps
    // the transaction page-bounded while retaining a rollback path.
    for (PendingTileUpdate& update : updates) {
        const dtMeshTile* oldTile = navMesh.getTileAt(update.x, update.z, 0);
        if (oldTile == nullptr) {
            continue;
        }
        update.oldReference = navMesh.getTileRef(oldTile);
        update.oldDataSize = oldTile->dataSize;
        if (oldTile->data == nullptr || update.oldDataSize <= 0) {
            outDiagnostic = "Detour dirty tile has no recoverable data";
            return false;
        }
        update.oldBackup.reset(static_cast<unsigned char*>(dtAlloc(update.oldDataSize, DT_ALLOC_PERM)));
        if (update.oldBackup == nullptr) {
            outDiagnostic = "Detour failed to back up a dirty tile";
            return false;
        }
        std::memcpy(update.oldBackup.get(), oldTile->data, static_cast<std::size_t>(update.oldDataSize));
    }

    const auto rollback = [&]() noexcept {
        bool restored = true;
        for (auto it = updates.rbegin(); it != updates.rend(); ++it) {
            PendingTileUpdate& update = *it;
            if (!update.applied) {
                continue;
            }
            if (update.newInstalled) {
                const dtTileRef installed = navMesh.getTileRefAt(update.x, update.z, 0);
                if (installed == 0 || dtStatusFailed(navMesh.removeTile(installed, nullptr, nullptr))) {
                    restored = false;
                }
                update.newInstalled = false;
            }
            if (update.oldBackup != nullptr) {
                const dtStatus status = navMesh.addTile(update.oldBackup.get(), update.oldDataSize, DT_TILE_FREE_DATA,
                                                        update.oldReference, nullptr);
                if (dtStatusFailed(status)) {
                    restored = false;
                } else {
                    update.oldBackup.release();
                }
            }
            update.applied = false;
        }
        return restored;
    };

    uint32_t tileCount = currentTileCount;
    for (PendingTileUpdate& update : updates) {
        if (update.oldReference != 0) {
            if (dtStatusFailed(navMesh.removeTile(update.oldReference, nullptr, nullptr))) {
                const bool restored = rollback();
                outDiagnostic = restored ? "Detour failed to remove a dirty tile" : "Detour dirty-tile rollback failed";
                return false;
            }
            if (tileCount > 0u) {
                --tileCount;
            }
        }
        update.applied = true;
        if (update.newData == nullptr) {
            continue;
        }
        const dtStatus status =
            navMesh.addTile(update.newData.get(), update.newDataSize, DT_TILE_FREE_DATA, update.oldReference, nullptr);
        if (dtStatusFailed(status)) {
            const bool restored = rollback();
            outDiagnostic = restored ? "Detour failed to install a dirty tile" : "Detour dirty-tile rollback failed";
            return false;
        }
        update.newData.release();
        update.newInstalled = true;
        ++tileCount;
    }

    outTileCount = tileCount;
    outDiagnostic.clear();
    return true;
}

} // namespace HIKARI::NAVIGATION::BUILD
