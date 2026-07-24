#include "Physics/HIKARI_PhysicsBodyValidator.h"

#include <algorithm>
#include <cmath>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::PHYSICS {
    namespace {

        bool GeometryRangeIsValid(
            const PhysicsShapeDesc& shape) noexcept {
            return shape.geometry != nullptr &&
                shape.vertexOffset <= shape.geometry->vertices.size() &&
                shape.vertexCount <=
                    shape.geometry->vertices.size() - shape.vertexOffset &&
                shape.indexOffset <= shape.geometry->indices.size() &&
                shape.indexCount <=
                    shape.geometry->indices.size() - shape.indexOffset;
        }

        bool HasNonUniformScale(const MATH::Vec3& scale) noexcept {
            return std::abs(scale.x - scale.y) > 1.0e-5f ||
                std::abs(scale.x - scale.z) > 1.0e-5f;
        }

        bool HasLocalRotation(const MATH::Quat& rotation) noexcept {
            const MATH::Quat normalized = MATH::NormalizeQ(rotation);
            return std::abs(normalized.x) > 1.0e-5f ||
                std::abs(normalized.y) > 1.0e-5f ||
                std::abs(normalized.z) > 1.0e-5f;
        }

        PhysicsBodyValidationResult Failure(
            PhysicsErrorCode error,
            std::string message) {
            PhysicsBodyValidationResult result{};
            result.error = error;
            result.message = std::move(message);
            return result;
        }

    } // namespace

    PhysicsBodyValidationResult ValidatePhysicsBodyCreateInfo(
        const PhysicsBodyCreateInfo& createInfo) {
        if (createInfo.shapes.empty()) {
            return Failure(
                PhysicsErrorCode::NoEnabledShapes,
                "body has no enabled collision shapes");
        }
        if (!createInfo.body.object.IsValid() ||
            !MATH::IsFinite(createInfo.initialPose.position) ||
            !MATH::IsFinite(createInfo.initialPose.rotation) ||
            !MATH::IsFinite(createInfo.initialLinearVelocity) ||
            !MATH::IsFinite(createInfo.initialAngularVelocity) ||
            !std::isfinite(createInfo.body.mass) ||
            !std::isfinite(createInfo.body.gravityScale) ||
            !std::isfinite(createInfo.body.linearDamping) ||
            !std::isfinite(createInfo.body.angularDamping) ||
            createInfo.body.mass <= 0.0f ||
            createInfo.body.linearDamping < 0.0f ||
            createInfo.body.angularDamping < 0.0f) {
            return Failure(
                PhysicsErrorCode::InvalidBodyDefinition,
                "body contains a non-finite or invalid value");
        }

        const bool fullyLocked =
            createInfo.body.lockTranslationX &&
            createInfo.body.lockTranslationY &&
            createInfo.body.lockTranslationZ &&
            createInfo.body.lockRotationX &&
            createInfo.body.lockRotationY &&
            createInfo.body.lockRotationZ;
        if (createInfo.body.motionType == PhysicsMotionType::Dynamic &&
            fullyLocked) {
            return Failure(
                PhysicsErrorCode::FullyLockedDynamicBody,
                "dynamic body cannot lock all translation and rotation axes; use Kinematic or Static");
        }

        PhysicsBodyValidationResult result{};
        for (const PhysicsShapeDesc& shape : createInfo.shapes) {
            if (!MATH::IsFinite(shape.localCenter) ||
                !MATH::IsFinite(shape.localRotation) ||
                !MATH::IsFinite(shape.halfExtents) ||
                !MATH::IsFinite(shape.localScale) ||
                !std::isfinite(shape.radius) ||
                !std::isfinite(shape.height) ||
                !std::isfinite(shape.material.friction) ||
                !std::isfinite(shape.material.restitution) ||
                !std::isfinite(shape.material.density) ||
                shape.material.friction < 0.0f ||
                shape.material.restitution < 0.0f ||
                shape.material.restitution > 1.0f ||
                shape.material.density <= 0.0f ||
                shape.filter.layer == 0u) {
                return Failure(
                    PhysicsErrorCode::InvalidBodyDefinition,
                    "collision shape contains a non-finite or invalid value");
            }
            if (shape.type == PhysicsShapeType::Box &&
                (shape.halfExtents.x <= 0.0f ||
                 shape.halfExtents.y <= 0.0f ||
                 shape.halfExtents.z <= 0.0f)) {
                return Failure(
                    PhysicsErrorCode::InvalidBodyDefinition,
                    "box collision requires positive half extents");
            }
            if (shape.type == PhysicsShapeType::Sphere &&
                shape.radius <= 0.0f) {
                return Failure(
                    PhysicsErrorCode::InvalidBodyDefinition,
                    "sphere collision requires a positive radius");
            }
            if (shape.type == PhysicsShapeType::Capsule &&
                (shape.radius <= 0.0f ||
                 shape.height < shape.radius * 2.0f)) {
                return Failure(
                    PhysicsErrorCode::InvalidBodyDefinition,
                    "capsule collision requires positive radius and height at least twice the radius");
            }
            if (shape.localScale.x <= 0.0f ||
                shape.localScale.y <= 0.0f ||
                shape.localScale.z <= 0.0f) {
                return Failure(
                    PhysicsErrorCode::UnsupportedScale,
                    "collision geometry requires positive, non-zero scale");
            }
            if (HasNonUniformScale(shape.localScale) &&
                HasLocalRotation(shape.localRotation)) {
                return Failure(
                    PhysicsErrorCode::UnsupportedScale,
                    "a locally rotated collider under non-uniform world scale would introduce shear; bake the scale or remove the collider rotation");
            }
            if (shape.type == PhysicsShapeType::TriangleMesh &&
                createInfo.body.motionType != PhysicsMotionType::Static) {
                return Failure(
                    PhysicsErrorCode::UnsupportedMotionShape,
                    "triangle mesh collision is static-only; use convex shapes for moving bodies");
            }
            if ((shape.type == PhysicsShapeType::ConvexHull ||
                 shape.type == PhysicsShapeType::TriangleMesh) &&
                !GeometryRangeIsValid(shape)) {
                return Failure(
                    PhysicsErrorCode::CollisionAssetInvalid,
                    "collision geometry range is missing or invalid");
            }
            if (shape.type == PhysicsShapeType::ConvexHull &&
                shape.vertexCount < 4u) {
                return Failure(
                    PhysicsErrorCode::CollisionAssetInvalid,
                    "convex hull requires at least four vertices");
            }
            if (shape.type == PhysicsShapeType::TriangleMesh &&
                (shape.vertexCount < 3u || shape.indexCount < 3u ||
                 shape.indexCount % 3u != 0u)) {
                return Failure(
                    PhysicsErrorCode::CollisionAssetInvalid,
                    "triangle mesh geometry is incomplete");
            }
        }

        result.valid = true;
        result.error = PhysicsErrorCode::None;
        result.message = "body definition is valid";
        return result;
    }

    const char* ToString(PhysicsErrorCode error) noexcept {
        switch (error) {
        case PhysicsErrorCode::WorldUnavailable: return "World unavailable";
        case PhysicsErrorCode::TransformInvalid: return "Invalid transform";
        case PhysicsErrorCode::NoEnabledShapes: return "No enabled shapes";
        case PhysicsErrorCode::CollisionAssetMissing: return "Collision asset missing";
        case PhysicsErrorCode::CollisionAssetInvalid: return "Collision asset invalid";
        case PhysicsErrorCode::UnsupportedScale: return "Unsupported scale";
        case PhysicsErrorCode::UnsupportedMotionShape: return "Unsupported body/shape combination";
        case PhysicsErrorCode::FullyLockedDynamicBody: return "Dynamic body fully locked";
        case PhysicsErrorCode::InvalidBodyDefinition: return "Invalid body definition";
        case PhysicsErrorCode::BackendShapeCreationFailed: return "Backend shape creation failed";
        case PhysicsErrorCode::BackendCapacityExceeded: return "Backend capacity exceeded";
        case PhysicsErrorCode::BackendStepFailed: return "Backend step failed";
        case PhysicsErrorCode::BackendFailure: return "Backend failure";
        case PhysicsErrorCode::None:
        default: return "None";
        }
    }

    const char* ToString(PhysicsBodyRuntimeState state) noexcept {
        switch (state) {
        case PhysicsBodyRuntimeState::Ready: return "Ready";
        case PhysicsBodyRuntimeState::RetainedPrevious: return "Using last-known-good body";
        case PhysicsBodyRuntimeState::PendingRetry: return "Pending retry";
        case PhysicsBodyRuntimeState::Invalid:
        default: return "Invalid";
        }
    }

    const char* ToString(PhysicsMotionType motionType) noexcept {
        switch (motionType) {
        case PhysicsMotionType::Kinematic: return "Kinematic";
        case PhysicsMotionType::Dynamic: return "Dynamic";
        case PhysicsMotionType::Static:
        default: return "Static";
        }
    }

} // namespace HIKARI::PHYSICS
