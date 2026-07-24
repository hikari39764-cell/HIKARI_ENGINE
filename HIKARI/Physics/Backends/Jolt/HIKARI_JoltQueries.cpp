#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackendInternal.h"

#include <algorithm>
#include <cmath>

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

#include "Physics/Backends/Jolt/HIKARI_JoltConversions.h"
#include "Physics/Backends/Jolt/HIKARI_JoltShapeFactory.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {
    namespace {

        constexpr float kDirectionEpsilon = 0.000001f;

        bool NormalizeDirection(
            const MATH::Vec3& direction,
            MATH::Vec3& outDirection) noexcept {
            const float length = MATH::Length(direction);
            if (!std::isfinite(length) || length <= kDirectionEpsilon) {
                return false;
            }
            outDirection = direction * (1.0f / length);
            return true;
        }

        JPH::RMat44 BuildTransform(const PhysicsPose& pose) noexcept {
            return JPH::RMat44::sRotationTranslation(
                ToJolt(pose.rotation),
                ToJoltPosition(pose.position));
        }

        MATH::Vec3 NormalFromPenetrationAxis(
            JPH::Vec3Arg penetrationAxis) noexcept {
            const float lengthSq = penetrationAxis.LengthSq();
            if (lengthSq <= kDirectionEpsilon) {
                return {};
            }
            return FromJolt(
                -penetrationAxis / std::sqrt(lengthSq));
        }

    } // namespace

    bool JoltPhysicsBackend::PassesQueryFilter(
        const BodyRecord& body,
        uint32_t shapeIndex,
        const PhysicsQueryFilter& filter) const noexcept {
        if (shapeIndex >= body.shapes.size()) {
            return false;
        }
        const PhysicsShapeDesc& shape = body.shapes[shapeIndex];
        if ((shape.filter.layer & filter.layerMask) == 0u) {
            return false;
        }
        if (!filter.includeTriggers && shape.isTrigger) {
            return false;
        }
        return !filter.ignoredObject.IsValid() ||
            body.object != filter.ignoredObject;
    }

    bool JoltPhysicsBackend::Raycast(
        const PhysicsRaycastQuery& query,
        PhysicsHit& outHit) const {
        if (!initialized_ || !physicsSystem_ ||
            !std::isfinite(query.maxDistance) ||
            query.maxDistance <= 0.0f) {
            return false;
        }

        MATH::Vec3 direction{};
        if (!NormalizeDirection(query.direction, direction)) {
            return false;
        }

        const JPH::RRayCast ray(
            ToJoltPosition(query.origin),
            ToJolt(direction * query.maxDistance));
        JPH::RayCastSettings raySettings{};
        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
        physicsSystem_->GetNarrowPhaseQuery().CastRay(
            ray,
            raySettings,
            collector);
        if (!collector.HadHit()) {
            return false;
        }
        collector.Sort();

        for (const JPH::RayCastResult& result : collector.mHits) {
            JPH::BodyLockRead bodyLock(
                physicsSystem_->GetBodyLockInterface(),
                result.mBodyID);
            if (!bodyLock.Succeeded()) {
                continue;
            }
            const JPH::Body& joltBody = bodyLock.GetBody();
            std::scoped_lock recordsLock(recordsMutex_);
            const PhysicsBodyHandle handle = PhysicsBodyHandle{
                static_cast<uint32_t>(joltBody.GetUserData() & 0xFFFFFFFFu),
                static_cast<uint32_t>(joltBody.GetUserData() >> 32u)
            };
            const BodyRecord* record = FindRecord(handle);
            if (!record) {
                continue;
            }
            const uint64_t shapeUserData = joltBody.GetShape()
                ->GetSubShapeUserData(result.mSubShapeID2);
            if (shapeUserData == 0 ||
                shapeUserData > record->shapes.size()) {
                continue;
            }
            const uint32_t shapeIndex = static_cast<uint32_t>(
                shapeUserData - 1u);
            if (!PassesQueryFilter(*record, shapeIndex, query.filter)) {
                continue;
            }

            const JPH::RVec3 position = ray.GetPointOnRay(result.mFraction);
            outHit.body = handle;
            outHit.object = record->object;
            outHit.shapeIndex = shapeIndex;
            outHit.shapeKey = record->shapes[shapeIndex].key;
            outHit.position = FromJoltPosition(position);
            outHit.normal = FromJolt(
                joltBody.GetWorldSpaceSurfaceNormal(
                    result.mSubShapeID2,
                    position));
            outHit.fraction = std::clamp(result.mFraction, 0.0f, 1.0f);
            outHit.distance = outHit.fraction * query.maxDistance;
            outHit.isTrigger = record->shapes[shapeIndex].isTrigger;
            return true;
        }
        return false;
    }

    bool JoltPhysicsBackend::ShapeCast(
        const PhysicsShapeCastQuery& query,
        PhysicsHit& outHit) const {
        if (!initialized_ || !physicsSystem_ ||
            !std::isfinite(query.maxDistance) ||
            query.maxDistance <= 0.0f) {
            return false;
        }
        MATH::Vec3 direction{};
        if (!NormalizeDirection(query.direction, direction)) {
            return false;
        }
        JPH::ShapeRefC shape = BuildQueryShape(query.shape);
        if (!shape) {
            return false;
        }

        const JPH::RShapeCast shapeCast =
            JPH::RShapeCast::sFromWorldTransform(
                shape.GetPtr(),
                JPH::Vec3::sReplicate(1.0f),
                BuildTransform(query.startPose),
                ToJolt(direction * query.maxDistance));
        JPH::ShapeCastSettings castSettings{};
        JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
        physicsSystem_->GetNarrowPhaseQuery().CastShape(
            shapeCast,
            castSettings,
            JPH::RVec3::sZero(),
            collector);
        if (!collector.HadHit()) {
            return false;
        }
        collector.Sort();

        for (const JPH::ShapeCastResult& result : collector.mHits) {
            JPH::BodyLockRead bodyLock(
                physicsSystem_->GetBodyLockInterface(),
                result.mBodyID2);
            if (!bodyLock.Succeeded()) {
                continue;
            }
            const JPH::Body& joltBody = bodyLock.GetBody();
            std::scoped_lock recordsLock(recordsMutex_);
            const PhysicsBodyHandle handle{
                static_cast<uint32_t>(joltBody.GetUserData() & 0xFFFFFFFFu),
                static_cast<uint32_t>(joltBody.GetUserData() >> 32u)
            };
            const BodyRecord* record = FindRecord(handle);
            if (!record) {
                continue;
            }
            const uint64_t shapeUserData = joltBody.GetShape()
                ->GetSubShapeUserData(result.mSubShapeID2);
            if (shapeUserData == 0 ||
                shapeUserData > record->shapes.size()) {
                continue;
            }
            const uint32_t shapeIndex = static_cast<uint32_t>(
                shapeUserData - 1u);
            if (!PassesQueryFilter(*record, shapeIndex, query.filter)) {
                continue;
            }

            outHit.body = handle;
            outHit.object = record->object;
            outHit.shapeIndex = shapeIndex;
            outHit.shapeKey = record->shapes[shapeIndex].key;
            outHit.position = FromJolt(result.mContactPointOn2);
            outHit.normal = NormalFromPenetrationAxis(
                result.mPenetrationAxis);
            outHit.fraction = std::clamp(result.mFraction, 0.0f, 1.0f);
            outHit.distance = outHit.fraction * query.maxDistance;
            outHit.isTrigger = record->shapes[shapeIndex].isTrigger;
            return true;
        }
        return false;
    }

    void JoltPhysicsBackend::Overlap(
        const PhysicsOverlapQuery& query,
        std::vector<PhysicsHit>& outHits) const {
        if (!initialized_ || !physicsSystem_) {
            return;
        }
        JPH::ShapeRefC shape = BuildQueryShape(query.shape);
        if (!shape) {
            return;
        }

        JPH::CollideShapeSettings collideSettings{};
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        physicsSystem_->GetNarrowPhaseQuery().CollideShape(
            shape.GetPtr(),
            JPH::Vec3::sReplicate(1.0f),
            BuildTransform(query.pose),
            collideSettings,
            JPH::RVec3::sZero(),
            collector);
        if (!collector.HadHit()) {
            return;
        }

        for (const JPH::CollideShapeResult& result : collector.mHits) {
            JPH::BodyLockRead bodyLock(
                physicsSystem_->GetBodyLockInterface(),
                result.mBodyID2);
            if (!bodyLock.Succeeded()) {
                continue;
            }
            const JPH::Body& joltBody = bodyLock.GetBody();
            std::scoped_lock recordsLock(recordsMutex_);
            const PhysicsBodyHandle handle{
                static_cast<uint32_t>(joltBody.GetUserData() & 0xFFFFFFFFu),
                static_cast<uint32_t>(joltBody.GetUserData() >> 32u)
            };
            const BodyRecord* record = FindRecord(handle);
            if (!record) {
                continue;
            }
            const uint64_t shapeUserData = joltBody.GetShape()
                ->GetSubShapeUserData(result.mSubShapeID2);
            if (shapeUserData == 0 ||
                shapeUserData > record->shapes.size()) {
                continue;
            }
            const uint32_t shapeIndex = static_cast<uint32_t>(
                shapeUserData - 1u);
            if (!PassesQueryFilter(*record, shapeIndex, query.filter)) {
                continue;
            }

            PhysicsHit hit{};
            hit.body = handle;
            hit.object = record->object;
            hit.shapeIndex = shapeIndex;
            hit.shapeKey = record->shapes[shapeIndex].key;
            hit.position = FromJolt(result.mContactPointOn2);
            hit.normal = NormalFromPenetrationAxis(
                result.mPenetrationAxis);
            hit.isTrigger = record->shapes[shapeIndex].isTrigger;
            outHits.push_back(hit);
        }
    }

} // namespace HIKARI::PHYSICS::JOLT_BACKEND
