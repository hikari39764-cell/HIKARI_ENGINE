#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackendInternal.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <Jolt/Physics/Collision/CollideShape.h>

#include "Physics/Backends/Jolt/HIKARI_JoltConversions.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {
    namespace {

        PhysicsBodyHandle HandleFromUserData(uint64_t value) noexcept {
            PhysicsBodyHandle result{};
            result.slot = static_cast<uint32_t>(value & 0xFFFFFFFFu);
            result.generation = static_cast<uint32_t>(value >> 32u);
            return result;
        }

        float CombinedFriction(float a, float b) noexcept {
            return std::sqrt(
                (std::max)(0.0f, a) * (std::max)(0.0f, b));
        }

        float CombinedRestitution(float a, float b) noexcept {
            return (std::max)(
                std::clamp(a, 0.0f, 1.0f),
                std::clamp(b, 0.0f, 1.0f));
        }

    } // namespace

    size_t JoltPhysicsBackend::ContactKeyHash::operator()(
        const ContactKey& key) const noexcept {
        size_t result = key.body1;
        result = result * 16777619u ^ key.subShape1;
        result = result * 16777619u ^ key.body2;
        result = result * 16777619u ^ key.subShape2;
        return result;
    }

    JoltPhysicsBackend::ContactListener::ContactListener(
        JoltPhysicsBackend& owner) noexcept
        : owner_(owner) {
    }

    JPH::ValidateResult
        JoltPhysicsBackend::ContactListener::OnContactValidate(
            const JPH::Body& body1,
            const JPH::Body& body2,
            JPH::RVec3Arg,
            const JPH::CollideShapeResult& result) {
        return owner_.AcceptsContact(
            body1,
            result.mSubShapeID1,
            body2,
            result.mSubShapeID2)
            ? JPH::ValidateResult::AcceptContact
            : JPH::ValidateResult::RejectContact;
    }

    void JoltPhysicsBackend::ContactListener::OnContactAdded(
        const JPH::Body& body1,
        const JPH::Body& body2,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings& settings) {
        owner_.ApplyContactSettings(
            body1,
            manifold.mSubShapeID1,
            body2,
            manifold.mSubShapeID2,
            settings);
        owner_.QueueContact(
            PhysicsContactPhase::Begin,
            body1,
            body2,
            manifold);
    }

    void JoltPhysicsBackend::ContactListener::OnContactPersisted(
        const JPH::Body& body1,
        const JPH::Body& body2,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings& settings) {
        owner_.ApplyContactSettings(
            body1,
            manifold.mSubShapeID1,
            body2,
            manifold.mSubShapeID2,
            settings);
        owner_.QueueContact(
            PhysicsContactPhase::Persist,
            body1,
            body2,
            manifold);
    }

    void JoltPhysicsBackend::ContactListener::OnContactRemoved(
        const JPH::SubShapeIDPair& pair) {
        owner_.QueueContactEnd(pair);
    }

    void JoltPhysicsBackend::DrainContactEvents(
        std::vector<PhysicsContactEvent>& outEvents) {
        std::scoped_lock lock(contactMutex_);
        if (pendingContactEvents_.empty()) {
            return;
        }
        outEvents.insert(
            outEvents.end(),
            std::make_move_iterator(pendingContactEvents_.begin()),
            std::make_move_iterator(pendingContactEvents_.end()));
        pendingContactEvents_.clear();
    }

    const PhysicsShapeDesc* JoltPhysicsBackend::ResolveShape(
        const JPH::Body& body,
        const JPH::SubShapeID& subShapeId,
        uint32_t& outIndex) const noexcept {
        const BodyRecord* record = FindRecordFromUserData(body.GetUserData());
        return record ? ResolveShape(*record, subShapeId, outIndex) : nullptr;
    }

    const PhysicsShapeDesc* JoltPhysicsBackend::ResolveShape(
        const BodyRecord& body,
        const JPH::SubShapeID& subShapeId,
        uint32_t& outIndex) const noexcept {
        const uint64_t userData = physicsSystem_->GetBodyInterfaceNoLock()
            .GetShape(body.bodyId)->GetSubShapeUserData(subShapeId);
        if (userData == 0 || userData > body.shapes.size()) {
            return nullptr;
        }
        outIndex = static_cast<uint32_t>(userData - 1u);
        return &body.shapes[outIndex];
    }

    bool JoltPhysicsBackend::AcceptsContact(
        const JPH::Body& body1,
        const JPH::SubShapeID& subShape1,
        const JPH::Body& body2,
        const JPH::SubShapeID& subShape2) const noexcept {
        std::scoped_lock lock(recordsMutex_);
        uint32_t index1 = 0;
        uint32_t index2 = 0;
        const PhysicsShapeDesc* shape1 = ResolveShape(
            body1, subShape1, index1);
        const PhysicsShapeDesc* shape2 = ResolveShape(
            body2, subShape2, index2);
        if (!shape1 || !shape2) {
            return false;
        }
        return (shape1->filter.layer & shape2->filter.mask) != 0u &&
            (shape2->filter.layer & shape1->filter.mask) != 0u;
    }

    void JoltPhysicsBackend::ApplyContactSettings(
        const JPH::Body& body1,
        const JPH::SubShapeID& subShape1,
        const JPH::Body& body2,
        const JPH::SubShapeID& subShape2,
        JPH::ContactSettings& settings) const {
        std::scoped_lock lock(recordsMutex_);
        uint32_t index1 = 0;
        uint32_t index2 = 0;
        const PhysicsShapeDesc* shape1 = ResolveShape(
            body1, subShape1, index1);
        const PhysicsShapeDesc* shape2 = ResolveShape(
            body2, subShape2, index2);
        if (!shape1 || !shape2) {
            return;
        }
        settings.mCombinedFriction = CombinedFriction(
            shape1->material.friction,
            shape2->material.friction);
        settings.mCombinedRestitution = CombinedRestitution(
            shape1->material.restitution,
            shape2->material.restitution);
        settings.mIsSensor = shape1->isTrigger || shape2->isTrigger;
    }

    void JoltPhysicsBackend::QueueContact(
        PhysicsContactPhase phase,
        const JPH::Body& body1,
        const JPH::Body& body2,
        const JPH::ContactManifold& manifold) {
        PhysicsContactEvent event{};
        ContactKey key{};
        {
            std::scoped_lock lock(recordsMutex_);
            const PhysicsBodyHandle handle1 = HandleFromUserData(
                body1.GetUserData());
            const PhysicsBodyHandle handle2 = HandleFromUserData(
                body2.GetUserData());
            const BodyRecord* record1 = FindRecord(handle1);
            const BodyRecord* record2 = FindRecord(handle2);
            uint32_t shapeIndex1 = 0;
            uint32_t shapeIndex2 = 0;
            const PhysicsShapeDesc* shape1 = record1
                ? ResolveShape(*record1, manifold.mSubShapeID1, shapeIndex1)
                : nullptr;
            const PhysicsShapeDesc* shape2 = record2
                ? ResolveShape(*record2, manifold.mSubShapeID2, shapeIndex2)
                : nullptr;
            if (!record1 || !record2 || !shape1 || !shape2) {
                return;
            }

            event.phase = phase;
            event.bodyA = handle1;
            event.bodyB = handle2;
            event.objectA = record1->object;
            event.objectB = record2->object;
            event.shapeIndexA = shapeIndex1;
            event.shapeIndexB = shapeIndex2;
            event.shapeKeyA = shape1->key;
            event.shapeKeyB = shape2->key;
            if (!manifold.mRelativeContactPointsOn1.empty()) {
                event.position = FromJoltPosition(
                    manifold.GetWorldSpaceContactPointOn1(0));
            } else {
                event.position = FromJoltPosition(manifold.mBaseOffset);
            }
            event.normal = FromJolt(manifold.mWorldSpaceNormal);
            event.penetrationDepth = manifold.mPenetrationDepth;
            event.isTrigger = shape1->isTrigger || shape2->isTrigger;

            key.body1 = body1.GetID().GetIndexAndSequenceNumber();
            key.subShape1 = manifold.mSubShapeID1.GetValue();
            key.body2 = body2.GetID().GetIndexAndSequenceNumber();
            key.subShape2 = manifold.mSubShapeID2.GetValue();
        }

        std::scoped_lock lock(contactMutex_);
        pendingContactEvents_.push_back(event);
        activeContacts_[key] = event;
    }

    void JoltPhysicsBackend::QueueContactEnd(
        const JPH::SubShapeIDPair& pair) {
        const ContactKey key{
            pair.GetBody1ID().GetIndexAndSequenceNumber(),
            pair.GetSubShapeID1().GetValue(),
            pair.GetBody2ID().GetIndexAndSequenceNumber(),
            pair.GetSubShapeID2().GetValue(),
        };
        std::scoped_lock lock(contactMutex_);
        const auto found = activeContacts_.find(key);
        if (found == activeContacts_.end()) {
            return;
        }
        PhysicsContactEvent event = found->second;
        event.phase = PhysicsContactPhase::End;
        event.penetrationDepth = 0.0f;
        event.impulse = 0.0f;
        pendingContactEvents_.push_back(std::move(event));
        activeContacts_.erase(found);
    }

} // namespace HIKARI::PHYSICS::JOLT_BACKEND
