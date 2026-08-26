#include "Scene/Navigation/HIKARI_NavigationWorldService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include "Core/HIKARI_Logger.h"
#include "Physics/HIKARI_TerrainCollisionProjection.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

namespace HIKARI::NAVIGATION {
    namespace {

        constexpr float kCellSize = 0.25f;
        constexpr float kCellHeight = 0.10f;
        constexpr int kTileSizeCells = 64;
        constexpr float kAgentHeight = 1.80f;
        constexpr float kAgentRadius = 0.45f;
        constexpr float kAgentMaxClimb = 0.45f;
        constexpr float kAgentMaxSlopeDegrees = 50.0f;
        constexpr int kMaxQueryPolygons = 256;
        constexpr int kMaxStraightPathPoints = 256;
        constexpr uint16_t kWalkableFlag = 1u;
        constexpr uint64_t kHashOffset = 1469598103934665603ull;
        constexpr uint64_t kHashPrime = 1099511628211ull;

        uint64_t HashValue(uint64_t hash, uint64_t value) noexcept {
            for (uint32_t byte = 0u; byte < 8u; ++byte) {
                hash ^= (value >> (byte * 8u)) & 0xFFu;
                hash *= kHashPrime;
            }
            return hash;
        }

        uint64_t HashFloat(uint64_t hash, float value) noexcept {
            uint32_t bits = 0u;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            return HashValue(hash, bits);
        }

        MATH::Vec3 SanitizeExtents(const MATH::Vec3& extents) noexcept {
            const auto finitePositive = [](
                float value,
                float fallback) noexcept {
                return std::isfinite(value) && value > 1.0e-4f
                    ? value
                    : fallback;
            };
            return {
                finitePositive(extents.x, 1.0f),
                finitePositive(extents.y, 2.0f),
                finitePositive(extents.z, 1.0f)
            };
        }

        std::array<float, 3u> ToDetour(const MATH::Vec3& value) noexcept {
            return { value.x, value.y, value.z };
        }

        MATH::Vec3 FromDetour(const float* value) noexcept {
            return { value[0], value[1], value[2] };
        }

        struct HeightfieldDeleter {
            void operator()(rcHeightfield* value) const noexcept {
                rcFreeHeightField(value);
            }
        };

        struct CompactHeightfieldDeleter {
            void operator()(rcCompactHeightfield* value) const noexcept {
                rcFreeCompactHeightfield(value);
            }
        };

        struct ContourSetDeleter {
            void operator()(rcContourSet* value) const noexcept {
                rcFreeContourSet(value);
            }
        };

        struct PolyMeshDeleter {
            void operator()(rcPolyMesh* value) const noexcept {
                rcFreePolyMesh(value);
            }
        };

        struct PolyMeshDetailDeleter {
            void operator()(rcPolyMeshDetail* value) const noexcept {
                rcFreePolyMeshDetail(value);
            }
        };

        struct NavMeshDeleter {
            void operator()(dtNavMesh* value) const noexcept {
                dtFreeNavMesh(value);
            }
        };

        struct NavMeshQueryDeleter {
            void operator()(dtNavMeshQuery* value) const noexcept {
                dtFreeNavMeshQuery(value);
            }
        };

        using HeightfieldPtr =
            std::unique_ptr<rcHeightfield, HeightfieldDeleter>;
        using CompactHeightfieldPtr =
            std::unique_ptr<rcCompactHeightfield, CompactHeightfieldDeleter>;
        using ContourSetPtr =
            std::unique_ptr<rcContourSet, ContourSetDeleter>;
        using PolyMeshPtr =
            std::unique_ptr<rcPolyMesh, PolyMeshDeleter>;
        using PolyMeshDetailPtr =
            std::unique_ptr<rcPolyMeshDetail, PolyMeshDetailDeleter>;
        using NavMeshPtr =
            std::unique_ptr<dtNavMesh, NavMeshDeleter>;
        using NavMeshQueryPtr =
            std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter>;

        struct NavigationGeometry {
            std::vector<float> vertices{};
            std::vector<int> triangles{};
            std::array<float, 3u> boundsMin{
                (std::numeric_limits<float>::max)(),
                (std::numeric_limits<float>::max)(),
                (std::numeric_limits<float>::max)() };
            std::array<float, 3u> boundsMax{
                (std::numeric_limits<float>::lowest)(),
                (std::numeric_limits<float>::lowest)(),
                (std::numeric_limits<float>::lowest)() };
        };

        struct NavigationChunkKey {
            uint64_t object = 0u;
            int32_t x = 0;
            int32_t z = 0;

            bool operator<(const NavigationChunkKey& rhs) const noexcept {
                if (object != rhs.object) {
                    return object < rhs.object;
                }
                if (x != rhs.x) {
                    return x < rhs.x;
                }
                return z < rhs.z;
            }
        };

        struct NavigationChunkSource {
            RuntimeObjectHandle object{};
            TerrainSurfaceComponent* terrain = nullptr;
            TERRAIN::TerrainChunkKey key{};
            uint64_t revision = 0u;
        };

        struct CurrentNavigationSources {
            std::map<NavigationChunkKey, NavigationChunkSource> chunks{};
            std::map<uint64_t, uint64_t> objectSignatures{};
            std::map<uint64_t, uint64_t> collisionRevisions{};
            uint64_t revision = 0u;
            bool hasTerrain = false;
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

        using NavigationChunkCache =
            std::map<NavigationChunkKey, CachedNavigationChunk>;

        struct NavigationGrid {
            std::array<float, 3u> origin{};
            int tilesX = 0;
            int tilesZ = 0;
            float tileWorldSize = 0.0f;
            int maxTiles = 0;
            int maxPolys = 0;
        };

        struct NavigationBuildOutput {
            NavMeshPtr navMesh{};
            NavMeshQueryPtr query{};
            NavigationChunkCache chunks{};
            std::map<uint64_t, uint64_t> objectSignatures{};
            std::map<uint64_t, uint64_t> collisionRevisions{};
            NavigationGrid grid{};
            uint32_t tileCount = 0u;
        };

        uint64_t ComputeTerrainObjectSignature(
            const TerrainSurfaceComponent& terrain) noexcept {
            uint64_t hash = kHashOffset;
            constexpr std::array<MATH::Vec3, 4u> probes{
                MATH::Vec3{ 0.0f, 0.0f, 0.0f },
                MATH::Vec3{ 1.0f, 0.0f, 0.0f },
                MATH::Vec3{ 0.0f, 1.0f, 0.0f },
                MATH::Vec3{ 0.0f, 0.0f, 1.0f }
            };
            for (const MATH::Vec3& probe : probes) {
                const MATH::Vec3 world = terrain.LocalToWorld(probe);
                hash = HashFloat(hash, world.x);
                hash = HashFloat(hash, world.y);
                hash = HashFloat(hash, world.z);
            }
            hash = HashFloat(hash, terrain.GetChunkWorldSize());
            return HashValue(hash, terrain.GetChunkCells());
        }

        NavigationSourceStamp GatherNavigationSourceStamp(World& world) {
            NavigationSourceStamp result{};
            world.ForEachObjectWith<TerrainSurfaceComponent>(
                [&](GameObject& object, TerrainSurfaceComponent& terrain) {
                    result.hasTerrain = true;
                    const RuntimeObjectHandle handle =
                        object.GetRuntimeHandle();
                    const uint64_t objectValue = handle.ToValue();
                    result.objectSignatures[objectValue] =
                        ComputeTerrainObjectSignature(terrain);
                    result.collisionRevisions[objectValue] =
                        terrain.GetCollisionRevision();
                });

            uint64_t hash = kHashOffset;
            for (const auto& [object, signature] :
                    result.objectSignatures) {
                hash = HashValue(hash, object);
                hash = HashValue(hash, signature);
                const auto collision =
                    result.collisionRevisions.find(object);
                hash = HashValue(
                    hash,
                    collision != result.collisionRevisions.end()
                        ? collision->second
                        : 0u);
            }
            result.revision = result.hasTerrain ? hash : 0u;
            return result;
        }

        CurrentNavigationSources GatherCurrentSources(
            World& world,
            const NavigationSourceStamp& stamp) {
            CurrentNavigationSources result{};
            result.objectSignatures = stamp.objectSignatures;
            result.collisionRevisions = stamp.collisionRevisions;
            result.hasTerrain = stamp.hasTerrain;
            world.ForEachObjectWith<TerrainSurfaceComponent>(
                [&](GameObject& object, TerrainSurfaceComponent& terrain) {
                    const RuntimeObjectHandle handle =
                        object.GetRuntimeHandle();
                    const uint64_t objectValue = handle.ToValue();
                    for (const auto& [key, chunk] : terrain.GetChunks()) {
                        (void)chunk;
                        const NavigationChunkKey navigationKey{
                            objectValue, key.x, key.z };
                        result.chunks.emplace(
                            navigationKey,
                            NavigationChunkSource{
                                handle,
                                &terrain,
                                key,
                                terrain.GetChunkRevision(key) });
                    }
                });

            uint64_t hash = stamp.revision;
            for (const auto& [key, source] : result.chunks) {
                hash = HashValue(hash, key.object);
                hash = HashValue(
                    hash,
                    static_cast<uint64_t>(
                        static_cast<int64_t>(key.x)));
                hash = HashValue(
                    hash,
                    static_cast<uint64_t>(
                        static_cast<int64_t>(key.z)));
                hash = HashValue(hash, source.revision);
            }
            result.revision = result.hasTerrain ? hash : 0u;
            return result;
        }

        bool AppendTerrainGeometry(
            const TerrainSurfaceComponent& terrain,
            const PHYSICS::TerrainCollisionProjection& projection,
            NavigationGeometry& out,
            std::string& outDiagnostic) {
            if (projection.geometry == nullptr ||
                projection.geometry->indices.empty()) {
                return true;
            }
            const MATH::Vec3 worldOrigin = terrain.LocalToWorld({});
            const MATH::Vec3 worldX =
                terrain.LocalToWorld({ 1.0f, 0.0f, 0.0f }) - worldOrigin;
            const MATH::Vec3 worldY =
                terrain.LocalToWorld({ 0.0f, 1.0f, 0.0f }) - worldOrigin;
            const MATH::Vec3 worldZ =
                terrain.LocalToWorld({ 0.0f, 0.0f, 1.0f }) - worldOrigin;
            const float determinant = MATH::Dot(
                MATH::Cross(worldX, worldY), worldZ);
            if (!std::isfinite(determinant) ||
                std::fabs(determinant) <= 1.0e-8f) {
                outDiagnostic =
                    "navigation terrain transform is singular or non-finite";
                return false;
            }
            const bool reverseWinding = determinant < 0.0f;
            if (projection.geometry->vertices.size() >
                    static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
                out.vertices.size() / 3u >
                    static_cast<std::size_t>((std::numeric_limits<int>::max)()) -
                        projection.geometry->vertices.size()) {
                outDiagnostic =
                    "navigation terrain exceeds Recast int vertex capacity";
                return false;
            }

            const int baseVertex = static_cast<int>(out.vertices.size() / 3u);
            out.vertices.reserve(
                out.vertices.size() +
                projection.geometry->vertices.size() * 3u);
            for (const MATH::Vec3& local : projection.geometry->vertices) {
                const MATH::Vec3 world = terrain.LocalToWorld(local);
                if (!std::isfinite(world.x) ||
                    !std::isfinite(world.y) ||
                    !std::isfinite(world.z)) {
                    outDiagnostic =
                        "navigation terrain contains a non-finite world vertex";
                    return false;
                }
                out.vertices.push_back(world.x);
                out.vertices.push_back(world.y);
                out.vertices.push_back(world.z);
                out.boundsMin[0] = (std::min)(out.boundsMin[0], world.x);
                out.boundsMin[1] = (std::min)(out.boundsMin[1], world.y);
                out.boundsMin[2] = (std::min)(out.boundsMin[2], world.z);
                out.boundsMax[0] = (std::max)(out.boundsMax[0], world.x);
                out.boundsMax[1] = (std::max)(out.boundsMax[1], world.y);
                out.boundsMax[2] = (std::max)(out.boundsMax[2], world.z);
            }
            out.triangles.reserve(
                out.triangles.size() + projection.geometry->indices.size());
            for (std::size_t triangle = 0u;
                triangle < projection.geometry->indices.size();
                triangle += 3u) {
                const std::array<uint32_t, 3u> indices{
                    projection.geometry->indices[triangle],
                    projection.geometry->indices[triangle + 1u],
                    projection.geometry->indices[triangle + 2u]
                };
                for (const uint32_t index : indices) {
                    if (index < projection.geometry->vertices.size()) {
                        continue;
                    }
                    outDiagnostic =
                        "navigation terrain contains an out-of-range index";
                    return false;
                }
                out.triangles.push_back(
                    baseVertex + static_cast<int>(indices[0]));
                out.triangles.push_back(baseVertex + static_cast<int>(
                    indices[reverseWinding ? 2u : 1u]));
                out.triangles.push_back(baseVertex + static_cast<int>(
                    indices[reverseWinding ? 1u : 2u]));
            }
            return true;
        }

        bool AppendNavigationGeometry(
            const NavigationGeometry& source,
            NavigationGeometry& out,
            std::string& outDiagnostic) {
            if (source.triangles.empty()) {
                return true;
            }
            const std::size_t sourceVertexCount =
                source.vertices.size() / 3u;
            const std::size_t outputVertexCount =
                out.vertices.size() / 3u;
            if (source.vertices.size() % 3u != 0u ||
                source.triangles.size() % 3u != 0u ||
                sourceVertexCount >
                    static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
                outputVertexCount >
                    static_cast<std::size_t>((std::numeric_limits<int>::max)()) -
                        sourceVertexCount) {
                outDiagnostic =
                    "navigation chunk geometry exceeds Recast int capacity";
                return false;
            }
            const int baseVertex = static_cast<int>(outputVertexCount);
            out.vertices.insert(
                out.vertices.end(),
                source.vertices.begin(),
                source.vertices.end());
            out.triangles.reserve(
                out.triangles.size() + source.triangles.size());
            for (const int index : source.triangles) {
                if (index < 0 ||
                    static_cast<std::size_t>(index) >= sourceVertexCount) {
                    outDiagnostic =
                        "navigation chunk contains an out-of-range index";
                    return false;
                }
                out.triangles.push_back(baseVertex + index);
            }
            for (std::size_t axis = 0u; axis < 3u; ++axis) {
                out.boundsMin[axis] = (std::min)(
                    out.boundsMin[axis], source.boundsMin[axis]);
                out.boundsMax[axis] = (std::max)(
                    out.boundsMax[axis], source.boundsMax[axis]);
            }
            return true;
        }

        bool BuildChunkGeometry(
            const NavigationChunkSource& source,
            NavigationGeometry& out,
            std::string& outDiagnostic) {
            if (source.terrain == nullptr) {
                outDiagnostic = "navigation chunk lost its terrain owner";
                return false;
            }
            const PHYSICS::TerrainCollisionProjection projection =
                PHYSICS::BuildTerrainChunkCollisionProjection(
                    source.object,
                    *source.terrain,
                    source.key);
            if (!projection.buildSucceeded) {
                outDiagnostic = projection.diagnostic.empty()
                    ? "terrain chunk collision projection failed"
                    : projection.diagnostic;
                return false;
            }
            return AppendTerrainGeometry(
                *source.terrain,
                projection,
                out,
                outDiagnostic);
        }

        bool GatherGeometry(
            const CurrentNavigationSources& sources,
            NavigationGeometry& out,
            NavigationChunkCache& outChunks,
            std::string& outDiagnostic) {
            for (const auto& [key, source] : sources.chunks) {
                CachedNavigationChunk cached{};
                cached.revision = source.revision;
                if (!BuildChunkGeometry(
                        source,
                        cached.geometry,
                        outDiagnostic) ||
                    !AppendNavigationGeometry(
                        cached.geometry,
                        out,
                        outDiagnostic)) {
                    return false;
                }
                outChunks.emplace(key, std::move(cached));
            }
            if (out.triangles.size() % 3u != 0u) {
                outDiagnostic =
                    "navigation triangle stream is not divisible by three";
                return false;
            }
            return true;
        }

        int ClampTileIndex(int value, int count) noexcept {
            return (std::clamp)(value, 0, (std::max)(count - 1, 0));
        }

        bool BinTriangles(
            const NavigationGeometry& geometry,
            int tilesX,
            int tilesZ,
            float tileWorldSize,
            float borderWorldSize,
            std::vector<std::vector<int>>& outBins,
            std::string& outDiagnostic) {
            if (tilesX <= 0 || tilesZ <= 0 ||
                static_cast<uint64_t>(tilesX) * tilesZ >
                    (std::numeric_limits<std::size_t>::max)()) {
                outDiagnostic = "navigation tile grid is invalid";
                return false;
            }
            outBins.clear();
            outBins.resize(
                static_cast<std::size_t>(tilesX) *
                static_cast<std::size_t>(tilesZ));
            const int triangleCount =
                static_cast<int>(geometry.triangles.size() / 3u);
            for (int triangle = 0; triangle < triangleCount; ++triangle) {
                float minimumX = (std::numeric_limits<float>::max)();
                float minimumZ = (std::numeric_limits<float>::max)();
                float maximumX = (std::numeric_limits<float>::lowest)();
                float maximumZ = (std::numeric_limits<float>::lowest)();
                for (int corner = 0; corner < 3; ++corner) {
                    const int vertex = geometry.triangles[
                        static_cast<std::size_t>(triangle) * 3u + corner];
                    const float* position =
                        &geometry.vertices[static_cast<std::size_t>(vertex) * 3u];
                    minimumX = (std::min)(minimumX, position[0]);
                    minimumZ = (std::min)(minimumZ, position[2]);
                    maximumX = (std::max)(maximumX, position[0]);
                    maximumZ = (std::max)(maximumZ, position[2]);
                }
                const int firstX = ClampTileIndex(
                    static_cast<int>(std::floor(
                        (minimumX - geometry.boundsMin[0] - borderWorldSize) /
                        tileWorldSize)),
                    tilesX);
                const int lastX = ClampTileIndex(
                    static_cast<int>(std::floor(
                        (maximumX - geometry.boundsMin[0] + borderWorldSize) /
                        tileWorldSize)),
                    tilesX);
                const int firstZ = ClampTileIndex(
                    static_cast<int>(std::floor(
                        (minimumZ - geometry.boundsMin[2] - borderWorldSize) /
                        tileWorldSize)),
                    tilesZ);
                const int lastZ = ClampTileIndex(
                    static_cast<int>(std::floor(
                        (maximumZ - geometry.boundsMin[2] + borderWorldSize) /
                        tileWorldSize)),
                    tilesZ);
                for (int z = firstZ; z <= lastZ; ++z) {
                    for (int x = firstX; x <= lastX; ++x) {
                        std::vector<int>& bin = outBins[
                            static_cast<std::size_t>(z) * tilesX + x];
                        const std::size_t source =
                            static_cast<std::size_t>(triangle) * 3u;
                        bin.push_back(geometry.triangles[source]);
                        bin.push_back(geometry.triangles[source + 1u]);
                        bin.push_back(geometry.triangles[source + 2u]);
                    }
                }
            }
            return true;
        }

        bool BuildTileData(
            rcContext& context,
            const NavigationGeometry& geometry,
            const std::vector<int>& triangles,
            int tileX,
            int tileZ,
            unsigned char*& outNavData,
            int& outNavDataSize,
            bool& outTileBuilt,
            std::string& outDiagnostic) {
            outNavData = nullptr;
            outNavDataSize = 0;
            outTileBuilt = false;
            if (triangles.empty()) {
                return true;
            }

            rcConfig config{};
            config.cs = kCellSize;
            config.ch = kCellHeight;
            config.walkableSlopeAngle = kAgentMaxSlopeDegrees;
            config.walkableHeight =
                static_cast<int>(std::ceil(kAgentHeight / config.ch));
            config.walkableClimb =
                static_cast<int>(std::floor(kAgentMaxClimb / config.ch));
            config.walkableRadius =
                static_cast<int>(std::ceil(kAgentRadius / config.cs));
            config.maxEdgeLen =
                static_cast<int>(12.0f / config.cs);
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

            const float tileWorldSize =
                static_cast<float>(config.tileSize) * config.cs;
            config.bmin[0] = geometry.boundsMin[0] +
                static_cast<float>(tileX) * tileWorldSize -
                static_cast<float>(config.borderSize) * config.cs;
            config.bmin[1] = geometry.boundsMin[1] - kAgentMaxClimb;
            config.bmin[2] = geometry.boundsMin[2] +
                static_cast<float>(tileZ) * tileWorldSize -
                static_cast<float>(config.borderSize) * config.cs;
            config.bmax[0] = config.bmin[0] +
                static_cast<float>(config.width) * config.cs;
            config.bmax[1] = geometry.boundsMax[1] + kAgentHeight;
            config.bmax[2] = config.bmin[2] +
                static_cast<float>(config.height) * config.cs;

            HeightfieldPtr solid(rcAllocHeightfield());
            if (!solid || !rcCreateHeightfield(
                    &context,
                    *solid,
                    config.width,
                    config.height,
                    config.bmin,
                    config.bmax,
                    config.cs,
                    config.ch)) {
                outDiagnostic = "Recast failed to allocate a tile heightfield";
                return false;
            }

            const int triangleCount =
                static_cast<int>(triangles.size() / 3u);
            std::vector<unsigned char> areas(
                static_cast<std::size_t>(triangleCount), RC_NULL_AREA);
            rcMarkWalkableTriangles(
                &context,
                config.walkableSlopeAngle,
                geometry.vertices.data(),
                static_cast<int>(geometry.vertices.size() / 3u),
                triangles.data(),
                triangleCount,
                areas.data());
            if (!rcRasterizeTriangles(
                    &context,
                    geometry.vertices.data(),
                    static_cast<int>(geometry.vertices.size() / 3u),
                    triangles.data(),
                    areas.data(),
                    triangleCount,
                    *solid,
                    config.walkableClimb)) {
                outDiagnostic = "Recast failed to rasterize a terrain tile";
                return false;
            }

            rcFilterLowHangingWalkableObstacles(
                &context, config.walkableClimb, *solid);
            rcFilterLedgeSpans(
                &context,
                config.walkableHeight,
                config.walkableClimb,
                *solid);
            rcFilterWalkableLowHeightSpans(
                &context, config.walkableHeight, *solid);

            CompactHeightfieldPtr compact(rcAllocCompactHeightfield());
            if (!compact || !rcBuildCompactHeightfield(
                    &context,
                    config.walkableHeight,
                    config.walkableClimb,
                    *solid,
                    *compact)) {
                outDiagnostic =
                    "Recast failed to build a compact terrain heightfield";
                return false;
            }
            solid.reset();
            if (!rcErodeWalkableArea(
                    &context, config.walkableRadius, *compact) ||
                !rcBuildDistanceField(&context, *compact) ||
                !rcBuildRegions(
                    &context,
                    *compact,
                    config.borderSize,
                    config.minRegionArea,
                    config.mergeRegionArea)) {
                outDiagnostic =
                    "Recast failed to partition a walkable terrain tile";
                return false;
            }

            ContourSetPtr contours(rcAllocContourSet());
            if (!contours || !rcBuildContours(
                    &context,
                    *compact,
                    config.maxSimplificationError,
                    config.maxEdgeLen,
                    *contours)) {
                outDiagnostic = "Recast failed to build terrain contours";
                return false;
            }
            if (contours->nconts == 0) {
                return true;
            }

            PolyMeshPtr polyMesh(rcAllocPolyMesh());
            if (!polyMesh || !rcBuildPolyMesh(
                    &context,
                    *contours,
                    config.maxVertsPerPoly,
                    *polyMesh)) {
                outDiagnostic = "Recast failed to build a terrain polygon mesh";
                return false;
            }
            PolyMeshDetailPtr detailMesh(rcAllocPolyMeshDetail());
            if (!detailMesh || !rcBuildPolyMeshDetail(
                    &context,
                    *polyMesh,
                    *compact,
                    config.detailSampleDist,
                    config.detailSampleMaxError,
                    *detailMesh)) {
                outDiagnostic =
                    "Recast failed to build terrain detail polygons";
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

            if (!dtCreateNavMeshData(
                    &params, &outNavData, &outNavDataSize) ||
                outNavData == nullptr || outNavDataSize <= 0) {
                outDiagnostic = "Detour failed to encode a terrain tile";
                return false;
            }
            outTileBuilt = true;
            return true;
        }

        bool BuildAndInstallTile(
            rcContext& context,
            const NavigationGeometry& geometry,
            const std::vector<int>& triangles,
            int tileX,
            int tileZ,
            dtNavMesh& navMesh,
            bool& outTileBuilt,
            std::string& outDiagnostic) {
            unsigned char* navData = nullptr;
            int navDataSize = 0;
            if (!BuildTileData(
                    context,
                    geometry,
                    triangles,
                    tileX,
                    tileZ,
                    navData,
                    navDataSize,
                    outTileBuilt,
                    outDiagnostic)) {
                return false;
            }
            if (!outTileBuilt) {
                return true;
            }
            const dtStatus status = navMesh.addTile(
                navData,
                navDataSize,
                DT_TILE_FREE_DATA,
                0,
                nullptr);
            if (dtStatusFailed(status)) {
                dtFree(navData);
                outDiagnostic = "Detour failed to install a terrain tile";
                return false;
            }
            return true;
        }

        bool BuildNavigation(
            const CurrentNavigationSources& sources,
            NavigationBuildOutput& out,
            std::string& outDiagnostic) {
            NavigationGeometry geometry{};
            if (!GatherGeometry(
                    sources,
                    geometry,
                    out.chunks,
                    outDiagnostic)) {
                return false;
            }
            out.objectSignatures = sources.objectSignatures;
            out.collisionRevisions = sources.collisionRevisions;
            if (geometry.triangles.empty()) {
                outDiagnostic = "scene has no solid walkable terrain geometry";
                return true;
            }
            if (geometry.vertices.size() / 3u >
                    static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
                geometry.triangles.size() / 3u >
                    static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
                outDiagnostic = "navigation source exceeds Recast int capacity";
                return false;
            }

            const float tileWorldSize =
                static_cast<float>(kTileSizeCells) * kCellSize;
            const float originX = std::floor(
                geometry.boundsMin[0] / tileWorldSize) * tileWorldSize;
            const float originZ = std::floor(
                geometry.boundsMin[2] / tileWorldSize) * tileWorldSize;
            const int tilesX = (std::max)(1, static_cast<int>(std::ceil(
                (geometry.boundsMax[0] - originX) /
                tileWorldSize)));
            const int tilesZ = (std::max)(1, static_cast<int>(std::ceil(
                (geometry.boundsMax[2] - originZ) /
                tileWorldSize)));
            const uint64_t requestedTileCount =
                static_cast<uint64_t>(tilesX) * tilesZ;
            if (requestedTileCount > (1u << 14u)) {
                outDiagnostic =
                    "navigation tile grid exceeds Detour's 14-bit tile budget";
                return false;
            }
            const int tileBits = static_cast<int>((std::min)(
                dtIlog2(dtNextPow2(static_cast<unsigned int>(
                    requestedTileCount))),
                14u));
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

            const int borderCells =
                static_cast<int>(std::ceil(kAgentRadius / kCellSize)) + 3;
            std::vector<std::vector<int>> triangleBins{};
            if (!BinTriangles(
                    geometry,
                    tilesX,
                    tilesZ,
                    tileWorldSize,
                    static_cast<float>(borderCells) * kCellSize,
                    triangleBins,
                    outDiagnostic)) {
                return false;
            }
            rcContext context(false);
            uint32_t builtTileCount = 0u;
            for (int z = 0; z < tilesZ; ++z) {
                for (int x = 0; x < tilesX; ++x) {
                    bool tileBuilt = false;
                    if (!BuildAndInstallTile(
                            context,
                            geometry,
                            triangleBins[
                                static_cast<std::size_t>(z) * tilesX + x],
                            x,
                            z,
                            *navMesh,
                            tileBuilt,
                            outDiagnostic)) {
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
            if (!query || dtStatusFailed(query->init(
                    navMesh.get(), 4096))) {
                outDiagnostic = "Detour query initialization failed";
                return false;
            }
            out.navMesh = std::move(navMesh);
            out.query = std::move(query);
            out.grid.origin = {
                originX,
                geometry.boundsMin[1],
                originZ };
            out.grid.tilesX = tilesX;
            out.grid.tilesZ = tilesZ;
            out.grid.tileWorldSize = tileWorldSize;
            out.grid.maxTiles = navParams.maxTiles;
            out.grid.maxPolys = navParams.maxPolys;
            out.tileCount = builtTileCount;
            outDiagnostic.clear();
            return true;
        }

        using DirtyTileSet = std::set<std::pair<int, int>>;

        bool GeometryFitsGrid(
            const NavigationGeometry& geometry,
            const NavigationGrid& grid) noexcept {
            if (geometry.triangles.empty()) {
                return true;
            }
            constexpr float epsilon = 1.0e-3f;
            const float maximumX = grid.origin[0] +
                static_cast<float>(grid.tilesX) * grid.tileWorldSize;
            const float maximumZ = grid.origin[2] +
                static_cast<float>(grid.tilesZ) * grid.tileWorldSize;
            return geometry.boundsMin[0] >= grid.origin[0] - epsilon &&
                geometry.boundsMin[2] >= grid.origin[2] - epsilon &&
                geometry.boundsMax[0] <= maximumX + epsilon &&
                geometry.boundsMax[2] <= maximumZ + epsilon;
        }

        void MarkGeometryTiles(
            const NavigationGeometry& geometry,
            const NavigationGrid& grid,
            float borderWorldSize,
            DirtyTileSet& outTiles) {
            if (geometry.triangles.empty() ||
                grid.tilesX <= 0 || grid.tilesZ <= 0 ||
                grid.tileWorldSize <= 0.0f) {
                return;
            }
            int firstX = static_cast<int>(std::floor(
                (geometry.boundsMin[0] - grid.origin[0] - borderWorldSize) /
                grid.tileWorldSize));
            int lastX = static_cast<int>(std::floor(
                (geometry.boundsMax[0] - grid.origin[0] + borderWorldSize) /
                grid.tileWorldSize));
            int firstZ = static_cast<int>(std::floor(
                (geometry.boundsMin[2] - grid.origin[2] - borderWorldSize) /
                grid.tileWorldSize));
            int lastZ = static_cast<int>(std::floor(
                (geometry.boundsMax[2] - grid.origin[2] + borderWorldSize) /
                grid.tileWorldSize));
            if (lastX < 0 || lastZ < 0 ||
                firstX >= grid.tilesX || firstZ >= grid.tilesZ) {
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

        bool GeometryIntersectsTile(
            const NavigationGeometry& geometry,
            const NavigationGrid& grid,
            int tileX,
            int tileZ,
            float borderWorldSize) noexcept {
            if (geometry.triangles.empty()) {
                return false;
            }
            const float minimumX = grid.origin[0] +
                static_cast<float>(tileX) * grid.tileWorldSize -
                borderWorldSize;
            const float minimumZ = grid.origin[2] +
                static_cast<float>(tileZ) * grid.tileWorldSize -
                borderWorldSize;
            const float maximumX = minimumX +
                grid.tileWorldSize + borderWorldSize * 2.0f;
            const float maximumZ = minimumZ +
                grid.tileWorldSize + borderWorldSize * 2.0f;
            return geometry.boundsMax[0] >= minimumX &&
                geometry.boundsMin[0] <= maximumX &&
                geometry.boundsMax[2] >= minimumZ &&
                geometry.boundsMin[2] <= maximumZ;
        }

        bool GatherTileGeometry(
            const NavigationChunkCache& currentChunks,
            const NavigationChunkCache& updatedChunks,
            const std::set<NavigationChunkKey>& changedChunks,
            const NavigationGrid& grid,
            int tileX,
            int tileZ,
            float borderWorldSize,
            NavigationGeometry& out,
            std::string& outDiagnostic) {
            for (const auto& [key, chunk] : currentChunks) {
                if (changedChunks.contains(key) ||
                    !GeometryIntersectsTile(
                        chunk.geometry,
                        grid,
                        tileX,
                        tileZ,
                        borderWorldSize)) {
                    continue;
                }
                if (!AppendNavigationGeometry(
                        chunk.geometry, out, outDiagnostic)) {
                    return false;
                }
            }
            for (const auto& [key, chunk] : updatedChunks) {
                (void)key;
                if (!GeometryIntersectsTile(
                        chunk.geometry,
                        grid,
                        tileX,
                        tileZ,
                        borderWorldSize)) {
                    continue;
                }
                if (!AppendNavigationGeometry(
                        chunk.geometry, out, outDiagnostic)) {
                    return false;
                }
            }
            if (!out.triangles.empty()) {
                out.boundsMin[0] = grid.origin[0];
                out.boundsMin[2] = grid.origin[2];
            }
            return true;
        }

        bool BuildIncrementalNavigation(
            dtNavMesh& navMesh,
            const NavigationGrid& grid,
            const NavigationChunkCache& currentChunks,
            const NavigationChunkCache& updatedChunks,
            const std::set<NavigationChunkKey>& changedChunks,
            const DirtyTileSet& dirtyTiles,
            uint32_t currentTileCount,
            uint32_t& outTileCount,
            std::string& outDiagnostic) {
            struct PendingTileUpdate {
                int x = 0;
                int z = 0;
                unsigned char* newData = nullptr;
                int newDataSize = 0;
                dtTileRef oldReference = 0;
                unsigned char* oldBackup = nullptr;
                int oldDataSize = 0;
                bool applied = false;
                bool newInstalled = false;
            };
            std::vector<PendingTileUpdate> updates{};
            updates.reserve(dirtyTiles.size());
            const auto releasePending = [&updates]() noexcept {
                for (PendingTileUpdate& update : updates) {
                    if (update.newData != nullptr) {
                        dtFree(update.newData);
                        update.newData = nullptr;
                    }
                    if (update.oldBackup != nullptr) {
                        dtFree(update.oldBackup);
                        update.oldBackup = nullptr;
                    }
                }
            };

            const int borderCells =
                static_cast<int>(std::ceil(kAgentRadius / kCellSize)) + 3;
            const float borderWorldSize =
                static_cast<float>(borderCells) * kCellSize;
            rcContext context(false);
            for (const auto& [tileX, tileZ] : dirtyTiles) {
                NavigationGeometry geometry{};
                if (!GatherTileGeometry(
                        currentChunks,
                        updatedChunks,
                        changedChunks,
                        grid,
                        tileX,
                        tileZ,
                        borderWorldSize,
                        geometry,
                        outDiagnostic)) {
                    releasePending();
                    return false;
                }
                bool tileBuilt = false;
                PendingTileUpdate update{};
                update.x = tileX;
                update.z = tileZ;
                if (!BuildTileData(
                        context,
                        geometry,
                        geometry.triangles,
                        tileX,
                        tileZ,
                        update.newData,
                        update.newDataSize,
                        tileBuilt,
                        outDiagnostic)) {
                    releasePending();
                    return false;
                }
                if (!tileBuilt) {
                    update.newData = nullptr;
                    update.newDataSize = 0;
                }
                updates.push_back(update);
            }

            // Back up only the dirty tiles before mutating Detour. This keeps
            // the transaction page-bounded while retaining a rollback path.
            for (PendingTileUpdate& update : updates) {
                const dtMeshTile* oldTile = navMesh.getTileAt(
                    update.x, update.z, 0);
                if (oldTile == nullptr) {
                    continue;
                }
                update.oldReference = navMesh.getTileRef(oldTile);
                update.oldDataSize = oldTile->dataSize;
                if (oldTile->data == nullptr || update.oldDataSize <= 0) {
                    outDiagnostic =
                        "Detour dirty tile has no recoverable data";
                    releasePending();
                    return false;
                }
                update.oldBackup = static_cast<unsigned char*>(
                    dtAlloc(update.oldDataSize, DT_ALLOC_PERM));
                if (update.oldBackup == nullptr) {
                    outDiagnostic =
                        "Detour failed to back up a dirty tile";
                    releasePending();
                    return false;
                }
                std::memcpy(
                    update.oldBackup,
                    oldTile->data,
                    static_cast<std::size_t>(update.oldDataSize));
            }

            const auto rollback = [&]() noexcept {
                bool restored = true;
                for (auto it = updates.rbegin();
                    it != updates.rend();
                    ++it) {
                    PendingTileUpdate& update = *it;
                    if (!update.applied) {
                        continue;
                    }
                    if (update.newInstalled) {
                        const dtTileRef installed = navMesh.getTileRefAt(
                            update.x, update.z, 0);
                        if (installed == 0 || dtStatusFailed(
                                navMesh.removeTile(
                                    installed, nullptr, nullptr))) {
                            restored = false;
                        }
                        update.newInstalled = false;
                    }
                    if (update.oldBackup != nullptr) {
                        const dtStatus status = navMesh.addTile(
                            update.oldBackup,
                            update.oldDataSize,
                            DT_TILE_FREE_DATA,
                            update.oldReference,
                            nullptr);
                        if (dtStatusFailed(status)) {
                            restored = false;
                        } else {
                            update.oldBackup = nullptr;
                        }
                    }
                    update.applied = false;
                }
                return restored;
            };

            uint32_t tileCount = currentTileCount;
            for (PendingTileUpdate& update : updates) {
                if (update.oldReference != 0) {
                    if (dtStatusFailed(navMesh.removeTile(
                            update.oldReference, nullptr, nullptr))) {
                        const bool restored = rollback();
                        outDiagnostic = restored
                            ? "Detour failed to remove a dirty tile"
                            : "Detour dirty-tile rollback failed";
                        releasePending();
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
                const dtStatus status = navMesh.addTile(
                    update.newData,
                    update.newDataSize,
                    DT_TILE_FREE_DATA,
                    update.oldReference,
                    nullptr);
                if (dtStatusFailed(status)) {
                    const bool restored = rollback();
                    outDiagnostic = restored
                        ? "Detour failed to install a dirty tile"
                        : "Detour dirty-tile rollback failed";
                    releasePending();
                    return false;
                }
                update.newData = nullptr;
                update.newInstalled = true;
                ++tileCount;
            }

            for (PendingTileUpdate& update : updates) {
                if (update.oldBackup != nullptr) {
                    dtFree(update.oldBackup);
                    update.oldBackup = nullptr;
                }
            }
            outTileCount = tileCount;
            outDiagnostic.clear();
            return true;
        }

        bool FindNearest(
            dtNavMeshQuery& query,
            const dtQueryFilter& filter,
            const MATH::Vec3& point,
            const MATH::Vec3& halfExtents,
            dtPolyRef& outReference,
            std::array<float, 3u>& outPoint) {
            const std::array<float, 3u> detourPoint = ToDetour(point);
            const MATH::Vec3 safeExtents = SanitizeExtents(halfExtents);
            const std::array<float, 3u> detourExtents =
                ToDetour(safeExtents);
            outReference = 0;
            return dtStatusSucceed(query.findNearestPoly(
                    detourPoint.data(),
                    detourExtents.data(),
                    &filter,
                    &outReference,
                    outPoint.data())) &&
                outReference != 0;
        }

    } // namespace

    struct NavigationWorldService::Impl {
        NavMeshPtr navMesh{};
        NavMeshQueryPtr query{};
        dtQueryFilter filter{};
        NavigationChunkCache chunks{};
        std::map<uint64_t, uint64_t> objectSignatures{};
        std::map<uint64_t, uint64_t> collisionRevisions{};
        NavigationGrid grid{};
        uint64_t sourceRevision = 0u;
        uint64_t lastAttemptedStampRevision =
            (std::numeric_limits<uint64_t>::max)();
        uint64_t lastAttemptedRevision =
            (std::numeric_limits<uint64_t>::max)();
        uint32_t tileCount = 0u;
        std::string diagnostic{};
        NavigationDiagnostics diagnostics{};

        Impl() {
            filter.setIncludeFlags(kWalkableFlag);
            filter.setExcludeFlags(0u);
            filter.setAreaCost(0, 1.0f);
        }
    };

    NavigationWorldService::NavigationWorldService()
        : impl_(std::make_unique<Impl>()) {}

    NavigationWorldService::~NavigationWorldService() = default;

    bool NavigationWorldService::Synchronize(World& world) noexcept {
        ++impl_->diagnostics.synchronizeCalls;
        try {
            const NavigationSourceStamp stamp =
                GatherNavigationSourceStamp(world);
            if (stamp.revision == impl_->lastAttemptedStampRevision) {
                ++impl_->diagnostics.unchangedStampFastPaths;
                return impl_->query != nullptr;
            }
            impl_->lastAttemptedStampRevision = stamp.revision;
            CurrentNavigationSources sources =
                GatherCurrentSources(world, stamp);
            ++impl_->diagnostics.sourceChunkGatherCount;
            const uint64_t revision = sources.revision;
            if (revision == impl_->lastAttemptedRevision) {
                return impl_->query != nullptr;
            }
            impl_->lastAttemptedRevision = revision;
            if (!sources.hasTerrain) {
                impl_->navMesh.reset();
                impl_->query.reset();
                impl_->chunks.clear();
                impl_->objectSignatures.clear();
                impl_->collisionRevisions.clear();
                impl_->grid = {};
                impl_->tileCount = 0u;
                impl_->sourceRevision = 0u;
                impl_->diagnostic = "scene has no TerrainSurfaceComponent";
                return false;
            }

            NavigationBuildOutput build{};
            std::string diagnostic{};
            bool fullBuild = impl_->navMesh == nullptr ||
                impl_->query == nullptr ||
                impl_->objectSignatures != sources.objectSignatures ||
                impl_->grid.tilesX <= 0 ||
                impl_->grid.tilesZ <= 0;
            std::set<NavigationChunkKey> changedChunks{};
            NavigationChunkCache updatedChunks{};
            DirtyTileSet dirtyTiles{};

            if (!fullBuild) {
                for (const auto& [key, source] : sources.chunks) {
                    const auto current = impl_->chunks.find(key);
                    if (current == impl_->chunks.end() ||
                        current->second.revision != source.revision) {
                        changedChunks.insert(key);
                    }
                }
                for (const auto& [key, chunk] : impl_->chunks) {
                    (void)chunk;
                    if (!sources.chunks.contains(key)) {
                        changedChunks.insert(key);
                    }
                }

                if (impl_->collisionRevisions !=
                        sources.collisionRevisions) {
                    for (const auto& [object, collisionRevision] :
                            sources.collisionRevisions) {
                        const auto previous =
                            impl_->collisionRevisions.find(object);
                        if (previous != impl_->collisionRevisions.end() &&
                            previous->second == collisionRevision) {
                            continue;
                        }
                        const bool hasChangedChunk = std::any_of(
                            changedChunks.begin(),
                            changedChunks.end(),
                            [object](const NavigationChunkKey& key) {
                                return key.object == object;
                            });
                        if (!hasChangedChunk) {
                            fullBuild = true;
                            break;
                        }
                    }
                }
            }

            if (!fullBuild) {
                const int borderCells = static_cast<int>(std::ceil(
                    kAgentRadius / kCellSize)) + 3;
                const float borderWorldSize =
                    static_cast<float>(borderCells) * kCellSize;
                for (const NavigationChunkKey& key : changedChunks) {
                    const auto current = impl_->chunks.find(key);
                    if (current != impl_->chunks.end()) {
                        MarkGeometryTiles(
                            current->second.geometry,
                            impl_->grid,
                            borderWorldSize,
                            dirtyTiles);
                    }
                    const auto source = sources.chunks.find(key);
                    if (source == sources.chunks.end()) {
                        continue;
                    }
                    CachedNavigationChunk updated{};
                    updated.revision = source->second.revision;
                    if (!BuildChunkGeometry(
                            source->second,
                            updated.geometry,
                            diagnostic)) {
                        ++impl_->diagnostics.lastKnownGoodRetentionCount;
                        impl_->diagnostics.lastChangedChunkCount =
                            static_cast<uint32_t>((std::min)(
                                changedChunks.size(),
                                static_cast<std::size_t>(
                                    (std::numeric_limits<uint32_t>::max)())));
                        impl_->diagnostics.lastDirtyTileCount =
                            static_cast<uint32_t>((std::min)(
                                dirtyTiles.size(),
                                static_cast<std::size_t>(
                                    (std::numeric_limits<uint32_t>::max)())));
                        impl_->diagnostic = diagnostic.empty()
                            ? "incremental navigation source build failed"
                            : diagnostic;
                        HIKARI_LOG_WARN(
                            "[Navigation] " + impl_->diagnostic +
                            "; retaining last-known-good navmesh");
                        return impl_->query != nullptr;
                    }
                    if (!GeometryFitsGrid(
                            updated.geometry,
                            impl_->grid)) {
                        fullBuild = true;
                        break;
                    }
                    MarkGeometryTiles(
                        updated.geometry,
                        impl_->grid,
                        borderWorldSize,
                        dirtyTiles);
                    updatedChunks.emplace(key, std::move(updated));
                }
            }

            impl_->diagnostics.lastChangedChunkCount =
                static_cast<uint32_t>((std::min)(
                    changedChunks.size(),
                    static_cast<std::size_t>(
                        (std::numeric_limits<uint32_t>::max)())));
            impl_->diagnostics.lastDirtyTileCount =
                static_cast<uint32_t>((std::min)(
                    dirtyTiles.size(),
                    static_cast<std::size_t>(
                        (std::numeric_limits<uint32_t>::max)())));

            bool buildSucceeded = false;
            if (fullBuild) {
                buildSucceeded = BuildNavigation(
                    sources, build, diagnostic);
            } else if (dirtyTiles.empty()) {
                buildSucceeded = true;
            } else {
                buildSucceeded = BuildIncrementalNavigation(
                    *impl_->navMesh,
                    impl_->grid,
                    impl_->chunks,
                    updatedChunks,
                    changedChunks,
                    dirtyTiles,
                    impl_->tileCount,
                    build.tileCount,
                    diagnostic);
            }
            if (!buildSucceeded) {
                ++impl_->diagnostics.lastKnownGoodRetentionCount;
                impl_->diagnostic = diagnostic.empty()
                    ? "navigation build failed"
                    : diagnostic;
                HIKARI_LOG_WARN(
                    "[Navigation] " + impl_->diagnostic +
                    "; retaining last-known-good navmesh");
                return impl_->query != nullptr;
            }

            if (fullBuild) {
                ++impl_->diagnostics.fullBuildCount;
                impl_->navMesh = std::move(build.navMesh);
                impl_->query = std::move(build.query);
                impl_->chunks = std::move(build.chunks);
                impl_->objectSignatures =
                    std::move(build.objectSignatures);
                impl_->collisionRevisions =
                    std::move(build.collisionRevisions);
                impl_->grid = build.grid;
                impl_->tileCount = build.tileCount;
            } else {
                if (!dirtyTiles.empty()) {
                    ++impl_->diagnostics.incrementalBuildCount;
                }
                if (!dirtyTiles.empty()) {
                    impl_->tileCount = build.tileCount;
                }
                for (const NavigationChunkKey& key : changedChunks) {
                    impl_->chunks.erase(key);
                }
                for (auto& [key, chunk] : updatedChunks) {
                    impl_->chunks.emplace(key, std::move(chunk));
                }
                impl_->objectSignatures = sources.objectSignatures;
                impl_->collisionRevisions = sources.collisionRevisions;
            }
            impl_->sourceRevision = revision;
            impl_->diagnostic = diagnostic;
            if (impl_->query != nullptr) {
                HIKARI_LOG_INFO(
                    "[Navigation] synchronized terrain navmesh mode=" +
                    std::string(fullBuild ? "full" : "incremental") +
                    " tiles=" +
                    std::to_string(impl_->tileCount) +
                    " dirtyTiles=" +
                    std::to_string(dirtyTiles.size()) +
                    " revision=" + std::to_string(revision));
                return true;
            }
            HIKARI_LOG_WARN(
                "[Navigation] " + impl_->diagnostic);
            return false;
        } catch (const std::exception& error) {
            ++impl_->diagnostics.lastKnownGoodRetentionCount;
            impl_->diagnostic =
                "navigation synchronization exception: " +
                std::string(error.what());
        } catch (...) {
            ++impl_->diagnostics.lastKnownGoodRetentionCount;
            impl_->diagnostic =
                "navigation synchronization failed unexpectedly";
        }
        HIKARI_LOG_WARN(
            "[Navigation] " + impl_->diagnostic +
            "; retaining last-known-good navmesh");
        return impl_->query != nullptr;
    }

    void NavigationWorldService::Clear() noexcept {
        impl_->query.reset();
        impl_->navMesh.reset();
        impl_->chunks.clear();
        impl_->objectSignatures.clear();
        impl_->collisionRevisions.clear();
        impl_->grid = {};
        impl_->sourceRevision = 0u;
        impl_->lastAttemptedStampRevision =
            (std::numeric_limits<uint64_t>::max)();
        impl_->lastAttemptedRevision =
            (std::numeric_limits<uint64_t>::max)();
        impl_->tileCount = 0u;
        impl_->diagnostic.clear();
        impl_->diagnostics = {};
    }

    bool NavigationWorldService::ProjectPoint(
        World& world,
        const MATH::Vec3& point,
        const MATH::Vec3& halfExtents,
        MATH::Vec3& outProjectedPoint) noexcept {
        outProjectedPoint = {};
        (void)world;
        if (impl_->query == nullptr) {
            return false;
        }
        dtPolyRef reference = 0;
        std::array<float, 3u> projected{};
        if (!FindNearest(
                *impl_->query,
                impl_->filter,
                point,
                halfExtents,
                reference,
                projected)) {
            return false;
        }
        outProjectedPoint = FromDetour(projected.data());
        return true;
    }

    PathResult NavigationWorldService::FindPath(
        World& world,
        const MATH::Vec3& start,
        const MATH::Vec3& end,
        const MATH::Vec3& halfExtents,
        MATH::Vec3* outPoints,
        uint8_t* outPointFlags,
        uint32_t pointCapacity) noexcept {
        PathResult result{};
        (void)world;
        if (impl_->query == nullptr) {
            return result;
        }
        try {
            dtPolyRef startReference = 0;
            dtPolyRef endReference = 0;
            std::array<float, 3u> projectedStart{};
            std::array<float, 3u> projectedEnd{};
            if (!FindNearest(
                    *impl_->query,
                    impl_->filter,
                    start,
                    halfExtents,
                    startReference,
                    projectedStart) ||
                !FindNearest(
                    *impl_->query,
                    impl_->filter,
                    end,
                    halfExtents,
                    endReference,
                    projectedEnd)) {
                result.status = QueryStatus::Failed;
                return result;
            }

            std::array<dtPolyRef, kMaxQueryPolygons> polygonPath{};
            int polygonCount = 0;
            const dtStatus pathStatus = impl_->query->findPath(
                startReference,
                endReference,
                projectedStart.data(),
                projectedEnd.data(),
                &impl_->filter,
                polygonPath.data(),
                &polygonCount,
                static_cast<int>(polygonPath.size()));
            if (dtStatusFailed(pathStatus) || polygonCount <= 0) {
                result.status = QueryStatus::Failed;
                return result;
            }

            std::array<float, 3u> effectiveEnd = projectedEnd;
            result.partial = polygonPath[
                static_cast<std::size_t>(polygonCount - 1)] != endReference;
            if (result.partial && dtStatusFailed(
                    impl_->query->closestPointOnPoly(
                        polygonPath[static_cast<std::size_t>(polygonCount - 1)],
                        projectedEnd.data(),
                        effectiveEnd.data(),
                        nullptr))) {
                result.status = QueryStatus::Failed;
                return result;
            }

            std::array<float, kMaxStraightPathPoints * 3> straightPoints{};
            std::array<unsigned char, kMaxStraightPathPoints> straightFlags{};
            std::array<dtPolyRef, kMaxStraightPathPoints> straightReferences{};
            int straightCount = 0;
            const dtStatus straightStatus = impl_->query->findStraightPath(
                projectedStart.data(),
                effectiveEnd.data(),
                polygonPath.data(),
                polygonCount,
                straightPoints.data(),
                straightFlags.data(),
                straightReferences.data(),
                &straightCount,
                kMaxStraightPathPoints,
                0);
            if (dtStatusFailed(straightStatus) || straightCount <= 0) {
                result.status = QueryStatus::Failed;
                return result;
            }

            result.pointCount = static_cast<uint32_t>(straightCount);
            for (int point = 1; point < straightCount; ++point) {
                const MATH::Vec3 previous = FromDetour(
                    &straightPoints[static_cast<std::size_t>(point - 1) * 3u]);
                const MATH::Vec3 current = FromDetour(
                    &straightPoints[static_cast<std::size_t>(point) * 3u]);
                result.pathLength += MATH::Length(current - previous);
            }
            const uint32_t writeCount = (std::min)(
                pointCapacity, result.pointCount);
            if (writeCount > 0u &&
                (outPoints == nullptr || outPointFlags == nullptr)) {
                result.status = QueryStatus::Failed;
                return result;
            }
            for (uint32_t point = 0u; point < writeCount; ++point) {
                outPoints[point] = FromDetour(
                    &straightPoints[static_cast<std::size_t>(point) * 3u]);
                outPointFlags[point] = straightFlags[point];
            }
            if (pointCapacity < result.pointCount) {
                result.status = QueryStatus::BufferTooSmall;
            } else {
                result.status = result.partial
                    ? QueryStatus::Partial
                    : QueryStatus::Success;
            }
            return result;
        } catch (...) {
            result.status = QueryStatus::Failed;
            return result;
        }
    }

    bool NavigationWorldService::Raycast(
        World& world,
        const MATH::Vec3& start,
        const MATH::Vec3& end,
        const MATH::Vec3& halfExtents,
        MATH::Vec3& outHitPoint,
        float& outNormalizedDistance) noexcept {
        outHitPoint = end;
        outNormalizedDistance = 1.0f;
        (void)world;
        if (impl_->query == nullptr) {
            return false;
        }
        try {
            dtPolyRef startReference = 0;
            std::array<float, 3u> projectedStart{};
            if (!FindNearest(
                    *impl_->query,
                    impl_->filter,
                    start,
                    halfExtents,
                    startReference,
                    projectedStart)) {
                return false;
            }
            const std::array<float, 3u> detourEnd = ToDetour(end);
            float normalizedDistance = 1.0f;
            std::array<float, 3u> hitNormal{};
            std::array<dtPolyRef, kMaxQueryPolygons> visited{};
            int visitedCount = 0;
            const dtStatus status = impl_->query->raycast(
                startReference,
                projectedStart.data(),
                detourEnd.data(),
                &impl_->filter,
                &normalizedDistance,
                hitNormal.data(),
                visited.data(),
                &visitedCount,
                static_cast<int>(visited.size()));
            if (dtStatusFailed(status)) {
                return false;
            }
            if (!std::isfinite(normalizedDistance) ||
                normalizedDistance > 1.0f) {
                normalizedDistance = 1.0f;
            }
            normalizedDistance = (std::clamp)(
                normalizedDistance, 0.0f, 1.0f);
            outNormalizedDistance = normalizedDistance;
            const MATH::Vec3 projectedStartPoint =
                FromDetour(projectedStart.data());
            outHitPoint = projectedStartPoint +
                (end - projectedStartPoint) * normalizedDistance;
            return true;
        } catch (...) {
            return false;
        }
    }

    uint64_t NavigationWorldService::GetSourceRevision() const noexcept {
        return impl_->sourceRevision;
    }

    const std::string& NavigationWorldService::GetDiagnostic() const noexcept {
        return impl_->diagnostic;
    }

    const NavigationDiagnostics&
        NavigationWorldService::GetDiagnostics() const noexcept {
        return impl_->diagnostics;
    }

} // namespace HIKARI::NAVIGATION
