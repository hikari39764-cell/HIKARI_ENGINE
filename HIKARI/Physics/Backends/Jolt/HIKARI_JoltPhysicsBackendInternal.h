#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackend.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {

    class JoltPhysicsBackend final : public IPhysicsWorldBackend {
    public:
        JoltPhysicsBackend();
        ~JoltPhysicsBackend() override;

        std::string_view GetBackendName() const noexcept override;
        PhysicsBackendCapabilities GetCapabilities() const noexcept override;
        bool Initialize(const PhysicsWorldSettings& settings) override;
        void Shutdown() noexcept override;

        PhysicsBodyCreateResult CreateBody(
            const PhysicsBodyCreateInfo& createInfo) override;
        bool DestroyBody(PhysicsBodyHandle body) override;
        bool SetBodyPose(
            PhysicsBodyHandle body,
            const PhysicsPose& pose,
            bool activate) override;
        bool SetKinematicTarget(
            PhysicsBodyHandle body,
            const PhysicsPose& pose) override;
        bool SetBodyVelocity(
            PhysicsBodyHandle body,
            const MATH::Vec3& linearVelocity,
            const MATH::Vec3& angularVelocity) override;
        bool TryGetBodyState(
            PhysicsBodyHandle body,
            PhysicsBodyState& outState) const override;

        PhysicsCharacterCreateResult CreateCharacter(
            const PhysicsCharacterCreateInfo& createInfo) override;
        bool DestroyCharacter(
            PhysicsCharacterHandle character) override;
        bool SetCharacterPose(
            PhysicsCharacterHandle character,
            const PhysicsPose& pose) override;
        bool SetCharacterVelocity(
            PhysicsCharacterHandle character,
            const MATH::Vec3& linearVelocity) override;
        bool RefreshCharacterGroundVelocity(
            PhysicsCharacterHandle character) override;
        bool RefreshCharacterContacts(
            PhysicsCharacterHandle character) override;
        bool StepCharacter(
            PhysicsCharacterHandle character,
            float fixedDeltaSeconds,
            const PhysicsCharacterStepSettings& settings) override;
        bool TryGetCharacterState(
            PhysicsCharacterHandle character,
            PhysicsCharacterState& outState) const override;

        PhysicsStepResult Step(float fixedDeltaSeconds) override;
        PhysicsBackendStatistics GetStatistics()
            const noexcept override;
        void DrainContactEvents(
            std::vector<PhysicsContactEvent>& outEvents) override;

        bool Raycast(
            const PhysicsRaycastQuery& query,
            PhysicsHit& outHit) const override;
        bool ShapeCast(
            const PhysicsShapeCastQuery& query,
            PhysicsHit& outHit) const override;
        void Overlap(
            const PhysicsOverlapQuery& query,
            std::vector<PhysicsHit>& outHits) const override;

    private:
        struct BodyRecord {
            uint32_t generation = 0;
            bool occupied = false;
            JPH::BodyID bodyId{};
            RuntimeObjectHandle object{};
            PhysicsMotionType motionType = PhysicsMotionType::Static;
            std::vector<PhysicsShapeDesc> shapes{};
            PhysicsPose pendingKinematicTarget{};
            bool hasPendingKinematicTarget = false;
        };

        struct CharacterRecord {
            uint32_t generation = 0;
            bool occupied = false;
            RuntimeObjectHandle object{};
            PhysicsCharacterDesc desc{};
            JPH::Ref<JPH::CharacterVirtual> character{};
        };

        struct ContactKey {
            uint32_t body1 = 0;
            uint32_t subShape1 = 0;
            uint32_t body2 = 0;
            uint32_t subShape2 = 0;

            bool operator==(const ContactKey&) const noexcept = default;
        };

        struct ContactKeyHash {
            size_t operator()(const ContactKey& key) const noexcept;
        };

        class ContactListener final : public JPH::ContactListener {
        public:
            explicit ContactListener(JoltPhysicsBackend& owner) noexcept;

            JPH::ValidateResult OnContactValidate(
                const JPH::Body& body1,
                const JPH::Body& body2,
                JPH::RVec3Arg baseOffset,
                const JPH::CollideShapeResult& result) override;
            void OnContactAdded(
                const JPH::Body& body1,
                const JPH::Body& body2,
                const JPH::ContactManifold& manifold,
                JPH::ContactSettings& settings) override;
            void OnContactPersisted(
                const JPH::Body& body1,
                const JPH::Body& body2,
                const JPH::ContactManifold& manifold,
                JPH::ContactSettings& settings) override;
            void OnContactRemoved(
                const JPH::SubShapeIDPair& pair) override;

        private:
            JoltPhysicsBackend& owner_;
        };

        class CharacterContactListener;
        class CharacterBodyFilter;

        PhysicsBodyHandle AllocateHandle();
        PhysicsCharacterHandle AllocateCharacterHandle();
        BodyRecord* FindRecord(PhysicsBodyHandle body) noexcept;
        const BodyRecord* FindRecord(PhysicsBodyHandle body) const noexcept;
        const BodyRecord* FindRecordFromUserData(uint64_t value) const noexcept;
        CharacterRecord* FindCharacter(
            PhysicsCharacterHandle character) noexcept;
        const CharacterRecord* FindCharacter(
            PhysicsCharacterHandle character) const noexcept;
        const CharacterRecord* FindCharacter(
            const JPH::CharacterVirtual* character) const noexcept;
        const BodyRecord* FindRecordFromBodyId(
            const JPH::BodyID& bodyId) const noexcept;
        const PhysicsShapeDesc* ResolveShape(
            const JPH::Body& body,
            const JPH::SubShapeID& subShapeId,
            uint32_t& outIndex) const noexcept;
        const PhysicsShapeDesc* ResolveShape(
            const BodyRecord& body,
            const JPH::SubShapeID& subShapeId,
            uint32_t& outIndex) const noexcept;
        bool PassesQueryFilter(
            const BodyRecord& body,
            uint32_t shapeIndex,
            const PhysicsQueryFilter& filter) const noexcept;
        bool CharacterFilterAcceptsBody(
            const PhysicsCharacterDesc& character,
            const JPH::Body& body) const noexcept;
        bool CharacterFilterAcceptsShape(
            const PhysicsCharacterDesc& character,
            const JPH::BodyID& bodyId,
            const JPH::SubShapeID& subShapeId) const noexcept;
        void FillCharacterState(
            const CharacterRecord& record,
            PhysicsCharacterState& outState) const noexcept;

        void ApplyContactSettings(
            const JPH::Body& body1,
            const JPH::SubShapeID& subShape1,
            const JPH::Body& body2,
            const JPH::SubShapeID& subShape2,
            JPH::ContactSettings& settings) const;
        bool AcceptsContact(
            const JPH::Body& body1,
            const JPH::SubShapeID& subShape1,
            const JPH::Body& body2,
            const JPH::SubShapeID& subShape2) const noexcept;
        void QueueContact(
            PhysicsContactPhase phase,
            const JPH::Body& body1,
            const JPH::Body& body2,
            const JPH::ContactManifold& manifold);
        void QueueContactEnd(const JPH::SubShapeIDPair& pair);

        bool initialized_ = false;
        PhysicsWorldSettings settings_{};
        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_{};
        std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_{};
        std::unique_ptr<JPH::PhysicsSystem> physicsSystem_{};
        std::unique_ptr<ContactListener> contactListener_{};

        mutable std::mutex recordsMutex_{};
        std::vector<BodyRecord> bodyRecords_{};
        std::vector<uint32_t> freeSlots_{};
        std::unordered_map<uint32_t, PhysicsBodyHandle> bodyIds_{};

        std::vector<CharacterRecord> characterRecords_{};
        std::vector<uint32_t> freeCharacterSlots_{};
        std::unique_ptr<JPH::CharacterContactListener>
            characterContactListener_{};

        mutable std::mutex contactMutex_{};
        std::vector<PhysicsContactEvent> pendingContactEvents_{};
        std::unordered_map<
            ContactKey,
            PhysicsContactEvent,
            ContactKeyHash> activeContacts_{};
    };

} // namespace HIKARI::PHYSICS::JOLT_BACKEND
