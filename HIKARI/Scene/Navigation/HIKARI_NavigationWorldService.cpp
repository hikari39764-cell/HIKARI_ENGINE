#include "Scene/Navigation/HIKARI_NavigationWorldService.h"
#include "HIKARI_NavigationBuild.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include "Core/HIKARI_Logger.h"
#include "Core/Numeric/HIKARI_IntegerConversion.h"

namespace HIKARI::NAVIGATION {
using namespace BUILD;
namespace {
constexpr int kMaxQueryPolygons = 256;
constexpr int kMaxStraightPathPoints = 256;
MATH::Vec3 SanitizeExtents(const MATH::Vec3& extents) noexcept {
    const auto finitePositive = [](float value, float fallback) noexcept {
        return std::isfinite(value) && value > 1.0e-4f ? value : fallback;
    };
    return {finitePositive(extents.x, 1.0f), finitePositive(extents.y, 2.0f), finitePositive(extents.z, 1.0f)};
}

std::array<float, 3u> ToDetour(const MATH::Vec3& value) noexcept { return {value.x, value.y, value.z}; }

MATH::Vec3 FromDetour(const float* value) noexcept { return {value[0], value[1], value[2]}; }

bool FindNearest(dtNavMeshQuery& query, const dtQueryFilter& filter, const MATH::Vec3& point,
                 const MATH::Vec3& halfExtents, dtPolyRef& outReference, std::array<float, 3u>& outPoint) {
    const std::array<float, 3u> detourPoint = ToDetour(point);
    const MATH::Vec3 safeExtents = SanitizeExtents(halfExtents);
    const std::array<float, 3u> detourExtents = ToDetour(safeExtents);
    outReference = 0;
    return dtStatusSucceed(query.findNearestPoly(detourPoint.data(), detourExtents.data(), &filter, &outReference,
                                                 outPoint.data())) &&
           outReference != 0;
}

} // namespace

namespace {
struct NavigationBuildJob {
    NavigationSourceStamp stamp;
    uint64_t revision = 0;
    bool full = false, succeeded = false;
    NavigationBuildOutput build;
    NavigationGeometry geometry;
    NavigationChunkCache updated;
    std::set<NavigationChunkKey> changed;
    DirtyTileSet dirty;
    PendingTileUpdates tiles;
    std::string diagnostic;
};
} // namespace

struct NavigationWorldService::Impl {
    NavigationBuildOutput live;
    NavigationSourceStamp sourceStamp;
    dtQueryFilter filter{};
    uint64_t sourceRevision = 0u;
    uint64_t lastAttemptedStampRevision = (std::numeric_limits<uint64_t>::max)();
    uint64_t lastAttemptedRevision = (std::numeric_limits<uint64_t>::max)();
    std::string diagnostic{};
    NavigationDiagnostics diagnostics{};
    std::future<std::unique_ptr<NavigationBuildJob>> pending;

    ~Impl() {
        if (pending.valid())
            pending.wait();
    }

    void RecordChangedCounts(const NavigationBuildJob& job) noexcept {
        diagnostics.lastChangedChunkCount = NUMERIC::SaturateToUint32(job.changed.size());
        diagnostics.lastDirtyTileCount = NUMERIC::SaturateToUint32(job.dirty.size());
    }

    bool Publish(NavigationBuildJob& job) {
        auto& build = job.build;
        if (job.succeeded && !job.full && !job.dirty.empty())
            job.succeeded =
                CommitIncrementalNavigation(*live.navMesh, job.tiles, live.tileCount, build.tileCount, job.diagnostic);
        if (!job.succeeded) {
            ++diagnostics.lastKnownGoodRetentionCount;
            diagnostic = job.diagnostic.empty() ? "navigation build failed" : job.diagnostic;
            HIKARI_LOG_WARN("[Navigation] " + diagnostic + "; retaining last-known-good navmesh");
            return live.query != nullptr;
        }
        if (job.full) {
            ++diagnostics.fullBuildCount;
            live = std::move(build);
        } else {
            if (!job.dirty.empty()) {
                ++diagnostics.incrementalBuildCount;
                live.tileCount = build.tileCount;
            }
            for (const auto& key : job.changed)
                live.chunks.erase(key);
            for (auto& [key, chunk] : job.updated)
                live.chunks.emplace(key, std::move(chunk));
        }
        sourceStamp = std::move(job.stamp);
        sourceRevision = job.revision;
        diagnostic = std::move(job.diagnostic);
        return live.query != nullptr;
    }

    Impl() {
        filter.setIncludeFlags(kWalkableFlag);
        filter.setExcludeFlags(0u);
        filter.setAreaCost(0, 1.0f);
    }
};

NavigationWorldService::NavigationWorldService() : impl_(std::make_unique<Impl>()) {}

NavigationWorldService::~NavigationWorldService() = default;

bool NavigationWorldService::Synchronize(World& world, SynchronizationMode mode) noexcept {
    ++impl_->diagnostics.synchronizeCalls;
    try {
        NavigationSourceStamp stamp = GatherNavigationSourceStamp(world);
        if (impl_->pending.valid()) {
            if (mode == SynchronizationMode::Background &&
                impl_->pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return impl_->live.query != nullptr;
            auto completed = impl_->pending.get();
            impl_->diagnostics.buildPending = false;
            if (completed->stamp.revision == stamp.revision)
                return impl_->Publish(*completed);
            ++impl_->diagnostics.discardedBuildCount;
            impl_->lastAttemptedStampRevision = UINT64_MAX;
            impl_->lastAttemptedRevision = UINT64_MAX;
        }
        if (stamp.revision == impl_->lastAttemptedStampRevision) {
            ++impl_->diagnostics.unchangedStampFastPaths;
            return impl_->live.query != nullptr;
        }
        impl_->lastAttemptedStampRevision = stamp.revision;
        CurrentNavigationSources sources = GatherCurrentSources(world, stamp);
        ++impl_->diagnostics.sourceChunkGatherCount;
        const uint64_t revision = sources.revision;
        if (revision == impl_->lastAttemptedRevision) {
            return impl_->live.query != nullptr;
        }
        impl_->lastAttemptedRevision = revision;
        if (!stamp.hasTerrain) {
            impl_->live = {};
            impl_->sourceStamp = {};
            impl_->sourceRevision = 0u;
            impl_->diagnostic = "scene has no TerrainSurfaceComponent";
            return false;
        }

        auto job = std::make_unique<NavigationBuildJob>();
        job->revision = revision;
        auto& build = job->build;
        auto& diagnostic = job->diagnostic;
        auto& fullBuild = job->full;
        fullBuild = impl_->live.navMesh == nullptr || impl_->live.query == nullptr ||
                    impl_->sourceStamp.objectSignatures != stamp.objectSignatures || impl_->live.grid.tilesX <= 0 ||
                    impl_->live.grid.tilesZ <= 0;
        auto& changedChunks = job->changed;
        auto& updatedChunks = job->updated;
        auto& dirtyTiles = job->dirty;

        if (!fullBuild) {
            for (const auto& [key, source] : sources.chunks) {
                const auto current = impl_->live.chunks.find(key);
                if (current == impl_->live.chunks.end() || current->second.revision != source.revision) {
                    changedChunks.insert(key);
                }
            }
            for (const auto& [key, chunk] : impl_->live.chunks) {
                (void)chunk;
                if (!sources.chunks.contains(key)) {
                    changedChunks.insert(key);
                }
            }

            if (impl_->sourceStamp.collisionRevisions != stamp.collisionRevisions) {
                for (const auto& [object, collisionRevision] : stamp.collisionRevisions) {
                    const auto previous = impl_->sourceStamp.collisionRevisions.find(object);
                    if (previous != impl_->sourceStamp.collisionRevisions.end() &&
                        previous->second == collisionRevision) {
                        continue;
                    }
                    const bool hasChangedChunk =
                        std::any_of(changedChunks.begin(), changedChunks.end(),
                                    [object](const NavigationChunkKey& key) { return key.object == object; });
                    if (!hasChangedChunk) {
                        fullBuild = true;
                        break;
                    }
                }
            }
        }

        if (!fullBuild) {
            const float borderWorldSize = NavigationBorderWorldSize();
            for (const NavigationChunkKey& key : changedChunks) {
                const auto current = impl_->live.chunks.find(key);
                if (current != impl_->live.chunks.end()) {
                    MarkGeometryTiles(current->second.geometry, impl_->live.grid, borderWorldSize, dirtyTiles);
                }
                const auto source = sources.chunks.find(key);
                if (source == sources.chunks.end()) {
                    continue;
                }
                CachedNavigationChunk updated{};
                updated.revision = source->second.revision;
                if (!BuildChunkGeometry(source->second, updated.geometry, diagnostic)) {
                    impl_->RecordChangedCounts(*job);
                    return impl_->Publish(*job);
                }
                if (!GeometryFitsGrid(updated.geometry, impl_->live.grid)) {
                    fullBuild = true;
                    break;
                }
                MarkGeometryTiles(updated.geometry, impl_->live.grid, borderWorldSize, dirtyTiles);
                updatedChunks.emplace(key, std::move(updated));
            }
        }

        impl_->RecordChangedCounts(*job);

        job->stamp = std::move(stamp);
        if (fullBuild && !GatherGeometry(sources, job->geometry, build.chunks, diagnostic))
            return impl_->Publish(*job);

        // World/Terrain は所有スレッドだけで読む。Recast は独立した geometry を処理し、
        // 生きている navMesh/query には触れない。実行中は chunks を変更しない。
        auto work = [job = std::move(job), current = &impl_->live.chunks, grid = impl_->live.grid]() mutable {
            if (job->full)
                job->succeeded = BuildNavigation(job->geometry, job->build, job->diagnostic);
            else
                job->succeeded = PrepareIncrementalNavigation(grid, *current, job->updated, job->changed, job->dirty,
                                                              job->tiles, job->diagnostic);
            return std::move(job);
        };
        if (mode == SynchronizationMode::Background) {
            impl_->pending = std::async(std::launch::async, std::move(work));
            ++impl_->diagnostics.backgroundBuildCount;
            impl_->diagnostics.buildPending = true;
            return impl_->live.query != nullptr;
        }
        auto completed = work();
        return impl_->Publish(*completed);
    } catch (const std::exception& error) {
        ++impl_->diagnostics.lastKnownGoodRetentionCount;
        impl_->diagnostic = "navigation synchronization exception: " + std::string(error.what());
    } catch (...) {
        ++impl_->diagnostics.lastKnownGoodRetentionCount;
        impl_->diagnostic = "navigation synchronization failed unexpectedly";
    }
    impl_->diagnostics.buildPending = false;
    HIKARI_LOG_WARN("[Navigation] " + impl_->diagnostic + "; retaining last-known-good navmesh");
    return impl_->live.query != nullptr;
}

void NavigationWorldService::Clear() noexcept {
    // 破棄時のみ待つ。worker が借用している immutable chunk cache の寿命を守る。
    if (impl_->pending.valid()) {
        impl_->pending.wait();
        impl_->pending = {};
    }
    impl_->live = {};
    impl_->sourceStamp = {};
    impl_->sourceRevision = 0u;
    impl_->lastAttemptedStampRevision = (std::numeric_limits<uint64_t>::max)();
    impl_->lastAttemptedRevision = (std::numeric_limits<uint64_t>::max)();
    impl_->diagnostic.clear();
    impl_->diagnostics = {};
}

bool NavigationWorldService::ProjectPoint(World& world, const MATH::Vec3& point, const MATH::Vec3& halfExtents,
                                          MATH::Vec3& outProjectedPoint) noexcept {
    outProjectedPoint = {};
    (void)world;
    if (impl_->live.query == nullptr) {
        return false;
    }
    dtPolyRef reference = 0;
    std::array<float, 3u> projected{};
    if (!FindNearest(*impl_->live.query, impl_->filter, point, halfExtents, reference, projected)) {
        return false;
    }
    outProjectedPoint = FromDetour(projected.data());
    return true;
}

PathResult NavigationWorldService::FindPath(World& world, const MATH::Vec3& start, const MATH::Vec3& end,
                                            const MATH::Vec3& halfExtents, MATH::Vec3* outPoints,
                                            uint8_t* outPointFlags, uint32_t pointCapacity) noexcept {
    PathResult result{};
    (void)world;
    if (impl_->live.query == nullptr) {
        return result;
    }
    try {
        dtPolyRef startReference = 0;
        dtPolyRef endReference = 0;
        std::array<float, 3u> projectedStart{};
        std::array<float, 3u> projectedEnd{};
        if (!FindNearest(*impl_->live.query, impl_->filter, start, halfExtents, startReference, projectedStart) ||
            !FindNearest(*impl_->live.query, impl_->filter, end, halfExtents, endReference, projectedEnd)) {
            result.status = QueryStatus::Failed;
            return result;
        }

        std::array<dtPolyRef, kMaxQueryPolygons> polygonPath{};
        int polygonCount = 0;
        const dtStatus pathStatus = impl_->live.query->findPath(startReference, endReference, projectedStart.data(),
                                                                projectedEnd.data(), &impl_->filter, polygonPath.data(),
                                                                &polygonCount, static_cast<int>(polygonPath.size()));
        if (dtStatusFailed(pathStatus) || polygonCount <= 0) {
            result.status = QueryStatus::Failed;
            return result;
        }

        std::array<float, 3u> effectiveEnd = projectedEnd;
        result.partial = polygonPath[static_cast<std::size_t>(polygonCount - 1)] != endReference;
        if (result.partial && dtStatusFailed(impl_->live.query->closestPointOnPoly(
                                  polygonPath[static_cast<std::size_t>(polygonCount - 1)], projectedEnd.data(),
                                  effectiveEnd.data(), nullptr))) {
            result.status = QueryStatus::Failed;
            return result;
        }

        std::array<float, kMaxStraightPathPoints * 3> straightPoints{};
        std::array<unsigned char, kMaxStraightPathPoints> straightFlags{};
        std::array<dtPolyRef, kMaxStraightPathPoints> straightReferences{};
        int straightCount = 0;
        const dtStatus straightStatus = impl_->live.query->findStraightPath(
            projectedStart.data(), effectiveEnd.data(), polygonPath.data(), polygonCount, straightPoints.data(),
            straightFlags.data(), straightReferences.data(), &straightCount, kMaxStraightPathPoints, 0);
        if (dtStatusFailed(straightStatus) || straightCount <= 0) {
            result.status = QueryStatus::Failed;
            return result;
        }

        result.pointCount = static_cast<uint32_t>(straightCount);
        for (int point = 1; point < straightCount; ++point) {
            const MATH::Vec3 previous = FromDetour(&straightPoints[static_cast<std::size_t>(point - 1) * 3u]);
            const MATH::Vec3 current = FromDetour(&straightPoints[static_cast<std::size_t>(point) * 3u]);
            result.pathLength += MATH::Length(current - previous);
        }
        const uint32_t writeCount = (std::min)(pointCapacity, result.pointCount);
        if (writeCount > 0u && (outPoints == nullptr || outPointFlags == nullptr)) {
            result.status = QueryStatus::Failed;
            return result;
        }
        for (uint32_t point = 0u; point < writeCount; ++point) {
            outPoints[point] = FromDetour(&straightPoints[static_cast<std::size_t>(point) * 3u]);
            outPointFlags[point] = straightFlags[point];
        }
        if (pointCapacity < result.pointCount) {
            result.status = QueryStatus::BufferTooSmall;
        } else {
            result.status = result.partial ? QueryStatus::Partial : QueryStatus::Success;
        }
        return result;
    } catch (...) {
        result.status = QueryStatus::Failed;
        return result;
    }
}

bool NavigationWorldService::Raycast(World& world, const MATH::Vec3& start, const MATH::Vec3& end,
                                     const MATH::Vec3& halfExtents, MATH::Vec3& outHitPoint,
                                     float& outNormalizedDistance) noexcept {
    outHitPoint = end;
    outNormalizedDistance = 1.0f;
    (void)world;
    if (impl_->live.query == nullptr) {
        return false;
    }
    try {
        dtPolyRef startReference = 0;
        std::array<float, 3u> projectedStart{};
        if (!FindNearest(*impl_->live.query, impl_->filter, start, halfExtents, startReference, projectedStart)) {
            return false;
        }
        const std::array<float, 3u> detourEnd = ToDetour(end);
        float normalizedDistance = 1.0f;
        std::array<float, 3u> hitNormal{};
        std::array<dtPolyRef, kMaxQueryPolygons> visited{};
        int visitedCount = 0;
        const dtStatus status = impl_->live.query->raycast(
            startReference, projectedStart.data(), detourEnd.data(), &impl_->filter, &normalizedDistance,
            hitNormal.data(), visited.data(), &visitedCount, static_cast<int>(visited.size()));
        if (dtStatusFailed(status)) {
            return false;
        }
        if (!std::isfinite(normalizedDistance) || normalizedDistance > 1.0f) {
            normalizedDistance = 1.0f;
        }
        normalizedDistance = (std::clamp)(normalizedDistance, 0.0f, 1.0f);
        outNormalizedDistance = normalizedDistance;
        const MATH::Vec3 projectedStartPoint = FromDetour(projectedStart.data());
        outHitPoint = projectedStartPoint + (end - projectedStartPoint) * normalizedDistance;
        return true;
    } catch (...) {
        return false;
    }
}

uint64_t NavigationWorldService::GetSourceRevision() const noexcept { return impl_->sourceRevision; }

const std::string& NavigationWorldService::GetDiagnostic() const noexcept { return impl_->diagnostic; }

const NavigationDiagnostics& NavigationWorldService::GetDiagnostics() const noexcept { return impl_->diagnostics; }

} // namespace HIKARI::NAVIGATION
