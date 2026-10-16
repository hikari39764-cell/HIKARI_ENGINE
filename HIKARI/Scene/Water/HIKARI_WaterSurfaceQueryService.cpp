#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace HIKARI::WATER {

namespace {
bool ResolveWorldPlanePoint(const WaterSurfaceSnapshotEntry& surface, const MATH::Vec2& worldXZ,
                            MATH::Vec3& outPoint) noexcept {
    const MATH::Vec3 origin = MATH::TransformPoint(surface.world, {0.0f, 0.0f, 0.0f});
    const MATH::Vec3 normal = MATH::Normalize(MATH::TransformVector(surface.normalMatrix, {0.0f, 1.0f, 0.0f}));
    if (MATH::Length(normal) <= 1.0e-6f || std::abs(normal.y) <= 1.0e-5f) {
        return false;
    }
    outPoint = {worldXZ.x,
                origin.y - (normal.x * (worldXZ.x - origin.x) + normal.z * (worldXZ.y - origin.z)) / normal.y,
                worldXZ.y};
    return std::isfinite(outPoint.x) && std::isfinite(outPoint.y) && std::isfinite(outPoint.z);
}

MATH::Vec2 WorldDeltaToLocalXZ(const WaterSurfaceSnapshotEntry& surface, const MATH::Vec2& worldDelta) noexcept {
    const MATH::Vec3 basisX = MATH::TransformVector(surface.world, {1.0f, 0.0f, 0.0f});
    const MATH::Vec3 basisZ = MATH::TransformVector(surface.world, {0.0f, 0.0f, 1.0f});
    const float determinant = basisX.x * basisZ.z - basisX.z * basisZ.x;
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-6f) {
        return worldDelta;
    }
    const float inverseDeterminant = 1.0f / determinant;
    return {(worldDelta.x * basisZ.z - worldDelta.y * basisZ.x) * inverseDeterminant,
            (basisX.x * worldDelta.y - basisX.z * worldDelta.x) * inverseDeterminant};
}

bool SnapshotEntryEqual(const WaterSurfaceSnapshotEntry& left, const WaterSurfaceSnapshotEntry& right) noexcept {
    return left.object == right.object && left.world == right.world && left.halfExtents.x == right.halfExtents.x &&
           left.halfExtents.y == right.halfExtents.y &&
           left.maximumHorizontalDisplacement == right.maximumHorizontalDisplacement &&
           WaterSurfaceSettingsEqual(left.settings, right.settings);
}

bool SnapshotEqual(const std::vector<WaterSurfaceSnapshotEntry>& left,
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

bool EvaluateCandidate(const WaterSurfaceSnapshotEntry& surface, const WaterSurfaceQuery& query, uint64_t revision,
                       WaterSurfaceSample& outSample) noexcept {
    const bool cameraCenteredOcean = surface.settings.geometryMode == WaterSurfaceGeometryMode::CameraCenteredOcean;
    const MATH::Vec3 queryLocal = MATH::TransformPoint(surface.inverseWorld, query.positionWS);
    MATH::Vec2 sourceLocalXZ{};
    const WaterSurfaceEvaluation evaluation = EvaluateWaterSurfaceAtHorizontalPosition(
        surface.settings,
        cameraCenteredOcean ? MATH::Vec2{query.positionWS.x, query.positionWS.z}
                            : MATH::Vec2{queryLocal.x, queryLocal.z},
        query.timeSeconds, &sourceLocalXZ);
    if (!cameraCenteredOcean &&
        (std::abs(sourceLocalXZ.x) > surface.halfExtents.x || std::abs(sourceLocalXZ.y) > surface.halfExtents.y)) {
        return false;
    }
    MATH::Vec3 positionWS{};
    MATH::Vec3 normalWS{};
    if (cameraCenteredOcean) {
        MATH::Vec3 baseWorld{};
        if (!ResolveWorldPlanePoint(surface, sourceLocalXZ, baseWorld)) {
            return false;
        }
        const MATH::Vec3 baseLocal = MATH::TransformPoint(surface.inverseWorld, baseWorld);
        const MATH::Vec2 localHorizontalDelta = WorldDeltaToLocalXZ(
            surface, {evaluation.positionLocal.x - sourceLocalXZ.x, evaluation.positionLocal.z - sourceLocalXZ.y});
        positionWS = MATH::TransformPoint(surface.world, {baseLocal.x + localHorizontalDelta.x,
                                                          baseLocal.y + evaluation.positionLocal.y,
                                                          baseLocal.z + localHorizontalDelta.y});
        normalWS = MATH::Normalize(evaluation.normalLocal);
    } else {
        positionWS = MATH::TransformPoint(surface.world, evaluation.positionLocal);
        normalWS = MATH::Normalize(MATH::TransformVector(surface.normalMatrix, evaluation.normalLocal));
    }
    if (MATH::Length(normalWS) <= 1.0e-6f) {
        normalWS = {0.0f, 1.0f, 0.0f};
    }
    const MATH::Vec3 delta = query.positionWS - positionWS;
    const float signedDistance = MATH::Dot(delta, normalWS);

    outSample = {};
    outSample.valid = true;
    outSample.surfaceObject = surface.object;
    outSample.surfacePositionWS = positionWS;
    outSample.normalWS = normalWS;
    outSample.signedDistanceToSurface = signedDistance;
    outSample.submergenceDepth = (std::max)(-signedDistance, 0.0f);
    outSample.validFields = WaterSurfaceSampleField::Surface;
    if (HasSampleField(query.requestedFields, WaterSurfaceSampleField::Velocity)) {
        if (cameraCenteredOcean) {
            const MATH::Vec2 localHorizontalVelocity =
                WorldDeltaToLocalXZ(surface, {evaluation.velocityLocal.x, evaluation.velocityLocal.z});
            outSample.velocityWS = MATH::TransformVector(
                surface.world, {localHorizontalVelocity.x, evaluation.velocityLocal.y, localHorizontalVelocity.y});
        } else {
            outSample.velocityWS = MATH::TransformVector(surface.world, evaluation.velocityLocal);
        }
        outSample.validFields = outSample.validFields | WaterSurfaceSampleField::Velocity;
    }
    outSample.snapshotRevision = revision;
    outSample.evaluatedTimeSeconds = query.timeSeconds;
    return true;
}

void ResolveColumnDepth(const WaterSurfaceQuery& query, const IWaterBathymetryProvider* provider,
                        WaterSurfaceSample& sample) noexcept {
    if (provider == nullptr || !HasSampleField(query.requestedFields, WaterSurfaceSampleField::ColumnDepth) ||
        !std::isfinite(query.maximumColumnDepth) || query.maximumColumnDepth <= 0.0f) {
        return;
    }

    WaterBathymetryRay ray{};
    ray.surfaceObject = sample.surfaceObject;
    ray.originWS = sample.surfacePositionWS;
    ray.directionWS = sample.normalWS * -1.0f;
    ray.maximumDistance = query.maximumColumnDepth;

    float depth = 0.0f;
    if (!provider->RaycastBathymetry(ray, depth) || !std::isfinite(depth) || depth < 0.0f ||
        depth > ray.maximumDistance) {
        return;
    }

    sample.waterColumnDepth = depth;
    sample.validFields = sample.validFields | WaterSurfaceSampleField::ColumnDepth;
}

bool SampleNearestFromSnapshot(std::span<const WaterSurfaceSnapshotEntry> surfaces, uint64_t revision,
                               const IWaterBathymetryProvider* provider, const WaterSurfaceQuery& query,
                               WaterSurfaceSample& outSample) noexcept {
    outSample = {};
    outSample.snapshotRevision = revision;
    outSample.evaluatedTimeSeconds = query.timeSeconds;
    if (query.preferredSurface.IsValid()) {
        for (const WaterSurfaceSnapshotEntry& surface : surfaces) {
            if (surface.object == query.preferredSurface && EvaluateCandidate(surface, query, revision, outSample)) {
                ResolveColumnDepth(query, provider, outSample);
                return true;
            }
        }
    }

    float nearestDistance = (std::numeric_limits<float>::max)();
    WaterSurfaceSample nearest{};
    for (const WaterSurfaceSnapshotEntry& surface : surfaces) {
        WaterSurfaceSample candidate{};
        if (!EvaluateCandidate(surface, query, revision, candidate)) {
            continue;
        }
        const float distance = std::abs(candidate.signedDistanceToSurface);
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
} // namespace

void WaterSurfaceQueryService::SetBathymetryProvider(const IWaterBathymetryProvider* provider) noexcept {
    bathymetryProvider_.store(provider, std::memory_order_release);
}

bool WaterSurfaceQueryService::SampleNearest(const WaterSurfaceQuery& query,
                                             WaterSurfaceSample& outSample) const noexcept {
    const auto snapshot = snapshotStore_.Current();
    const IWaterBathymetryProvider* provider = bathymetryProvider_.load(std::memory_order_acquire);
    const std::span<const WaterSurfaceSnapshotEntry> surfaces =
        snapshot != nullptr ? std::span<const WaterSurfaceSnapshotEntry>(snapshot->entries)
                            : std::span<const WaterSurfaceSnapshotEntry>{};
    return SampleNearestFromSnapshot(surfaces, snapshot != nullptr ? snapshot->revision : 0u, provider, query,
                                     outSample);
}

uint64_t WaterSurfaceQueryService::GetSnapshotRevision() const noexcept { return snapshotStore_.Revision(); }

std::size_t WaterSurfaceQueryService::GetSurfaceCount() const noexcept { return snapshotStore_.Count(); }

void WaterSurfaceQueryService::PublishSnapshot(std::vector<WaterSurfaceSnapshotEntry> surfaces) {
    const auto current = snapshotStore_.Current();
    if (current != nullptr && SnapshotEqual(current->entries, surfaces)) {
        return;
    }
    snapshotStore_.Publish(std::move(surfaces));
}

void WaterSurfaceQueryService::Clear() { snapshotStore_.Clear(); }

} // namespace HIKARI::WATER
