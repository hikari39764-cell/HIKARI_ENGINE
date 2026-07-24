#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackendInternal.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <Jolt/Physics/Body/Body.h>

#include "Physics/Backends/Jolt/HIKARI_JoltConversions.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {
    namespace {
        constexpr JPH::ObjectLayer kCharacterLayer = 1;
        PhysicsCharacterGroundState FromJoltGroundState(
            JPH::CharacterBase::EGroundState state) noexcept {
            switch (state) {
            case JPH::CharacterBase::EGroundState::OnGround:
                return PhysicsCharacterGroundState::OnGround;
            case JPH::CharacterBase::EGroundState::OnSteepGround:
                return PhysicsCharacterGroundState::OnSteepGround;
            case JPH::CharacterBase::EGroundState::NotSupported:
                return PhysicsCharacterGroundState::NotSupported;
            case JPH::CharacterBase::EGroundState::InAir:
            default:
                return PhysicsCharacterGroundState::InAir;
            }
        }
    }

    class JoltPhysicsBackend::CharacterBodyFilter final
        : public JPH::BodyFilter {
    public:
        CharacterBodyFilter(
            const JoltPhysicsBackend& owner,
            PhysicsCharacterDesc desc) noexcept
            : owner_(owner),
              desc_(std::move(desc)) {
        }

        bool ShouldCollide(const JPH::BodyID&) const override {
            return true;
        }

        bool ShouldCollideLocked(const JPH::Body& body) const override {
            std::scoped_lock lock(owner_.recordsMutex_);
            return owner_.CharacterFilterAcceptsBody(desc_, body);
        }

    private:
        const JoltPhysicsBackend& owner_;
        PhysicsCharacterDesc desc_{};
    };

    bool JoltPhysicsBackend::RefreshCharacterGroundVelocity(
        PhysicsCharacterHandle handle) {
        std::scoped_lock lock(recordsMutex_);
        CharacterRecord* record = FindCharacter(handle);
        if (record == nullptr || !record->character) {
            return false;
        }
        record->character->UpdateGroundVelocity();
        return true;
    }

    bool JoltPhysicsBackend::RefreshCharacterContacts(
        PhysicsCharacterHandle handle) {
        PhysicsCharacterDesc desc{};
        JPH::Ref<JPH::CharacterVirtual> character{};
        {
            std::scoped_lock lock(recordsMutex_);
            CharacterRecord* record = FindCharacter(handle);
            if (record == nullptr || !record->character) {
                return false;
            }
            desc = record->desc;
            character = record->character;
        }
        CharacterBodyFilter bodyFilter(*this, desc);
        // ShapeFilter is evaluated at every level of a compound hierarchy.
        // Authored Collider metadata only exists on the final leaf, so trying
        // to resolve it here rejects intermediate nodes before Jolt reaches
        // the actual collision shape. BodyFilter provides the coarse reject;
        // CharacterContactListener validates the final leaf and its mask.
        JPH::ShapeFilter hierarchyFilter{};
        character->RefreshContacts(
            physicsSystem_->GetDefaultBroadPhaseLayerFilter(
                kCharacterLayer),
            physicsSystem_->GetDefaultLayerFilter(kCharacterLayer),
            bodyFilter,
            hierarchyFilter,
            *tempAllocator_);
        return true;
    }

    bool JoltPhysicsBackend::StepCharacter(
        PhysicsCharacterHandle handle,
        float fixedDeltaSeconds,
        const PhysicsCharacterStepSettings& stepSettings) {
        if (!initialized_ || !physicsSystem_ || !tempAllocator_ ||
            !std::isfinite(fixedDeltaSeconds) ||
            fixedDeltaSeconds <= 0.0f) {
            return false;
        }

        PhysicsCharacterDesc desc{};
        JPH::Ref<JPH::CharacterVirtual> character{};
        {
            std::scoped_lock lock(recordsMutex_);
            CharacterRecord* record = FindCharacter(handle);
            if (record == nullptr || !record->character) {
                return false;
            }
            desc = record->desc;
            character = record->character;
        }

        JPH::CharacterVirtual::ExtendedUpdateSettings extended{};
        extended.mWalkStairsStepUp = JPH::Vec3(
            0.0f,
            (std::max)(stepSettings.stepUpHeight, 0.0f),
            0.0f);
        extended.mStickToFloorStepDown = JPH::Vec3(
            0.0f,
            -(std::max)(stepSettings.stickToFloorDistance, 0.0f),
            0.0f);
        extended.mWalkStairsStepForwardTest = (std::max)(
            stepSettings.stepForwardTestDistance,
            0.0f);

        CharacterBodyFilter bodyFilter(*this, desc);
        JPH::ShapeFilter hierarchyFilter{};
        character->ExtendedUpdate(
            fixedDeltaSeconds,
            ToJolt(stepSettings.gravity),
            extended,
            physicsSystem_->GetDefaultBroadPhaseLayerFilter(
                kCharacterLayer),
            physicsSystem_->GetDefaultLayerFilter(kCharacterLayer),
            bodyFilter,
            hierarchyFilter,
            *tempAllocator_);
        return true;
    }

    void JoltPhysicsBackend::FillCharacterState(
        const CharacterRecord& record,
        PhysicsCharacterState& outState) const noexcept {
        outState = {};
        outState.pose.position = FromJoltPosition(
            record.character->GetPosition());
        outState.pose.rotation = FromJolt(
            record.character->GetRotation());
        outState.linearVelocity = FromJolt(
            record.character->GetLinearVelocity());
        outState.groundVelocity = FromJolt(
            record.character->GetGroundVelocity());
        outState.groundPosition = FromJoltPosition(
            record.character->GetGroundPosition());
        outState.groundNormal = FromJolt(
            record.character->GetGroundNormal());
        outState.groundState = FromJoltGroundState(
            record.character->GetGroundState());
        outState.maxHitsExceeded =
            record.character->GetMaxHitsExceeded();

        const JPH::BodyID groundBody =
            record.character->GetGroundBodyID();
        if (!groundBody.IsInvalid()) {
            if (const BodyRecord* ground =
                    FindRecordFromBodyId(groundBody)) {
                outState.groundObject = ground->object;
            }
        }

        const JPH::Vec3 up = record.character->GetUp();
        const float groundThreshold =
            record.character->GetCosMaxSlopeAngle();
        for (const JPH::CharacterContact& contact :
                record.character->GetActiveContacts()) {
            if (!contact.mHadCollision || contact.mWasDiscarded ||
                contact.mIsSensorB) {
                continue;
            }
            const float upDot = contact.mSurfaceNormal.Dot(up);
            outState.hitCeiling = outState.hitCeiling ||
                upDot < -0.25f;
            outState.hitWall = outState.hitWall ||
                (upDot >= -0.25f && upDot < groundThreshold);
        }
    }

    bool JoltPhysicsBackend::TryGetCharacterState(
        PhysicsCharacterHandle handle,
        PhysicsCharacterState& outState) const {
        std::scoped_lock lock(recordsMutex_);
        const CharacterRecord* record = FindCharacter(handle);
        if (record == nullptr || !record->character) {
            return false;
        }
        FillCharacterState(*record, outState);
        return true;
    }

} // namespace HIKARI::PHYSICS::JOLT_BACKEND
