#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS {

    class IPhysicsWorldBackend {
    public:
        virtual ~IPhysicsWorldBackend() = default;

        virtual std::string_view GetBackendName() const noexcept = 0;
        virtual PhysicsBackendCapabilities GetCapabilities()
            const noexcept = 0;

        virtual bool Initialize(
            const PhysicsWorldSettings& settings) = 0;
        virtual void Shutdown() noexcept = 0;

        virtual PhysicsBodyCreateResult CreateBody(
            const PhysicsBodyCreateInfo& createInfo) = 0;
        virtual bool DestroyBody(PhysicsBodyHandle body) = 0;
        virtual bool SetBodyPose(
            PhysicsBodyHandle body,
            const PhysicsPose& pose,
            bool activate) = 0;
        virtual bool SetKinematicTarget(
            PhysicsBodyHandle body,
            const PhysicsPose& pose) = 0;
        virtual bool SetBodyVelocity(
            PhysicsBodyHandle body,
            const MATH::Vec3& linearVelocity,
            const MATH::Vec3& angularVelocity) = 0;
        virtual bool TryGetBodyState(
            PhysicsBodyHandle body,
            PhysicsBodyState& outState) const = 0;

        virtual PhysicsCharacterCreateResult CreateCharacter(
            const PhysicsCharacterCreateInfo& createInfo) = 0;
        virtual bool DestroyCharacter(
            PhysicsCharacterHandle character) = 0;
        virtual bool SetCharacterPose(
            PhysicsCharacterHandle character,
            const PhysicsPose& pose) = 0;
        virtual bool SetCharacterVelocity(
            PhysicsCharacterHandle character,
            const MATH::Vec3& linearVelocity) = 0;
        virtual bool RefreshCharacterGroundVelocity(
            PhysicsCharacterHandle character) = 0;
        virtual bool RefreshCharacterContacts(
            PhysicsCharacterHandle character) = 0;
        virtual bool StepCharacter(
            PhysicsCharacterHandle character,
            float fixedDeltaSeconds,
            const PhysicsCharacterStepSettings& settings) = 0;
        virtual bool TryGetCharacterState(
            PhysicsCharacterHandle character,
            PhysicsCharacterState& outState) const = 0;

        virtual PhysicsStepResult Step(float fixedDeltaSeconds) = 0;
        virtual PhysicsBackendStatistics GetStatistics()
            const noexcept = 0;
        virtual void DrainContactEvents(
            std::vector<PhysicsContactEvent>& outEvents) = 0;

        virtual bool Raycast(
            const PhysicsRaycastQuery& query,
            PhysicsHit& outHit) const = 0;
        virtual bool ShapeCast(
            const PhysicsShapeCastQuery& query,
            PhysicsHit& outHit) const = 0;
        virtual void Overlap(
            const PhysicsOverlapQuery& query,
            std::vector<PhysicsHit>& outHits) const = 0;
    };

} // namespace HIKARI::PHYSICS
