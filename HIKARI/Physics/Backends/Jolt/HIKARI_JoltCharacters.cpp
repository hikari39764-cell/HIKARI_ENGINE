#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackendInternal.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include <Jolt/Physics/Body/Body.h>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Physics/Backends/Jolt/HIKARI_JoltConversions.h"
#include "Physics/Backends/Jolt/HIKARI_JoltShapeFactory.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {
    namespace {
        constexpr JPH::ObjectLayer kCharacterLayer = 1;
        constexpr uint64_t kCharacterUserDataTag = uint64_t{ 1 } << 63u;
		// 物理キャラクターのユーザーデータをエンコードするためのタグ
        uint32_t BodyIdKey(const JPH::BodyID& bodyId) noexcept {
            return bodyId.GetIndexAndSequenceNumber();
        }
		// 物理キャラクターのユーザーデータをエンコードする関数
        uint64_t EncodeCharacterUserData(
            PhysicsCharacterHandle handle) noexcept {
            return kCharacterUserDataTag |
                (static_cast<uint64_t>(handle.generation & 0x7FFFFFFFu)
                    << 32u) |
                static_cast<uint64_t>(handle.slot);
        }
		// 物理キャラクターのユーザーデータをデコードする関数
        PhysicsCharacterHandle DecodeCharacterUserData(
            uint64_t value) noexcept {
            if ((value & kCharacterUserDataTag) == 0u) {
                return {};
            }
            PhysicsCharacterHandle result{};
            result.slot = static_cast<uint32_t>(value & 0xFFFFFFFFu);
            result.generation = static_cast<uint32_t>(
                (value >> 32u) & 0x7FFFFFFFu);
            return result;
        }
		// 物理キャラクターの作成情報が有限であるかどうかをチェックする関数
        bool IsFinite(const PhysicsCharacterCreateInfo& info) noexcept {
            const auto finite = [](float value) {
                return std::isfinite(value);
            };
            const PhysicsCharacterDesc& desc = info.character;
            return finite(desc.mass) &&
                finite(desc.maxSlopeAngleRadians) &&
                finite(desc.characterPadding) &&
                finite(desc.predictiveContactDistance) &&
                finite(desc.penetrationRecoverySpeed) &&
                MATH::IsFinite(info.initialPose.position) &&
                MATH::IsFinite(info.initialLinearVelocity);
        }

    }

    class JoltPhysicsBackend::CharacterContactListener final
        : public JPH::CharacterContactListener {
    public:
        explicit CharacterContactListener(
            JoltPhysicsBackend& owner) noexcept
            : owner_(owner) {
        }

        bool OnContactValidate(
            const JPH::CharacterVirtual* character,
            const JPH::CharacterContact& contact) override {
            std::scoped_lock lock(owner_.recordsMutex_);
            const CharacterRecord* self = owner_.FindCharacter(character);
            return self != nullptr && !contact.mIsSensorB &&
                owner_.CharacterFilterAcceptsShape(
                    self->desc,
                    contact.mBodyB,
                    contact.mSubShapeIDB);
        }

        void OnContactAdded(
            const JPH::CharacterVirtual* character,
            const JPH::CharacterContact&,
            JPH::CharacterContactSettings& settings) override {
            ApplySettings(character, settings);
        }

        void OnContactPersisted(
            const JPH::CharacterVirtual* character,
            const JPH::CharacterContact&,
            JPH::CharacterContactSettings& settings) override {
            ApplySettings(character, settings);
        }

        bool OnCharacterContactValidate(
            const JPH::CharacterVirtual*,
            const JPH::CharacterContact&) override {
            return false;
        }

        void OnCharacterContactAdded(
            const JPH::CharacterVirtual* character,
            const JPH::CharacterContact&,
            JPH::CharacterContactSettings& settings) override {
            ApplySettings(character, settings);
        }

        void OnCharacterContactPersisted(
            const JPH::CharacterVirtual* character,
            const JPH::CharacterContact&,
            JPH::CharacterContactSettings& settings) override {
            ApplySettings(character, settings);
        }

    private:
        void ApplySettings(
            const JPH::CharacterVirtual*,
            JPH::CharacterContactSettings& settings) {
            settings.mCanPushCharacter = false;
            settings.mCanReceiveImpulses = false;
        }

        JoltPhysicsBackend& owner_;
    };

    PhysicsCharacterHandle JoltPhysicsBackend::AllocateCharacterHandle() {
        PhysicsCharacterHandle handle{};
        if (!freeCharacterSlots_.empty()) {
            handle.slot = freeCharacterSlots_.back();
            freeCharacterSlots_.pop_back();
            CharacterRecord& record = characterRecords_[handle.slot];
            record.generation = (record.generation + 1u) & 0x7FFFFFFFu;
            if (record.generation == 0u) {
                record.generation = 1u;
            }
            handle.generation = record.generation;
            return handle;
        }

        handle.slot = static_cast<uint32_t>(characterRecords_.size());
        handle.generation = 1u;
        characterRecords_.push_back(CharacterRecord{});
        characterRecords_.back().generation = handle.generation;
        return handle;
    }

    JoltPhysicsBackend::CharacterRecord* JoltPhysicsBackend::FindCharacter(
        PhysicsCharacterHandle handle) noexcept {
        if (!handle.IsValid() || handle.slot >= characterRecords_.size()) {
            return nullptr;
        }
        CharacterRecord& record = characterRecords_[handle.slot];
        return record.occupied && record.generation == handle.generation
            ? &record
            : nullptr;
    }

    const JoltPhysicsBackend::CharacterRecord*
        JoltPhysicsBackend::FindCharacter(
            PhysicsCharacterHandle handle) const noexcept {
        return const_cast<JoltPhysicsBackend*>(this)->FindCharacter(handle);
    }

    const JoltPhysicsBackend::CharacterRecord*
        JoltPhysicsBackend::FindCharacter(
            const JPH::CharacterVirtual* character) const noexcept {
        if (character == nullptr) {
            return nullptr;
        }
        return FindCharacter(DecodeCharacterUserData(
            character->GetUserData()));
    }

    const JoltPhysicsBackend::BodyRecord*
        JoltPhysicsBackend::FindRecordFromBodyId(
            const JPH::BodyID& bodyId) const noexcept {
        const auto found = bodyIds_.find(BodyIdKey(bodyId));
        return found == bodyIds_.end()
            ? nullptr
            : FindRecord(found->second);
    }

    bool JoltPhysicsBackend::CharacterFilterAcceptsBody(
        const PhysicsCharacterDesc& character,
        const JPH::Body& body) const noexcept {
        const BodyRecord* record = FindRecordFromBodyId(body.GetID());
        if (record == nullptr || record->object == character.object) {
            return false;
        }
        return std::any_of(
            record->shapes.begin(),
            record->shapes.end(),
            [&character](const PhysicsShapeDesc& shape) {
                return !shape.isTrigger &&
                    (character.filter.mask & shape.filter.layer) != 0u &&
                    (shape.filter.mask & character.filter.layer) != 0u;
            });
    }

    bool JoltPhysicsBackend::CharacterFilterAcceptsShape(
        const PhysicsCharacterDesc& character,
        const JPH::BodyID& bodyId,
        const JPH::SubShapeID& subShapeId) const noexcept {
        const BodyRecord* record = FindRecordFromBodyId(bodyId);
        if (record == nullptr || record->object == character.object) {
            return false;
        }
        uint32_t shapeIndex = 0u;
        const PhysicsShapeDesc* shape = ResolveShape(
            *record,
            subShapeId,
            shapeIndex);
        return shape != nullptr && !shape->isTrigger &&
            (character.filter.mask & shape->filter.layer) != 0u &&
            (shape->filter.mask & character.filter.layer) != 0u;
    }

    PhysicsCharacterCreateResult JoltPhysicsBackend::CreateCharacter(
        const PhysicsCharacterCreateInfo& createInfo) {
        PhysicsCharacterCreateResult result{};
        if (!initialized_ || !physicsSystem_ || !tempAllocator_) {
            result.error = PhysicsErrorCode::WorldUnavailable;
            result.message = "Jolt physics world is not initialized";
            result.recoverable = true;
            return result;
        }
        if (!IsFinite(createInfo) ||
            createInfo.character.shapes.empty() ||
            createInfo.character.filter.layer == 0u) {
            result.error = PhysicsErrorCode::InvalidBodyDefinition;
            result.message = "character definition is invalid";
            return result;
        }

        std::string shapeError{};
        const bool hasUnsupportedShape = std::any_of(
            createInfo.character.shapes.begin(),
            createInfo.character.shapes.end(),
            [](const PhysicsShapeDesc& candidate) {
                return candidate.isTrigger ||
                    candidate.type == PhysicsShapeType::TriangleMesh;
            });
        if (hasUnsupportedShape) {
            result.error = PhysicsErrorCode::UnsupportedMotionShape;
            result.message =
                "kinematic motion requires solid non-mesh collider shapes";
            return result;
        }

        JPH::ShapeRefC shape = BuildCompoundShape(
            createInfo.character.shapes,
            &shapeError);
        if (!shape) {
            result.error = PhysicsErrorCode::BackendShapeCreationFailed;
            result.message = shapeError.empty()
                ? "Jolt failed to create character shape"
                : "Jolt character shape creation failed: " + shapeError;
            return result;
        }

        PhysicsCharacterHandle handle{};
        {
            std::scoped_lock lock(recordsMutex_);
            handle = AllocateCharacterHandle();
        }

        JPH::CharacterVirtualSettings settings{};
        settings.mShape = shape;
        const JPH::AABox localBounds = shape->GetLocalBounds();
        const JPH::Vec3 extents = localBounds.GetExtent();
        const float shapeHeight = (std::max)(
            localBounds.mMax.GetY() - localBounds.mMin.GetY(),
            0.02f);
        const float supportHeight = (std::max)(
            0.01f,
            (std::min)(
                shapeHeight * 0.25f,
                (std::min)(extents.GetX(), extents.GetZ()) * 0.5f));
        // CharacterVirtual positions use the authored shape origin. A Jolt
        // compound's local bounds are center-of-mass relative, so folding
        // localBounds.mMin into this plane shifts the supporting volume below
        // the character's feet and reports every floor as NotSupported.
        settings.mSupportingVolume = JPH::Plane(
            JPH::Vec3::sAxisY(),
            -supportHeight);
        settings.mMaxSlopeAngle = std::clamp(
            createInfo.character.maxSlopeAngleRadians,
            0.0f,
            1.55334306f);
        settings.mMass = (std::max)(createInfo.character.mass, 0.001f);
        settings.mMaxStrength = 0.0f;
        settings.mCharacterPadding = (std::clamp)(
            createInfo.character.characterPadding,
            0.001f,
            (std::max)(
                0.001f,
                (std::min)(extents.GetX(), extents.GetZ()) * 0.5f));
        settings.mPredictiveContactDistance = (std::max)(
            createInfo.character.predictiveContactDistance,
            settings.mCharacterPadding);
        settings.mPenetrationRecoverySpeed = (std::clamp)(
            createInfo.character.penetrationRecoverySpeed,
            0.0f,
            1.0f);
        settings.mMaxNumHits = (std::clamp)(
            createInfo.character.maxCollisionHits,
            16u,
            4096u);
        settings.mEnhancedInternalEdgeRemoval =
            createInfo.character.enhancedInternalEdgeRemoval;
        JPH::Ref<JPH::CharacterVirtual> character =
            new JPH::CharacterVirtual(
                &settings,
                ToJoltPosition(createInfo.initialPose.position),
                ToJolt(createInfo.initialPose.rotation),
                EncodeCharacterUserData(handle),
                physicsSystem_.get());
        if (!character) {
            std::scoped_lock lock(recordsMutex_);
            freeCharacterSlots_.push_back(handle.slot);
            result.error = PhysicsErrorCode::BackendCapacityExceeded;
            result.message = "Jolt failed to allocate virtual character";
            result.recoverable = true;
            return result;
        }

        if (!characterContactListener_) {
            characterContactListener_ =
                std::make_unique<CharacterContactListener>(*this);
        }
        character->SetListener(characterContactListener_.get());
        character->SetLinearVelocity(ToJolt(
            createInfo.initialLinearVelocity));
        {
            std::scoped_lock lock(recordsMutex_);
            CharacterRecord& record = characterRecords_[handle.slot];
            record.occupied = true;
            record.object = createInfo.character.object;
            record.desc = createInfo.character;
            record.character = std::move(character);
        }

        (void)RefreshCharacterContacts(handle);
        result.handle = handle;
        result.message = "Jolt virtual character created";
        return result;
    }

    bool JoltPhysicsBackend::DestroyCharacter(
        PhysicsCharacterHandle handle) {
        if (!initialized_ || !physicsSystem_) {
            return false;
        }
        JPH::Ref<JPH::CharacterVirtual> character{};
        {
            std::scoped_lock lock(recordsMutex_);
            CharacterRecord* record = FindCharacter(handle);
            if (record == nullptr) {
                return false;
            }
            character = record->character;
            record->occupied = false;
            record->object = {};
            record->desc = {};
            record->character = nullptr;
            freeCharacterSlots_.push_back(handle.slot);
        }
        character = nullptr;
        return true;
    }

    bool JoltPhysicsBackend::SetCharacterPose(
        PhysicsCharacterHandle handle,
        const PhysicsPose& pose) {
        std::scoped_lock lock(recordsMutex_);
        CharacterRecord* record = FindCharacter(handle);
        if (record == nullptr || !record->character) {
            return false;
        }
        record->character->SetPosition(ToJoltPosition(pose.position));
        record->character->SetRotation(ToJolt(pose.rotation));
        return true;
    }

    bool JoltPhysicsBackend::SetCharacterVelocity(
        PhysicsCharacterHandle handle,
        const MATH::Vec3& linearVelocity) {
        std::scoped_lock lock(recordsMutex_);
        CharacterRecord* record = FindCharacter(handle);
        if (record == nullptr || !record->character) {
            return false;
        }
        record->character->SetLinearVelocity(ToJolt(linearVelocity));
        return true;
    }

} // namespace HIKARI::PHYSICS::JOLT_BACKEND
