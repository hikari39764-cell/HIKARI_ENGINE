#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace HIKARI::WATER {

    struct WaterSurfaceQueryService::Snapshot {
        std::vector<WaterSurfaceSnapshotEntry> surfaces{};
        uint64_t revision = 0u;
    };

    namespace {
        MATH::Vec3 TransformPoint(
            const MATH::Mat4& matrix,
            const MATH::Vec3& value) noexcept {
            const MATH::Vec4 transformed = matrix.TransformPoint({
                value.x, value.y, value.z, 1.0f });
            return { transformed.x, transformed.y, transformed.z };
        }

        MATH::Vec3 TransformVector(
            const MATH::Mat4& matrix,
            const MATH::Vec3& value) noexcept {
            const MATH::Vec4 transformed = matrix.TransformPoint({
                value.x, value.y, value.z, 0.0f });
            return { transformed.x, transformed.y, transformed.z };
        }

        bool MatrixEqual(
            const MATH::Mat4& left,
            const MATH::Mat4& right) noexcept {
            for (uint32_t column = 0u; column < 4u; ++column) {
                for (uint32_t row = 0u; row < 4u; ++row) {
                    if (left.m[column][row] != right.m[column][row]) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool SnapshotEntryEqual(
            const WaterSurfaceSnapshotEntry& left,
            const WaterSurfaceSnapshotEntry& right) noexcept {
            return left.object == right.object &&
                MatrixEqual(left.world, right.world) &&
                left.halfExtents.x == right.halfExtents.x &&
                left.halfExtents.y == right.halfExtents.y &&
                left.maximumHorizontalDisplacement ==
                    right.maximumHorizontalDisplacement &&
                WaterSurfaceSettingsEqual(
                    left.settings,
                    right.settings);
        }

        bool SnapshotEqual(
            const std::vector<WaterSurfaceSnapshotEntry>& left,
            const std::vector<WaterSurfaceSnapshotEntry>& right) noexcept {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t index = 0u; index < left.size(); ++index) {
                if (!SnapshotEntryEqual(left[index], right[index])) {
                    return false;
                }
            }
            return true;
        }

        bool EvaluateCandidate(
            const WaterSurfaceSnapshotEntry& surface,
            const WaterSurfaceQuery& query,
            uint64_t revision,
            WaterSurfaceSample& outSample) noexcept {

            const MATH::Vec3 queryLocal = TransformPoint(
                surface.inverseWorld,
                query.positionWS);
            MATH::Vec2 sourceLocalXZ{};
            const WaterSurfaceEvaluation evaluation =
                EvaluateWaterSurfaceAtHorizontalPosition(
                    surface.settings,
                    { queryLocal.x, queryLocal.z },
                    query.timeSeconds,
                    &sourceLocalXZ);
            if (std::abs(sourceLocalXZ.x) > surface.halfExtents.x ||
                std::abs(sourceLocalXZ.y) > surface.halfExtents.y) {
                return false;
            }

            const MATH::Vec3 positionWS = TransformPoint(
                surface.world,
                evaluation.positionLocal);
            MATH::Vec3 normalWS = MATH::Normalize(TransformVector(
                surface.normalMatrix,
                evaluation.normalLocal));
            if (MATH::Length(normalWS) <= 1.0e-6f) {
                normalWS = { 0.0f, 1.0f, 0.0f };
            }
            const MATH::Vec3 delta = query.positionWS - positionWS;
            const float signedDistance = MATH::Dot(delta, normalWS);

            outSample = {};
            outSample.valid = true;
            outSample.surfaceObject = surface.object;
            outSample.surfacePositionWS = positionWS;
            outSample.normalWS = normalWS;
            outSample.signedDistanceToSurface = signedDistance;
            outSample.submergenceDepth = (std::max)(
                -signedDistance,
                0.0f);
            outSample.validFields = WaterSurfaceSampleField::Surface;
            if (HasSampleField(
                    query.requestedFields,
                    WaterSurfaceSampleField::Velocity)) {
                outSample.velocityWS = TransformVector(
                    surface.world,
                    evaluation.velocityLocal);
                outSample.validFields = outSample.validFields |
                    WaterSurfaceSampleField::Velocity;
            }
            outSample.snapshotRevision = revision;
            outSample.evaluatedTimeSeconds = query.timeSeconds;
            return true;
        }

        void ResolveColumnDepth(
            const WaterSurfaceQuery& query,
            const IWaterBathymetryProvider* provider,
            WaterSurfaceSample& sample) noexcept {

            if (provider == nullptr ||
                !HasSampleField(
                    query.requestedFields,
                    WaterSurfaceSampleField::ColumnDepth) ||
                !std::isfinite(query.maximumColumnDepth) ||
                query.maximumColumnDepth <= 0.0f) {
                return;
            }

            WaterBathymetryRay ray{};
            ray.surfaceObject = sample.surfaceObject;
            ray.originWS = sample.surfacePositionWS;
            ray.directionWS = sample.normalWS * -1.0f;
            ray.maximumDistance = query.maximumColumnDepth;

            float depth = 0.0f;
            if (!provider->RaycastBathymetry(ray, depth) ||
                !std::isfinite(depth) ||
                depth < 0.0f ||
                depth > ray.maximumDistance) {
                return;
            }

            sample.waterColumnDepth = depth;
            sample.validFields = sample.validFields |
                WaterSurfaceSampleField::ColumnDepth;
        }

        bool SampleNearestFromSnapshot(
            std::span<const WaterSurfaceSnapshotEntry> surfaces,
            uint64_t revision,
            const IWaterBathymetryProvider* provider,
            const WaterSurfaceQuery& query,
            WaterSurfaceSample& outSample) noexcept {

            outSample = {};
            outSample.snapshotRevision = revision;
            outSample.evaluatedTimeSeconds = query.timeSeconds;
            if (query.preferredSurface.IsValid()) {
                for (const WaterSurfaceSnapshotEntry& surface : surfaces) {
                    if (surface.object == query.preferredSurface &&
                        EvaluateCandidate(
                            surface,
                            query,
                            revision,
                            outSample)) {
                        ResolveColumnDepth(query, provider, outSample);
                        return true;
                    }
                }
            }

            float nearestDistance =
                (std::numeric_limits<float>::max)();
            WaterSurfaceSample nearest{};
            for (const WaterSurfaceSnapshotEntry& surface : surfaces) {
                WaterSurfaceSample candidate{};
                if (!EvaluateCandidate(
                        surface,
                        query,
                        revision,
                        candidate)) {
                    continue;
                }
                const float distance =
                    std::abs(candidate.signedDistanceToSurface);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearest = candidate;
                }
            }
            if (!nearest.valid) {
                return false;
            }
            ResolveColumnDepth(query, provider, nearest);
            outSample = nearest;
            return true;
        }
    }

    void WaterSurfaceQueryService::SetBathymetryProvider(
        const IWaterBathymetryProvider* provider) noexcept {

        bathymetryProvider_.store(provider, std::memory_order_release);
    }

    bool WaterSurfaceQueryService::SampleNearest(
        const WaterSurfaceQuery& query,
        WaterSurfaceSample& outSample) const noexcept {

        const std::shared_ptr<const Snapshot> snapshot =
            snapshot_.load(std::memory_order_acquire);
        const IWaterBathymetryProvider* provider =
            bathymetryProvider_.load(std::memory_order_acquire);
        const std::span<const WaterSurfaceSnapshotEntry> surfaces =
            snapshot != nullptr
            ? std::span<const WaterSurfaceSnapshotEntry>(snapshot->surfaces)
            : std::span<const WaterSurfaceSnapshotEntry>{};
        return SampleNearestFromSnapshot(
            surfaces,
            snapshot != nullptr ? snapshot->revision : 0u,
            provider,
            query,
            outSample);
    }

    std::size_t WaterSurfaceQueryService::SampleBatch(
        std::span<const WaterSurfaceQuery> queries,
        std::span<WaterSurfaceSample> outSamples) const noexcept {

        const std::size_t count = (std::min)(
            queries.size(),
            outSamples.size());
        const std::shared_ptr<const Snapshot> snapshot =
            snapshot_.load(std::memory_order_acquire);
        const IWaterBathymetryProvider* provider =
            bathymetryProvider_.load(std::memory_order_acquire);
        const std::span<const WaterSurfaceSnapshotEntry> surfaces =
            snapshot != nullptr
            ? std::span<const WaterSurfaceSnapshotEntry>(snapshot->surfaces)
            : std::span<const WaterSurfaceSnapshotEntry>{};
        const uint64_t revision =
            snapshot != nullptr ? snapshot->revision : 0u;
        for (std::size_t index = 0u; index < count; ++index) {
            (void)SampleNearestFromSnapshot(
                surfaces,
                revision,
                provider,
                queries[index],
                outSamples[index]);
        }
        return count;
    }

    uint64_t WaterSurfaceQueryService::GetSnapshotRevision() const noexcept {
        const std::shared_ptr<const Snapshot> snapshot =
            snapshot_.load(std::memory_order_acquire);
        return snapshot != nullptr ? snapshot->revision : 0u;
    }

    std::size_t WaterSurfaceQueryService::GetSurfaceCount() const noexcept {
        const std::shared_ptr<const Snapshot> snapshot =
            snapshot_.load(std::memory_order_acquire);
        return snapshot != nullptr ? snapshot->surfaces.size() : 0u;
    }

    void WaterSurfaceQueryService::PublishSnapshot(
        std::vector<WaterSurfaceSnapshotEntry> surfaces) {

        std::scoped_lock lock(publishMutex_);
        const std::shared_ptr<const Snapshot> current =
            snapshot_.load(std::memory_order_acquire);
        if (current != nullptr &&
            SnapshotEqual(current->surfaces, surfaces)) {
            return;
        }
        auto next = std::make_shared<Snapshot>();
        next->surfaces = std::move(surfaces);
        next->revision = current != nullptr
            ? current->revision + 1u
            : 1u;
        snapshot_.store(
            std::shared_ptr<const Snapshot>(std::move(next)),
            std::memory_order_release);
    }

    void WaterSurfaceQueryService::Clear() {

        std::scoped_lock lock(publishMutex_);
        const std::shared_ptr<const Snapshot> current =
            snapshot_.load(std::memory_order_acquire);
        if (current == nullptr || current->surfaces.empty()) {
            return;
        }
        auto next = std::make_shared<Snapshot>();
        next->revision = current->revision + 1u;
        snapshot_.store(
            std::shared_ptr<const Snapshot>(std::move(next)),
            std::memory_order_release);
    }

} // namespace HIKARI::WATER
