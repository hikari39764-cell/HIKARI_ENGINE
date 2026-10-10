#include "Physics/HIKARI_PhysicsSceneBridge.h"

#include "Core/HIKARI_MathConfig.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Physics/HIKARI_PhysicsBodyValidator.h"
#include "Physics/HIKARI_PhysicsCollisionGeometryStore.h"
#include "Physics/HIKARI_PhysicsDefinitionScale.h"
#include "Physics/HIKARI_TerrainCollisionProjection.h"

namespace HIKARI::PHYSICS {
namespace {
constexpr float kDegreesToRadians = MATH::kDegreesToRadians;

MATH::Vec3 Multiply(const MATH::Vec3& left, const MATH::Vec3& right) noexcept {
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

MATH::Vec3 Absolute(const MATH::Vec3& value) noexcept {
    return {std::abs(value.x), std::abs(value.y), std::abs(value.z)};
}

PhysicsBodyDesc BuildBody(const GameObject& object, const PhysicsBodyComponent* component) {
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
    body.continuousCollision = component->GetContinuousCollision();
    const MATH::Vec3& lockTranslation = component->GetLockedTranslationAxes();
    const MATH::Vec3& lockRotation = component->GetLockedRotationAxes();
    body.lockTranslationX = lockTranslation.x >= 0.5f;
    body.lockTranslationY = lockTranslation.y >= 0.5f;
    body.lockTranslationZ = lockTranslation.z >= 0.5f;
    body.lockRotationX = lockRotation.x >= 0.5f;
    body.lockRotationY = lockRotation.y >= 0.5f;
    body.lockRotationZ = lockRotation.z >= 0.5f;
    return body;
}

void HashValue(uint64_t& seed, uint64_t value) noexcept {
    seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6u) + (seed >> 2u);
}

void HashFloat(uint64_t& seed, float value) noexcept { HashValue(seed, std::bit_cast<uint32_t>(value)); }

void HashVec3(uint64_t& seed, const MATH::Vec3& value) noexcept {
    HashFloat(seed, value.x);
    HashFloat(seed, value.y);
    HashFloat(seed, value.z);
}

void HashQuat(uint64_t& seed, const MATH::Quat& value) noexcept {
    HashFloat(seed, value.x);
    HashFloat(seed, value.y);
    HashFloat(seed, value.z);
    HashFloat(seed, value.w);
}

void HashBodyDefinitionFields(uint64_t& signature, const PhysicsBodyDesc& body, const MATH::Vec3& worldScale,
                              const MATH::Vec3& initialLinearVelocity,
                              const MATH::Vec3& initialAngularVelocity) noexcept {
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
    HashVec3(signature, initialLinearVelocity);
    HashVec3(signature, initialAngularVelocity);
}

void HashShapeDefinitionFields(uint64_t& signature, const PhysicsShapeDesc& shape) noexcept {
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

} // namespace

namespace {

PhysicsShapeDesc BuildResolvedShapeDesc(const ColliderComponent& collider, const ResolvedColliderShape& resolved,
                                        const MATH::Vec3& worldScale, uint32_t componentOrdinal) {
    const MATH::Vec3 absoluteScale = Absolute(worldScale);
    PhysicsShapeDesc shape{};
    shape.key.componentOrdinal = componentOrdinal;
    shape.type = resolved.type;
    shape.localCenter = Multiply(resolved.center, absoluteScale);
    const MATH::Vec3 rotationDegrees = resolved.rotationEulerDegrees;
    shape.localRotation = MATH::NormalizeQ(MATH::Quat::FromEulerXYZ(rotationDegrees.x * kDegreesToRadians,
                                                                    rotationDegrees.y * kDegreesToRadians,
                                                                    rotationDegrees.z * kDegreesToRadians));
    shape.halfExtents = Multiply(resolved.size, absoluteScale) * 0.5f;
    const float radialScale = resolved.type == PhysicsShapeType::Sphere
                                  ? (std::max)({absoluteScale.x, absoluteScale.y, absoluteScale.z})
                                  : (std::max)(absoluteScale.x, absoluteScale.z);
    shape.radius = resolved.radius * radialScale;
    shape.height = resolved.height * absoluteScale.y;
    shape.localScale = absoluteScale;
    shape.isTrigger = collider.IsTrigger();
    shape.material = PhysicsMaterialDesc{collider.GetFriction(), collider.GetRestitution(), collider.GetDensity()};
    shape.filter = PhysicsCollisionFilter{collider.GetCollisionLayer(), collider.GetCollisionMask()};
    return shape;
}

} // namespace

PhysicsShapeDesc BuildPhysicsShapeDesc(const ColliderComponent& collider, const MATH::Vec3& worldScale,
                                       uint32_t componentOrdinal) {
    return BuildResolvedShapeDesc(collider, collider.ResolveShape(), worldScale, componentOrdinal);
}

std::vector<PhysicsShapeDesc> BuildPhysicsShapeDescs(const ColliderComponent& collider, const MATH::Vec3& worldScale,
                                                     uint32_t componentOrdinal) {
    const std::vector<ResolvedColliderShape> resolved = collider.ResolveShapes();
    std::vector<PhysicsShapeDesc> shapes;
    shapes.reserve(resolved.size());
    for (size_t index = 0; index < resolved.size(); ++index) {
        shapes.push_back(BuildResolvedShapeDesc(collider, resolved[index], worldScale, componentOrdinal));
        if (resolved.size() > 1u) {
            shapes.back().key.sourceShapeId = index + 1u;
        }
    }
    return shapes;
}

bool TryGetPhysicsWorldPoseAndScale(const GameObject& object, PhysicsPose& outPose, MATH::Vec3& outScale) {
    return MATH::DecomposeTRS(object.GetTransform().GetWorldMatrix(), outPose.position, outPose.rotation, outScale);
}

PhysicsBodyBuildResult BuildPhysicsBodyCreateInfo(const GameObject& object, PhysicsBodyCreateInfo& outCreateInfo,
                                                  MATH::Vec3& outWorldScale,
                                                  PhysicsCollisionGeometryStore* collisionGeometryStore,
                                                  const TerrainCollisionProjection* terrainProjection,
                                                  const MATH::Vec3* retainedDefinitionScale) {
    PhysicsBodyBuildResult result{};
    PhysicsPose pose{};
    if (!TryGetPhysicsWorldPoseAndScale(object, pose, outWorldScale)) {
        result.error = PhysicsErrorCode::TransformInvalid;
        result.message = "object world transform cannot be decomposed";
        return result;
    }
    if (!MATH::IsFinite(outWorldScale) || outWorldScale.x <= 0.0f || outWorldScale.y <= 0.0f ||
        outWorldScale.z <= 0.0f) {
        result.error = PhysicsErrorCode::UnsupportedScale;
        result.message =
            "physics requires positive world scale; bake mirrored scale into the model before adding collision";
        return result;
    }
    outWorldScale = ResolvePhysicsDefinitionScale(outWorldScale, retainedDefinitionScale);

    outCreateInfo = {};
    // Terrain page は数百の immutable chunk shape を公開し得る。source
    // gate が rebuild を許可した時だけ、正確な容量を先に確保する。
    const std::size_t terrainShapeCapacity =
        terrainProjection != nullptr
            ? (!terrainProjection->chunkShapes.empty() ? terrainProjection->chunkShapes.size()
                                                       : (terrainProjection->geometry != nullptr ? 1u : 0u))
            : 0u;
    const std::size_t authoredShapeCapacity = object.GetComponents().size();
    if (authoredShapeCapacity <= outCreateInfo.shapes.max_size() &&
        terrainShapeCapacity <= outCreateInfo.shapes.max_size() - authoredShapeCapacity) {
        outCreateInfo.shapes.reserve(terrainShapeCapacity + authoredShapeCapacity);
    }
    outCreateInfo.initialPose = pose;
    const PhysicsBodyComponent* bodyComponent = object.GetComponent<PhysicsBodyComponent>();
    outCreateInfo.body = BuildBody(object, bodyComponent);
    if (bodyComponent != nullptr && bodyComponent->IsEnabled()) {
        outCreateInfo.initialLinearVelocity = bodyComponent->GetInitialLinearVelocity();
        outCreateInfo.initialAngularVelocity = bodyComponent->GetInitialAngularVelocity();
    }
    uint32_t componentOrdinal = 0u;
    bool appendFailed = false;
    bool terrainCollisionAppended = false;
    const auto appendTerrainShape = [&](const std::shared_ptr<const PhysicsGeometryBuffer>& geometry,
                                        uint64_t contentRevision, uint32_t terrainShapeOrdinal) {
        if (geometry == nullptr || geometry->vertices.size() < 3u || geometry->indices.size() < 3u ||
            geometry->vertices.size() > (std::numeric_limits<uint32_t>::max)() ||
            geometry->indices.size() > (std::numeric_limits<uint32_t>::max)()) {
            return;
        }
        PhysicsShapeDesc terrainShape{};
        // "TERRAIN"。authored Collider component の ordinal と衝突しない
        // domain-owned stable shape identity。
        terrainShape.key.sourceShapeId = kTerrainCollisionShapeSourceId;
        terrainShape.key.componentOrdinal = terrainShapeOrdinal;
        terrainShape.type = PhysicsShapeType::TriangleMesh;
        terrainShape.localScale = Absolute(outWorldScale);
        terrainShape.geometry = geometry;
        terrainShape.vertexCount = static_cast<uint32_t>(geometry->vertices.size());
        terrainShape.indexCount = static_cast<uint32_t>(geometry->indices.size());
        terrainShape.geometryContentRevision = contentRevision;
        terrainShape.material = PhysicsMaterialDesc{0.8f, 0.0f, 1.0f};
        terrainShape.filter = PhysicsCollisionFilter{1u, 0xFFFFFFFFu};
        outCreateInfo.shapes.push_back(std::move(terrainShape));
        terrainCollisionAppended = true;
        result.hasDefinition = true;
        result.sourceRevision ^=
            contentRevision + 0x9E3779B97F4A7C15ull + (result.sourceRevision << 6u) + (result.sourceRevision >> 2u);
    };
    if (terrainProjection != nullptr) {
        uint32_t terrainShapeOrdinal = 1u;
        for (const TerrainCollisionChunkShape& chunk : terrainProjection->chunkShapes) {
            appendTerrainShape(chunk.geometry, chunk.contentRevision, terrainShapeOrdinal++);
        }
        if (!terrainCollisionAppended) {
            appendTerrainShape(terrainProjection->geometry, terrainProjection->contentRevision, 0u);
        }
    }
    object.ForEachComponent<ColliderComponent>([&](const ColliderComponent& collider) {
        ++componentOrdinal;
        if (!collider.IsEnabled()) {
            return;
        }
        result.hasDefinition = true;
        if (collider.UsesCollisionGeometryAsset()) {
            if (collisionGeometryStore == nullptr) {
                appendFailed = true;
                result.error = PhysicsErrorCode::CollisionAssetMissing;
                result.message = "collision geometry store is unavailable";
                return;
            }
            const auto append =
                collisionGeometryStore->AppendShapes(collider, outWorldScale, componentOrdinal, outCreateInfo.shapes);
            if (!append.success) {
                appendFailed = true;
                result.error = append.error;
                result.message = append.message;
            }
            result.sourceRevision ^= append.contentRevision + 0x9E3779B97F4A7C15ull + (result.sourceRevision << 6u) +
                                     (result.sourceRevision >> 2u);
            return;
        }
        for (PhysicsShapeDesc& shape : BuildPhysicsShapeDescs(collider, outWorldScale, componentOrdinal)) {
            outCreateInfo.shapes.push_back(std::move(shape));
        }
    });
    if (collisionGeometryStore != nullptr) {
        result.sourceRevision ^= collisionGeometryStore->GetSourceRevision();
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

    const PhysicsBodyValidationResult validation = ValidatePhysicsBodyCreateInfo(outCreateInfo);
    result.success = validation.valid;
    result.error = validation.error;
    result.message = validation.message;
    return result;
}

uint64_t ComputePhysicsBodyDefinitionSignature(const PhysicsBodyCreateInfo& createInfo,
                                               const MATH::Vec3& worldScale) noexcept {
    uint64_t signature = HASH::kFnv1a64OffsetBasis;
    const PhysicsBodyDesc& body = createInfo.body;
    HashBodyDefinitionFields(signature, body, worldScale, createInfo.initialLinearVelocity,
                             createInfo.initialAngularVelocity);
    HashValue(signature, createInfo.shapes.size());
    for (const PhysicsShapeDesc& shape : createInfo.shapes) {
        HashShapeDefinitionFields(signature, shape);
    }
    return signature;
}

uint64_t ComputePhysicsBodyDefinitionSourceSignature(const GameObject& object, uint64_t collisionGeometrySourceRevision,
                                                     const TerrainCollisionProjection* terrainProjection,
                                                     const MATH::Vec3* retainedDefinitionScale) noexcept {
    uint64_t signature = HASH::kFnv1a64OffsetBasis;
    HashValue(signature, object.GetRuntimeHandle().ToValue());
    HashValue(signature, collisionGeometrySourceRevision);

    PhysicsPose pose{};
    MATH::Vec3 measuredScale{};
    const bool transformValid = TryGetPhysicsWorldPoseAndScale(object, pose, measuredScale) &&
                                MATH::IsFinite(measuredScale) && measuredScale.x > 0.0f && measuredScale.y > 0.0f &&
                                measuredScale.z > 0.0f;
    HashValue(signature, transformValid ? 1u : 0u);
    MATH::Vec3 definitionScale{};
    if (transformValid) {
        definitionScale = ResolvePhysicsDefinitionScale(measuredScale, retainedDefinitionScale);
    } else {
        // 不正行列にも安定した retry key が必要。padding を混ぜないよう
        // 全 scalar を明示的に hash する。
        const MATH::Mat4 world = object.GetTransform().GetWorldMatrix();
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                HashFloat(signature, world.m[column][row]);
            }
        }
    }

    const PhysicsBodyComponent* bodyComponent = object.GetComponent<PhysicsBodyComponent>();
    const PhysicsBodyDesc body = BuildBody(object, bodyComponent);
    const bool bodyEnabled = bodyComponent != nullptr && bodyComponent->IsEnabled();
    const MATH::Vec3 initialLinearVelocity = bodyEnabled ? bodyComponent->GetInitialLinearVelocity() : MATH::Vec3{};
    const MATH::Vec3 initialAngularVelocity = bodyEnabled ? bodyComponent->GetInitialAngularVelocity() : MATH::Vec3{};
    HashValue(signature, bodyComponent != nullptr ? 1u : 0u);
    HashBodyDefinitionFields(signature, body, definitionScale, initialLinearVelocity, initialAngularVelocity);

    HashValue(signature, terrainProjection != nullptr ? 1u : 0u);
    if (terrainProjection != nullptr) {
        HashValue(signature, terrainProjection->sourceRevision);
        HashValue(signature, terrainProjection->contentRevision);
        HashValue(signature, terrainProjection->chunkShapes.size());
        HashValue(signature, terrainProjection->geometry != nullptr ? 1u : 0u);
    }

    uint32_t componentOrdinal = 0u;
    object.ForEachComponent<ColliderComponent>([&](const ColliderComponent& collider) {
        ++componentOrdinal;
        HashValue(signature, componentOrdinal);
        HashValue(signature, collider.IsEnabled() ? 1u : 0u);
        HashValue(signature, static_cast<uint64_t>(collider.GetFitMode()));
        HashValue(signature, HASH::Fnv1aString(collider.GetCollisionGeometryAssetId()));
        if (!collider.IsEnabled()) {
            return;
        }
        // allocation-free な proxy でも Geometry fit provider を解決し、
        // terrain shape を実体化せず model 由来寸法を gate に含める。
        for (const PhysicsShapeDesc& shape : BuildPhysicsShapeDescs(collider, definitionScale, componentOrdinal)) {
            HashShapeDefinitionFields(signature, shape);
        }
    });
    HashValue(signature, componentOrdinal);
    return signature;
}

bool ArePhysicsPosesNearlyEqual(const PhysicsPose& left, const PhysicsPose& right) noexcept {
    const MATH::Vec3 positionDelta = left.position - right.position;
    const float rotationDot = std::abs(left.rotation.x * right.rotation.x + left.rotation.y * right.rotation.y +
                                       left.rotation.z * right.rotation.z + left.rotation.w * right.rotation.w);
    return MATH::Dot(positionDelta, positionDelta) <= 1.0e-10f && rotationDot >= 0.999999f;
}

bool ApplyPhysicsWorldPose(GameObject& object, const PhysicsPose& pose) {
    MATH::Vec3 currentWorldPosition{};
    MATH::Quat currentWorldRotation{};
    MATH::Vec3 currentWorldScale{};
    if (!MATH::DecomposeTRS(object.GetTransform().GetWorldMatrix(), currentWorldPosition, currentWorldRotation,
                            currentWorldScale)) {
        return false;
    }

    const MATH::Mat4 desiredWorld = MATH::Mat4::TRS(pose.position, MATH::NormalizeQ(pose.rotation), currentWorldScale);
    const GameObject* parent = object.GetParent();
    const MATH::Mat4 desiredLocal =
        parent != nullptr ? MATH::Inverse(parent->GetTransform().GetWorldMatrix()) * desiredWorld : desiredWorld;
    MATH::Vec3 localPosition{};
    MATH::Quat localRotation{};
    MATH::Vec3 localScale{};
    if (!MATH::DecomposeTRS(desiredLocal, localPosition, localRotation, localScale)) {
        return false;
    }
    Transform3D local = object.GetTransform();
    local.position = localPosition;
    local.rotation = MATH::NormalizeQ(localRotation);
    local.scale = localScale;
    local.useExplicitMatrix = false;
    // SetLocalTransform の false は同値でも返る。静止・衝突停止は正常な適用結果であり、
    // Motion state の公開を止めない。
    return object.GetTransform().HasSameLocalValue(local) || object.SetLocalTransform(local);
}

} // namespace HIKARI::PHYSICS
