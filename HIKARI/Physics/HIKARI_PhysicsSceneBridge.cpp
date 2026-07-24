#include "Physics/HIKARI_PhysicsSceneBridge.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Physics/HIKARI_PhysicsBodyValidator.h"
#include "Physics/HIKARI_PhysicsCollisionGeometryStore.h"
#include "Physics/HIKARI_PhysicsDefinitionScale.h"

namespace HIKARI::PHYSICS {
    namespace {
        constexpr float kDegreesToRadians =
            0.01745329251994329577f;

        MATH::Vec3 Multiply(
            const MATH::Vec3& left,
            const MATH::Vec3& right) noexcept {
            return {
                left.x * right.x,
                left.y * right.y,
                left.z * right.z
            };
        }

        MATH::Vec3 Absolute(const MATH::Vec3& value) noexcept {
            return {
                std::abs(value.x),
                std::abs(value.y),
                std::abs(value.z)
            };
        }

        PhysicsBodyDesc BuildBody(
            const GameObject& object,
            const PhysicsBodyComponent* component) {
            PhysicsBodyDesc body{};
            body.object = object.GetRuntimeHandle();
            if (component == nullptr || !component->IsEnabled()) {
                body.motionType = PhysicsMotionType::Static;
                return body;
            }
            body.motionType = component->GetMotionType();
            body.mass = component->GetMass();
            body.gravityScale = component->GetGravityScale();
            body.linearDamping = component->GetLinearDamping();
            body.angularDamping = component->GetAngularDamping();
            body.allowSleeping = component->GetAllowSleeping();
            body.continuousCollision =
                component->GetContinuousCollision();
            const MATH::Vec3& lockTranslation =
                component->GetLockedTranslationAxes();
            const MATH::Vec3& lockRotation =
                component->GetLockedRotationAxes();
            body.lockTranslationX = lockTranslation.x >= 0.5f;
            body.lockTranslationY = lockTranslation.y >= 0.5f;
            body.lockTranslationZ = lockTranslation.z >= 0.5f;
            body.lockRotationX = lockRotation.x >= 0.5f;
            body.lockRotationY = lockRotation.y >= 0.5f;
            body.lockRotationZ = lockRotation.z >= 0.5f;
            return body;
        }

        void HashValue(uint64_t& seed, uint64_t value) noexcept {
            seed ^= value + 0x9E3779B97F4A7C15ull +
                (seed << 6u) + (seed >> 2u);
        }

        void HashFloat(uint64_t& seed, float value) noexcept {
            HashValue(seed, std::bit_cast<uint32_t>(value));
        }

        void HashVec3(
            uint64_t& seed,
            const MATH::Vec3& value) noexcept {
            HashFloat(seed, value.x);
            HashFloat(seed, value.y);
            HashFloat(seed, value.z);
        }

        void HashQuat(
            uint64_t& seed,
            const MATH::Quat& value) noexcept {
            HashFloat(seed, value.x);
            HashFloat(seed, value.y);
            HashFloat(seed, value.z);
            HashFloat(seed, value.w);
        }
    }

    PhysicsShapeDesc BuildPhysicsShapeDesc(
        const ColliderComponent& collider,
        const MATH::Vec3& worldScale,
        uint32_t componentOrdinal) {
        const MATH::Vec3 absoluteScale = Absolute(worldScale);
        const ResolvedColliderShape resolved = collider.ResolveShape();
        PhysicsShapeDesc shape{};
        shape.key.componentOrdinal = componentOrdinal;
        shape.type = resolved.type;
        shape.localCenter = Multiply(resolved.center, absoluteScale);
        const MATH::Vec3 rotationDegrees =
            resolved.rotationEulerDegrees;
        shape.localRotation = MATH::NormalizeQ(
            MATH::Quat::FromEulerXYZ(
                rotationDegrees.x * kDegreesToRadians,
                rotationDegrees.y * kDegreesToRadians,
                rotationDegrees.z * kDegreesToRadians));
        shape.halfExtents = Multiply(
            resolved.size, absoluteScale) * 0.5f;
        const float radialScale = resolved.type ==
                PhysicsShapeType::Sphere
            ? (std::max)({
                absoluteScale.x,
                absoluteScale.y,
                absoluteScale.z
            })
            : (std::max)(absoluteScale.x, absoluteScale.z);
        shape.radius = resolved.radius * radialScale;
        shape.height = resolved.height * absoluteScale.y;
        shape.localScale = absoluteScale;
        shape.isTrigger = collider.IsTrigger();
        shape.material = PhysicsMaterialDesc{
            collider.GetFriction(),
            collider.GetRestitution(),
            collider.GetDensity()
        };
        shape.filter = PhysicsCollisionFilter{
            collider.GetCollisionLayer(),
            collider.GetCollisionMask()
        };
        return shape;
    }

    bool TryGetPhysicsWorldPoseAndScale(
        const GameObject& object,
        PhysicsPose& outPose,
        MATH::Vec3& outScale) {
        return MATH::DecomposeTRS(
            object.GetTransform().GetWorldMatrix(),
            outPose.position,
            outPose.rotation,
            outScale);
    }

    PhysicsBodyBuildResult BuildPhysicsBodyCreateInfo(
        const GameObject& object,
        PhysicsBodyCreateInfo& outCreateInfo,
        MATH::Vec3& outWorldScale,
        PhysicsCollisionGeometryStore* collisionGeometryStore,
        const MATH::Vec3* retainedDefinitionScale) {
        PhysicsBodyBuildResult result{};
        PhysicsPose pose{};
        if (!TryGetPhysicsWorldPoseAndScale(
                object,
                pose,
                outWorldScale)) {
            result.error = PhysicsErrorCode::TransformInvalid;
            result.message =
                "object world transform cannot be decomposed";
            return result;
        }
        if (!MATH::IsFinite(outWorldScale) ||
            outWorldScale.x <= 0.0f ||
            outWorldScale.y <= 0.0f ||
            outWorldScale.z <= 0.0f) {
            result.error = PhysicsErrorCode::UnsupportedScale;
            result.message =
                "physics requires positive world scale; bake mirrored scale into the model before adding collision";
            return result;
        }
        outWorldScale = ResolvePhysicsDefinitionScale(
            outWorldScale,
            retainedDefinitionScale);

        outCreateInfo = {};
        outCreateInfo.initialPose = pose;
        const PhysicsBodyComponent* bodyComponent =
            object.GetComponent<PhysicsBodyComponent>();
        outCreateInfo.body = BuildBody(object, bodyComponent);
        if (bodyComponent != nullptr && bodyComponent->IsEnabled()) {
            outCreateInfo.initialLinearVelocity =
                bodyComponent->GetInitialLinearVelocity();
            outCreateInfo.initialAngularVelocity =
                bodyComponent->GetInitialAngularVelocity();
        }
        uint32_t componentOrdinal = 0u;
        bool appendFailed = false;
        object.ForEachComponent<ColliderComponent>(
            [&](const ColliderComponent& collider) {
                ++componentOrdinal;
                if (!collider.IsEnabled()) {
                    return;
                }
                result.hasDefinition = true;
                if (collider.UsesCollisionGeometryAsset()) {
                    if (collisionGeometryStore == nullptr) {
                        appendFailed = true;
                        result.error =
                            PhysicsErrorCode::CollisionAssetMissing;
                        result.message =
                            "collision geometry store is unavailable";
                        return;
                    }
                    const auto append =
                        collisionGeometryStore->AppendShapes(
                            collider,
                            outWorldScale,
                            componentOrdinal,
                            outCreateInfo.shapes);
                    if (!append.success) {
                        appendFailed = true;
                        result.error = append.error;
                        result.message = append.message;
                    }
                    result.sourceRevision ^= append.contentRevision +
                        0x9E3779B97F4A7C15ull +
                        (result.sourceRevision << 6u) +
                        (result.sourceRevision >> 2u);
                    return;
                }
                outCreateInfo.shapes.push_back(BuildPhysicsShapeDesc(
                    collider,
                    outWorldScale,
                    componentOrdinal));
            });
        if (collisionGeometryStore != nullptr) {
            result.sourceRevision ^=
                collisionGeometryStore->GetSourceRevision();
        }
        if (!result.hasDefinition) {
            result.error = PhysicsErrorCode::NoEnabledShapes;
            result.message = "object has no enabled colliders";
            return result;
        }
        if (appendFailed) {
            outCreateInfo.shapes.clear();
            return result;
        }

        const PhysicsBodyValidationResult validation =
            ValidatePhysicsBodyCreateInfo(outCreateInfo);
        result.success = validation.valid;
        result.error = validation.error;
        result.message = validation.message;
        return result;
    }

    uint64_t ComputePhysicsBodyDefinitionSignature(
        const PhysicsBodyCreateInfo& createInfo,
        const MATH::Vec3& worldScale) noexcept {
        uint64_t signature = 1469598103934665603ull;
        const PhysicsBodyDesc& body = createInfo.body;
        HashValue(signature, static_cast<uint64_t>(body.motionType));
        HashFloat(signature, body.mass);
        HashFloat(signature, body.gravityScale);
        HashFloat(signature, body.linearDamping);
        HashFloat(signature, body.angularDamping);
        HashValue(signature, body.allowSleeping);
        HashValue(signature, body.continuousCollision);
        HashValue(signature, body.lockTranslationX);
        HashValue(signature, body.lockTranslationY);
        HashValue(signature, body.lockTranslationZ);
        HashValue(signature, body.lockRotationX);
        HashValue(signature, body.lockRotationY);
        HashValue(signature, body.lockRotationZ);
        HashVec3(signature, worldScale);
        HashVec3(signature, createInfo.initialLinearVelocity);
        HashVec3(signature, createInfo.initialAngularVelocity);
        HashValue(signature, createInfo.shapes.size());
        for (const PhysicsShapeDesc& shape : createInfo.shapes) {
            HashValue(signature, shape.key.sourceShapeId);
            HashValue(signature, shape.key.componentOrdinal);
            HashValue(signature, static_cast<uint64_t>(shape.type));
            HashVec3(signature, shape.localCenter);
            HashQuat(signature, shape.localRotation);
            HashVec3(signature, shape.halfExtents);
            HashFloat(signature, shape.radius);
            HashFloat(signature, shape.height);
            HashVec3(signature, shape.localScale);
            HashValue(signature, shape.vertexOffset);
            HashValue(signature, shape.vertexCount);
            HashValue(signature, shape.indexOffset);
            HashValue(signature, shape.indexCount);
            HashValue(signature, shape.geometryContentRevision);
            HashValue(signature, shape.isTrigger);
            HashFloat(signature, shape.material.friction);
            HashFloat(signature, shape.material.restitution);
            HashFloat(signature, shape.material.density);
            HashValue(signature, shape.filter.layer);
            HashValue(signature, shape.filter.mask);
        }
        return signature;
    }

    bool ArePhysicsPosesNearlyEqual(
        const PhysicsPose& left,
        const PhysicsPose& right) noexcept {
        const MATH::Vec3 positionDelta =
            left.position - right.position;
        const float rotationDot = std::abs(
            left.rotation.x * right.rotation.x +
            left.rotation.y * right.rotation.y +
            left.rotation.z * right.rotation.z +
            left.rotation.w * right.rotation.w);
        return MATH::Dot(positionDelta, positionDelta) <= 1.0e-10f &&
            rotationDot >= 0.999999f;
    }

    bool ApplyPhysicsWorldPose(
        GameObject& object,
        const PhysicsPose& pose) {
        MATH::Vec3 currentWorldPosition{};
        MATH::Quat currentWorldRotation{};
        MATH::Vec3 currentWorldScale{};
        if (!MATH::DecomposeTRS(
                object.GetTransform().GetWorldMatrix(),
                currentWorldPosition,
                currentWorldRotation,
                currentWorldScale)) {
            return false;
        }

        const MATH::Mat4 desiredWorld = MATH::Mat4::TRS(
            pose.position,
            MATH::NormalizeQ(pose.rotation),
            currentWorldScale);
        const GameObject* parent = object.GetParent();
        const MATH::Mat4 desiredLocal = parent != nullptr
            ? MATH::Inverse(
                parent->GetTransform().GetWorldMatrix()) * desiredWorld
            : desiredWorld;
        MATH::Vec3 localPosition{};
        MATH::Quat localRotation{};
        MATH::Vec3 localScale{};
        if (!MATH::DecomposeTRS(
                desiredLocal,
                localPosition,
                localRotation,
                localScale)) {
            return false;
        }
        Transform3D local = object.GetTransform();
        local.position = localPosition;
        local.rotation = MATH::NormalizeQ(localRotation);
        local.scale = localScale;
        local.useExplicitMatrix = false;
        return object.SetLocalTransform(local);
    }

} // namespace HIKARI::PHYSICS
