#include "Scene/Terrain/HIKARI_TerrainInteractionSourceSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <unordered_set>
#include <vector>

#include "HIKARI/Project/FrameContext.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Physics/HIKARI_TerrainCollisionProjection.h"
#include "Physics/HIKARI_PhysicsTypes.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeObjectActivityService.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceFollowerComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

namespace HIKARI {
namespace {
constexpr uint64_t kColliderFeatureBase = 0x1000u;
constexpr uint64_t kFollowerFeature = 0x2000u;
constexpr uint64_t kCharacterSupportFeature = 0x3000u;
constexpr float kMotionEpsilon = 1.0e-5f;
constexpr float kStationaryPublishIntervalSeconds = 0.125f;

struct ResolvedInteractionSource {
    TERRAIN::TerrainInteractionSourceKey source{};
    PHYSICS::PhysicsMotionType motionType = PHYSICS::PhysicsMotionType::Static;
    MATH::Vec3 positionWS{};
    TERRAIN::TerrainInteractionFootprint footprint{};
    float normalLoadN = 0.0f;
    uint64_t sourceShapeId = 0u;
    uint64_t definitionSignature = 0u;
};

void HashCombine(uint64_t& seed, uint64_t value) noexcept {
    seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6u) + (seed >> 2u);
}

PHYSICS::PhysicsMotionType ResolveMotionType(const GameObject& object,
                                             const PHYSICS::PhysicsRuntimeStatusService* runtimeStatus) noexcept {
    const PhysicsBodyComponent* body = object.GetComponent<PhysicsBodyComponent>();
    if (body == nullptr || !body->IsEnabled()) {
        return PHYSICS::PhysicsMotionType::Static;
    }
    if (runtimeStatus != nullptr) {
        const PHYSICS::PhysicsBodyRuntimeStatus* status = runtimeStatus->FindBodyStatus(object.GetRuntimeHandle());
        if (status != nullptr && status->body.IsValid() && status->state != PHYSICS::PhysicsBodyRuntimeState::Invalid) {
            return status->effectiveMotionType;
        }
    }
    return body->GetMotionType();
}

MATH::Vec3 TransformPoint(const MATH::Mat4& matrix, const MATH::Vec3& point) noexcept {
    const MATH::Vec4 transformed = matrix.TransformPoint({point.x, point.y, point.z, 1.0f});
    const float inverseW = std::fabs(transformed.w) > 1.0e-6f ? 1.0f / transformed.w : 1.0f;
    return {transformed.x * inverseW, transformed.y * inverseW, transformed.z * inverseW};
}

MATH::Vec3 TransformVector(const MATH::Mat4& matrix, const MATH::Vec3& vector) noexcept {
    const MATH::Vec4 transformed = matrix.TransformPoint({vector.x, vector.y, vector.z, 0.0f});
    return {transformed.x, transformed.y, transformed.z};
}

float AxisLength(const MATH::Vec3& axis) noexcept { return std::sqrt(MATH::Dot(axis, axis)); }

MATH::Vec3 PlanarTangent(const MATH::Vec3& preferred, const MATH::Vec3& fallback) noexcept {
    MATH::Vec3 tangent{preferred.x, 0.0f, preferred.z};
    if (MATH::Dot(tangent, tangent) <= 1.0e-10f) {
        tangent = {fallback.x, 0.0f, fallback.z};
    }
    return MATH::Dot(tangent, tangent) > 1.0e-10f ? MATH::Normalize(tangent) : MATH::Vec3{1.0f, 0.0f, 0.0f};
}

TERRAIN::TerrainInteractionFootprint BuildRuntimeFootprint(const MATH::Quat& bodyRotation,
                                                           const PHYSICS::PhysicsShapeDesc& shape) noexcept {
    const MATH::Mat4 shapeRotation = MATH::Mat4::Rotate(bodyRotation) * MATH::Mat4::Rotate(shape.localRotation);
    const MATH::Vec3 axisX = TransformVector(shapeRotation, {1.0f, 0.0f, 0.0f});
    const MATH::Vec3 axisY = TransformVector(shapeRotation, {0.0f, 1.0f, 0.0f});
    const MATH::Vec3 axisZ = TransformVector(shapeRotation, {0.0f, 0.0f, 1.0f});
    TERRAIN::TerrainInteractionFootprint footprint{};
    if (shape.type == PHYSICS::PhysicsShapeType::Sphere) {
        const float radius = std::fabs(shape.radius);
        footprint.kind = TERRAIN::TerrainFootprintKind::Sphere;
        footprint.halfExtentsWS = {radius, radius};
        footprint.verticalRadiusWS = radius;
        return footprint;
    }
    if (shape.type == PHYSICS::PhysicsShapeType::Capsule) {
        const float radius = std::fabs(shape.radius);
        const float totalHalfLength = (std::max)(std::fabs(shape.height) * 0.5f, radius);
        const float segmentHalfLength = (std::max)(totalHalfLength - radius, 0.0f);
        const float planarAxisLength = AxisLength({axisY.x, 0.0f, axisY.z});
        footprint.kind = TERRAIN::TerrainFootprintKind::Capsule;
        footprint.tangentWS = PlanarTangent(axisY, axisX);
        footprint.halfExtentsWS = {radius + segmentHalfLength * planarAxisLength, radius};
        footprint.verticalRadiusWS = radius + segmentHalfLength * std::fabs(axisY.y);
        return footprint;
    }
    const MATH::Vec3 localHalf{std::fabs(shape.halfExtents.x), std::fabs(shape.halfExtents.y),
                               std::fabs(shape.halfExtents.z)};
    footprint.kind = TERRAIN::TerrainFootprintKind::Ellipse;
    const float planarXLength = AxisLength({axisX.x, 0.0f, axisX.z});
    const float planarZLength = AxisLength({axisZ.x, 0.0f, axisZ.z});
    footprint.tangentWS = planarXLength >= planarZLength ? PlanarTangent(axisX, axisZ) : PlanarTangent(axisZ, axisX);
    const MATH::Vec3 bitangentWS = MATH::Normalize(MATH::Cross(MATH::Vec3{0.0f, 1.0f, 0.0f}, footprint.tangentWS));
    const auto supportRadius = [&](const MATH::Vec3& direction) noexcept {
        return std::fabs(MATH::Dot(axisX, direction)) * localHalf.x +
               std::fabs(MATH::Dot(axisY, direction)) * localHalf.y +
               std::fabs(MATH::Dot(axisZ, direction)) * localHalf.z;
    };
    footprint.halfExtentsWS = {supportRadius(footprint.tangentWS), supportRadius(bitangentWS)};
    footprint.verticalRadiusWS =
        std::fabs(axisX.y) * localHalf.x + std::fabs(axisY.y) * localHalf.y + std::fabs(axisZ.y) * localHalf.z;
    return footprint;
}

bool ResolveColliderSource(const PHYSICS::PhysicsRuntimeStatusService* runtimeStatus, GameObject& object,
                           uint32_t componentOrdinal, ResolvedInteractionSource& outSource,
                           const MATH::Mat4* sourceWorldOverride = nullptr,
                           const PHYSICS::PhysicsShapeKey* sourceShapeOverride = nullptr) {
    const PHYSICS::PhysicsMotionType motionType = ResolveMotionType(object, runtimeStatus);
    if (motionType == PHYSICS::PhysicsMotionType::Static) {
        return false;
    }
    const ColliderComponent* sourceCollider = nullptr;
    uint32_t ordinal = 0u;
    object.ForEachComponent<ColliderComponent>([&](const ColliderComponent& collider) {
        ++ordinal;
        if (ordinal == componentOrdinal && collider.IsEnabled() && !collider.IsTrigger()) {
            sourceCollider = &collider;
        }
    });
    if (sourceCollider == nullptr) {
        return false;
    }
    const MATH::Mat4 sourceWorld =
        sourceWorldOverride != nullptr ? *sourceWorldOverride : object.GetTransform().GetWorldMatrix();
    MATH::Vec3 position{};
    MATH::Quat rotation{};
    MATH::Vec3 scale{};
    MATH::Mat4 bodyWorld = sourceWorld;
    if (MATH::DecomposeTRS(sourceWorld, position, rotation, scale)) {
        bodyWorld = MATH::Mat4::TRS(position, rotation, {1.0f, 1.0f, 1.0f});
    } else {
        rotation = MATH::Quat::Identity();
        scale = {1.0f, 1.0f, 1.0f};
    }
    const PHYSICS::PhysicsShapeDesc* runtimeShape = nullptr;
    const PHYSICS::PhysicsBodyRuntimeStatus* bodyStatus = nullptr;
    if (runtimeStatus != nullptr) {
        bodyStatus = runtimeStatus->FindBodyStatus(object.GetRuntimeHandle());
        const std::vector<PHYSICS::PhysicsShapeDesc>* shapes =
            runtimeStatus->FindBodyDebugShapes(object.GetRuntimeHandle());
        if (shapes != nullptr) {
            for (const PHYSICS::PhysicsShapeDesc& shape : *shapes) {
                const bool keyMatches = sourceShapeOverride != nullptr ? shape.key == *sourceShapeOverride
                                                                       : shape.key.componentOrdinal == componentOrdinal;
                if (keyMatches && !shape.isTrigger) {
                    runtimeShape = &shape;
                    break;
                }
            }
        }
    }
    const PhysicsBodyComponent* body = object.GetComponent<PhysicsBodyComponent>();
    outSource.source.object = object.GetRuntimeHandle();
    outSource.source.featureId = kColliderFeatureBase + componentOrdinal;
    outSource.motionType = motionType;
    // 実行中は physics が保持する寸法を優先し、未構築時も同じ変換を使う。
    PHYSICS::PhysicsShapeDesc authoredShape{};
    if (runtimeShape == nullptr) {
        authoredShape = PHYSICS::BuildPhysicsShapeDesc(*sourceCollider, scale, componentOrdinal);
        if (sourceShapeOverride != nullptr)
            authoredShape.key = *sourceShapeOverride;
        runtimeShape = &authoredShape;
    }
    outSource.sourceShapeId = runtimeShape->key.sourceShapeId;
    outSource.positionWS = TransformPoint(bodyWorld, runtimeShape->localCenter);
    outSource.footprint = BuildRuntimeFootprint(rotation, *runtimeShape);
    if (bodyStatus != nullptr && bodyStatus->state == PHYSICS::PhysicsBodyRuntimeState::Ready) {
        outSource.definitionSignature = bodyStatus->definitionSignature;
    }
    if (body != nullptr) {
        const float loadScale =
            motionType == PHYSICS::PhysicsMotionType::Dynamic ? std::fabs(body->GetGravityScale()) : 1.0f;
        // Kinematic は solver impulse を持たないため、設定 mass を
        // terrain response 用の nominal support load として扱う。
        outSource.normalLoadN = (std::max)(body->GetMass(), 0.0f) * loadScale * 9.81f;
    } else {
        outSource.normalLoadN = 0.0f;
    }
    return true;
}

bool SuppressFollowerCompatibility(const PHYSICS::PhysicsRuntimeStatusService* runtimeStatus,
                                   const GameObject& object) noexcept {
    const PhysicsBodyComponent* body = object.GetComponent<PhysicsBodyComponent>();
    if (body == nullptr || !body->IsEnabled()) {
        return false;
    }
    // 明示 Static は follower fallback にも流さない。Dynamic / Kinematic
    // は有効 collider が一つでもあれば physics contact を唯一経路とし、
    // trigger-only object も compatibility stamp へ漏らさない。
    if (ResolveMotionType(object, runtimeStatus) == PHYSICS::PhysicsMotionType::Static) {
        return true;
    }
    bool found = false;
    object.ForEachComponent<ColliderComponent>(
        [&](const ColliderComponent& collider) { found = found || collider.IsEnabled(); });
    return found;
}

bool FootprintMoved(const MATH::Vec3& previousPosition, const TERRAIN::TerrainInteractionFootprint& previousFootprint,
                    const MATH::Vec3& currentPosition,
                    const TERRAIN::TerrainInteractionFootprint& currentFootprint) noexcept {
    const MATH::Vec3 delta = currentPosition - previousPosition;
    if (MATH::Dot(delta, delta) > kMotionEpsilon * kMotionEpsilon) {
        return true;
    }
    if (previousFootprint.kind != currentFootprint.kind ||
        std::fabs(previousFootprint.halfExtentsWS.x - currentFootprint.halfExtentsWS.x) > kMotionEpsilon ||
        std::fabs(previousFootprint.halfExtentsWS.y - currentFootprint.halfExtentsWS.y) > kMotionEpsilon ||
        std::fabs(previousFootprint.verticalRadiusWS - currentFootprint.verticalRadiusWS) > kMotionEpsilon) {
        return true;
    }
    const float tangentAlignment = std::fabs(MATH::Dot(previousFootprint.tangentWS, currentFootprint.tangentWS));
    return tangentAlignment < 0.99999f;
}
} // namespace

std::size_t TerrainInteractionSourceSystem::ContactKeyHash::operator()(const ContactKey& key) const noexcept {
    uint64_t hash = key.source.object.ToValue();
    HashCombine(hash, key.source.featureId);
    HashCombine(hash, key.target.ToValue());
    HashCombine(hash, key.sourceShapeId);
    HashCombine(hash, key.definitionSignature);
    return static_cast<std::size_t>(hash);
}

std::size_t TerrainInteractionSourceSystem::SourceKeyHash::operator()(
    const TERRAIN::TerrainInteractionSourceKey& key) const noexcept {
    uint64_t hash = key.object.ToValue();
    HashCombine(hash, key.featureId);
    return static_cast<std::size_t>(hash);
}

void TerrainInteractionSourceSystem::OnWorldAttached(World& world) {
    kinematicMotionService_ = world.Services().Find<PHYSICS::KinematicMotionService>();
    runtimeStatus_ = world.Services().Find<PHYSICS::PhysicsRuntimeStatusService>();
    physicsWorld_ = world.Services().Find<PHYSICS::PhysicsWorldService>();
    runtimePlayState_ = world.Services().Find<RuntimePlayStateService>();
    runtimeActivityService_ = world.Services().Find<RuntimeObjectActivityService>();
    activeRigidContacts_.clear();
    previousCenters_.clear();
    preFixedPhysicsSources_.clear();
    activeCharacterContacts_.clear();
    activeFollowerTargets_.clear();
    editorPreviewContacts_.clear();
}

void TerrainInteractionSourceSystem::OnWorldDetached(World&) {
    kinematicMotionService_ = nullptr;
    runtimeStatus_ = nullptr;
    physicsWorld_ = nullptr;
    runtimePlayState_ = nullptr;
    runtimeActivityService_ = nullptr;
    activeRigidContacts_.clear();
    previousCenters_.clear();
    preFixedPhysicsSources_.clear();
    activeCharacterContacts_.clear();
    activeFollowerTargets_.clear();
    editorPreviewContacts_.clear();
}

void TerrainInteractionSourceSystem::PreFixedUpdate(World& world, const FrameContext&) { CapturePhysicsSources(world); }

void TerrainInteractionSourceSystem::PostFixedUpdate(World& world, const FrameContext& frame) {
    ProcessPhysicsContacts(world, frame);
    PublishRigidContacts(world, frame);
    PublishCharacterSources(world, frame);
    PublishFollowerSources(world, frame);
}

void TerrainInteractionSourceSystem::Update(World& world, const FrameContext& frame) {
    if (runtimePlayState_ == nullptr || runtimePlayState_->IsActive()) {
        for (const auto& [source, contact] : editorPreviewContacts_) {
            previousCenters_.erase(ContactKey{source, contact.target});
        }
        editorPreviewContacts_.clear();
        return;
    }
    PublishEditorPreviewSources(world, frame);
}

void TerrainInteractionSourceSystem::CapturePhysicsSources(World& world) {
    preFixedPhysicsSources_.clear();
    world.ForEachObjectWith<PhysicsBodyComponent>([&](GameObject& object, PhysicsBodyComponent&) {
        if (runtimeActivityService_ != nullptr && !runtimeActivityService_->IsActive(object.GetRuntimeHandle())) {
            return;
        }
        if (ResolveMotionType(object, runtimeStatus_) == PHYSICS::PhysicsMotionType::Static) {
            return;
        }
        MATH::Mat4 bodyWorld{};
        const MATH::Mat4* bodyWorldOverride = nullptr;
        const PHYSICS::PhysicsBodyRuntimeStatus* status =
            runtimeStatus_ != nullptr ? runtimeStatus_->FindBodyStatus(object.GetRuntimeHandle()) : nullptr;
        PHYSICS::PhysicsBodyState bodyState{};
        MATH::Vec3 scenePosition{};
        MATH::Quat sceneRotation{};
        MATH::Vec3 sceneScale{};
        if (physicsWorld_ != nullptr && status != nullptr && status->body.IsValid() &&
            physicsWorld_->TryGetBodyState(status->body, bodyState) &&
            MATH::DecomposeTRS(object.GetTransform().GetWorldMatrix(), scenePosition, sceneRotation, sceneScale)) {
            // PhysicsSystem::PreFixedUpdate は kinematic target を先に
            // push する。scene transform ではなく step 前 body pose を
            // 始点にしないと、直接 transform 駆動の移動を見失う。
            bodyWorld = MATH::Mat4::TRS(bodyState.pose.position, bodyState.pose.rotation,
                                        {std::fabs(sceneScale.x), std::fabs(sceneScale.y), std::fabs(sceneScale.z)});
            bodyWorldOverride = &bodyWorld;
        }
        uint32_t componentOrdinal = 0u;
        object.ForEachComponent<ColliderComponent>([&](const ColliderComponent&) {
            ++componentOrdinal;
            ResolvedInteractionSource source{};
            if (!ResolveColliderSource(runtimeStatus_, object, componentOrdinal, source, bodyWorldOverride)) {
                return;
            }
            preFixedPhysicsSources_.insert_or_assign(source.source,
                                                     ColliderObservation{source.positionWS, source.footprint});
        });
    });
}

void TerrainInteractionSourceSystem::ProcessPhysicsContacts(World& world, const FrameContext&) {
    const std::span<const PHYSICS::PhysicsContactEvent> events =
        world.FixedEvents().Read<PHYSICS::PhysicsContactEvent>();
    for (const PHYSICS::PhysicsContactEvent& event : events) {
        if (event.isTrigger) {
            continue;
        }
        const bool terrainIsA = event.shapeKeyA.sourceShapeId == PHYSICS::kTerrainCollisionShapeSourceId;
        const bool terrainIsB = event.shapeKeyB.sourceShapeId == PHYSICS::kTerrainCollisionShapeSourceId;
        if (terrainIsA == terrainIsB) {
            continue;
        }
        const RuntimeObjectHandle terrainObject = terrainIsA ? event.objectA : event.objectB;
        const RuntimeObjectHandle sourceObject = terrainIsA ? event.objectB : event.objectA;
        const PHYSICS::PhysicsShapeKey sourceShape = terrainIsA ? event.shapeKeyB : event.shapeKeyA;
        if (sourceShape.componentOrdinal == 0u) {
            continue;
        }

        if (event.phase == PHYSICS::PhysicsContactPhase::End) {
            // A body replacement can emit End after the new definition is
            // already visible in runtime status.  Its signature therefore
            // need not equal the generation that received Begin.  Retire
            // every matching old generation instead of leaking its dent.
            for (auto& [key, active] : activeRigidContacts_) {
                if (key.source.object != sourceObject ||
                    key.source.featureId != kColliderFeatureBase + sourceShape.componentOrdinal ||
                    key.target != terrainObject ||
                    (sourceShape.sourceShapeId != 0u && key.sourceShapeId != sourceShape.sourceShapeId)) {
                    continue;
                }
                if (active.referenceCount > 1u) {
                    --active.referenceCount;
                    continue;
                }
                active.referenceCount = 0u;
                active.endedThisTick = true;
            }
            continue;
        }

        GameObject* terrain = world.FindObject(terrainObject);
        GameObject* sourceObjectPtr = world.FindObject(sourceObject);
        if (terrain == nullptr || sourceObjectPtr == nullptr ||
            terrain->GetComponent<TerrainSurfaceComponent>() == nullptr) {
            continue;
        }
        if (runtimeActivityService_ != nullptr && !runtimeActivityService_->IsActive(sourceObject)) {
            continue;
        }
        ResolvedInteractionSource source{};
        if (!ResolveColliderSource(runtimeStatus_, *sourceObjectPtr, sourceShape.componentOrdinal, source, nullptr,
                                   &sourceShape)) {
            // Static body、disabled collider、trigger collider はここで
            // authoritative に排除し、active contact にも登録しない。
            continue;
        }

        ContactKey key{};
        key.source = source.source;
        key.target = terrainObject;
        key.sourceShapeId = source.sourceShapeId;
        key.definitionSignature = source.definitionSignature;
        ActiveRigidContact& active = activeRigidContacts_[key];
        if (event.phase == PHYSICS::PhysicsContactPhase::Begin) {
            active.endedThisTick = false;
            ++active.referenceCount;
        } else if (active.referenceCount == 0u) {
            active.referenceCount = 1u;
            active.endedThisTick = false;
        }
        active.kinematic = source.motionType == PHYSICS::PhysicsMotionType::Kinematic;
        active.normalWS = terrainIsA ? event.normal : event.normal * -1.0f;
        active.penetrationDepthWS = (std::max)(event.penetrationDepth, 0.0f);
        active.normalImpulseNs = (std::max)(event.impulse, 0.0f);
        active.positionWS = source.positionWS;
        active.footprint = source.footprint;
        active.normalLoadN = source.normalLoadN;
        active.sourceShapeId = source.sourceShapeId;
        active.definitionSignature = source.definitionSignature;
    }
}

void TerrainInteractionSourceSystem::PublishRigidContacts(World& world, const FrameContext& frame) {
    std::vector<ContactKey> finished{};
    for (auto& [key, active] : activeRigidContacts_) {
        GameObject* sourceObject = world.FindObject(key.source.object);
        GameObject* targetObject = world.FindObject(key.target);
        ResolvedInteractionSource resolved{};
        const bool featureValid = key.source.featureId > kColliderFeatureBase;
        const uint64_t ordinal64 = featureValid ? key.source.featureId - kColliderFeatureBase : 0u;
        const PHYSICS::PhysicsShapeKey contactShape{
            key.sourceShapeId,
            ordinal64 <= (std::numeric_limits<uint32_t>::max)() ? static_cast<uint32_t>(ordinal64) : 0u};
        const bool sourceValid =
            sourceObject != nullptr &&
            (runtimeActivityService_ == nullptr || runtimeActivityService_->IsActive(key.source.object)) &&
            ordinal64 > 0u && ordinal64 <= (std::numeric_limits<uint32_t>::max)() &&
            ResolveColliderSource(runtimeStatus_, *sourceObject, static_cast<uint32_t>(ordinal64), resolved, nullptr,
                                  &contactShape);
        const bool targetValid =
            targetObject != nullptr && targetObject->GetComponent<TerrainSurfaceComponent>() != nullptr;
        const bool sourceGenerationValid =
            sourceValid && (active.definitionSignature == 0u || resolved.definitionSignature == 0u ||
                            active.definitionSignature == resolved.definitionSignature);

        bool movedThisStep = false;
        if (sourceGenerationValid) {
            active.kinematic = resolved.motionType == PHYSICS::PhysicsMotionType::Kinematic;
            active.positionWS = resolved.positionWS;
            active.footprint = resolved.footprint;
            active.normalLoadN = resolved.normalLoadN;
            const auto previous = preFixedPhysicsSources_.find(key.source);
            movedThisStep = previous != preFixedPhysicsSources_.end() &&
                            FootprintMoved(previous->second.positionWS, previous->second.footprint, resolved.positionWS,
                                           resolved.footprint);
        }

        const bool canMaintain = active.submitted && !active.endedThisTick && sourceGenerationValid && targetValid;
        const bool canBegin =
            sourceGenerationValid && targetValid && !active.submitted && (!active.kinematic || movedThisStep);
        const float fixedStepSeconds = (std::max)(frame.fixedDt, 0.0f);
        active.stationaryPublishRemainingSeconds =
            (std::max)(0.0f, active.stationaryPublishRemainingSeconds - fixedStepSeconds);
        const bool shouldPublish =
            canBegin || (canMaintain && (movedThisStep || active.stationaryPublishRemainingSeconds <= 0.0f));
        if (shouldPublish) {
            const TERRAIN::TerrainInteractionPhase phase =
                active.submitted ? TERRAIN::TerrainInteractionPhase::Persist : TERRAIN::TerrainInteractionPhase::Begin;
            const float strength = phase == TERRAIN::TerrainInteractionPhase::Begin || movedThisStep ? 1.0f : 0.0f;
            if (phase == TERRAIN::TerrainInteractionPhase::Begin && movedThisStep) {
                const auto previous = preFixedPhysicsSources_.find(key.source);
                if (previous != preFixedPhysicsSources_.end()) {
                    // Dynamic の着地と dormant Kinematic の初動をともに、
                    // PreFixed の始点から現在点まで欠かさず sweep する。
                    previousCenters_.insert_or_assign(key, previous->second.positionWS);
                }
            }
            PublishSweptInteraction(world, frame, key.source, key.target, phase,
                                    TERRAIN::TerrainInteractionSourceKind::RigidBody, active.positionWS,
                                    active.footprint, active.normalLoadN, active.normalWS, active.penetrationDepthWS,
                                    active.normalImpulseNs, strength, active.contactAgeSeconds, active.sourceShapeId,
                                    active.definitionSignature);
            active.submitted = true;
            active.stationaryPublishRemainingSeconds = kStationaryPublishIntervalSeconds;
            // 静止中も strength=0 の Persist で recovery hold だけを
            // 維持する。移動を再開した tick だけ strength=1 に戻す。
        }
        if (canMaintain || canBegin) {
            active.contactAgeSeconds += fixedStepSeconds;
        }
        active.normalImpulseNs = 0.0f;

        if (active.endedThisTick || !sourceGenerationValid || !targetValid) {
            if (active.submitted) {
                PublishSweptInteraction(world, frame, key.source, key.target, TERRAIN::TerrainInteractionPhase::End,
                                        TERRAIN::TerrainInteractionSourceKind::RigidBody, active.positionWS,
                                        active.footprint, active.normalLoadN, active.normalWS, 0.0f, 0.0f, 0.0f,
                                        active.contactAgeSeconds, active.sourceShapeId, active.definitionSignature);
            }
            finished.push_back(key);
        }
    }
    for (const ContactKey& key : finished) {
        activeRigidContacts_.erase(key);
        previousCenters_.erase(key);
    }
}

void TerrainInteractionSourceSystem::PublishCharacterSources(World& world, const FrameContext& frame) {
    std::unordered_set<TERRAIN::TerrainInteractionSourceKey, SourceKeyHash> seen{};
    if (kinematicMotionService_ != nullptr) {
        world.ForEachObjectWith<CharacterLocomotionComponent>([&](GameObject& object,
                                                                  CharacterLocomotionComponent& locomotion) {
            if (!locomotion.IsEnabled()) {
                return;
            }
            const RuntimeObjectHandle objectHandle = object.GetRuntimeHandle();
            if (runtimeActivityService_ != nullptr && !runtimeActivityService_->IsActive(objectHandle)) {
                return;
            }
            const PHYSICS::KinematicMotionState* motion = kinematicMotionService_->FindState(objectHandle);
            if (motion == nullptr || !motion->IsGrounded() || !motion->groundObject.IsValid()) {
                return;
            }
            GameObject* terrainObject = world.FindObject(motion->groundObject);
            if (terrainObject == nullptr || terrainObject->GetComponent<TerrainSurfaceComponent>() == nullptr) {
                return;
            }

            TERRAIN::TerrainInteractionSourceKey source{};
            source.object = objectHandle;
            source.featureId = kCharacterSupportFeature;
            seen.insert(source);

            TERRAIN::TerrainInteractionFootprint footprint{};
            footprint.kind = TERRAIN::TerrainFootprintKind::Ellipse;
            footprint.halfExtentsWS = {0.22f, 0.14f};
            footprint.verticalRadiusWS = 0.08f;
            const MATH::Vec3 forwardWS = TransformVector(object.GetTransform().GetWorldMatrix(), {0.0f, 0.0f, 1.0f});
            footprint.tangentWS = PlanarTangent({motion->velocity.x, 0.0f, motion->velocity.z}, forwardWS);

            float normalLoadN = 0.0f;
            const PhysicsBodyComponent* body = object.GetComponent<PhysicsBodyComponent>();
            if (body != nullptr && body->IsEnabled()) {
                normalLoadN = (std::max)(body->GetMass(), 0.0f) * std::fabs(body->GetGravityScale()) * 9.81f;
            }

            auto activeIt = activeCharacterContacts_.find(source);
            if (activeIt != activeCharacterContacts_.end() && activeIt->second.target != motion->groundObject) {
                PublishSweptInteraction(
                    world, frame, source, activeIt->second.target, TERRAIN::TerrainInteractionPhase::End,
                    TERRAIN::TerrainInteractionSourceKind::CharacterFoot, activeIt->second.positionWS,
                    activeIt->second.footprint, activeIt->second.normalLoadN, activeIt->second.normalWS, 0.0f, 0.0f,
                    0.0f, activeIt->second.contactAgeSeconds);
                activeCharacterContacts_.erase(activeIt);
            }

            const auto [insertedIt, inserted] = activeCharacterContacts_.try_emplace(
                source, ActiveCharacterContact{motion->groundObject, motion->groundPosition, motion->groundNormal,
                                               footprint, normalLoadN, 0.0f, 0.0f});
            ActiveCharacterContact& active = insertedIt->second;
            const float tangentAlignment = std::fabs(MATH::Dot(active.footprint.tangentWS, footprint.tangentWS));
            const MATH::Vec3 horizontalVelocity{motion->velocity.x, 0.0f, motion->velocity.z};
            const bool movingThisStep = MATH::Length(horizontalVelocity) > 1.0e-3f || tangentAlignment < 0.99999f;
            active.target = motion->groundObject;
            active.positionWS = motion->groundPosition;
            active.normalWS = motion->groundNormal;
            active.footprint = footprint;
            active.normalLoadN = normalLoadN;
            const float fixedStepSeconds = (std::max)(frame.fixedDt, 0.0f);
            active.stationaryPublishRemainingSeconds =
                (std::max)(0.0f, active.stationaryPublishRemainingSeconds - fixedStepSeconds);
            if (inserted || movingThisStep || active.stationaryPublishRemainingSeconds <= 0.0f) {
                PublishSweptInteraction(world, frame, source, active.target,
                                        inserted ? TERRAIN::TerrainInteractionPhase::Begin
                                                 : TERRAIN::TerrainInteractionPhase::Persist,
                                        TERRAIN::TerrainInteractionSourceKind::CharacterFoot, active.positionWS,
                                        active.footprint, active.normalLoadN, active.normalWS, 0.0f, 0.0f,
                                        inserted || movingThisStep ? 1.0f : 0.0f, active.contactAgeSeconds);
                active.stationaryPublishRemainingSeconds = kStationaryPublishIntervalSeconds;
            }
            active.contactAgeSeconds += fixedStepSeconds;
        });
    }

    for (auto it = activeCharacterContacts_.begin(); it != activeCharacterContacts_.end();) {
        if (!seen.contains(it->first)) {
            PublishSweptInteraction(world, frame, it->first, it->second.target, TERRAIN::TerrainInteractionPhase::End,
                                    TERRAIN::TerrainInteractionSourceKind::CharacterFoot, it->second.positionWS,
                                    it->second.footprint, it->second.normalLoadN, it->second.normalWS, 0.0f, 0.0f, 0.0f,
                                    it->second.contactAgeSeconds);
            it = activeCharacterContacts_.erase(it);
        } else {
            ++it;
        }
    }
}

void TerrainInteractionSourceSystem::PublishEditorPreviewSources(World& world, const FrameContext& frame) {
    std::unordered_set<TERRAIN::TerrainInteractionSourceKey, SourceKeyHash> seen{};
    WorldEventStream& events = world.FrameEvents();
    const float stepSeconds = std::clamp(frame.unscaledDt, 0.0f, 0.1f);

    world.ForEachObjectWith<PhysicsBodyComponent>([&](GameObject& object, PhysicsBodyComponent&) {
        if (ResolveMotionType(object, runtimeStatus_) == PHYSICS::PhysicsMotionType::Static) {
            return;
        }
        uint32_t componentOrdinal = 0u;
        object.ForEachComponent<ColliderComponent>([&](const ColliderComponent&) {
            ++componentOrdinal;
            ResolvedInteractionSource resolved{};
            if (!ResolveColliderSource(runtimeStatus_, object, componentOrdinal, resolved)) {
                return;
            }
            seen.insert(resolved.source);

            RuntimeObjectHandle bestTarget{};
            MATH::Vec3 bestNormal{0.0f, 1.0f, 0.0f};
            float bestPenetration = 0.0f;
            float bestScore = (std::numeric_limits<float>::max)();
            const float verticalRadius = resolved.footprint.verticalRadiusWS;
            world.ForEachObjectWith<TerrainSurfaceComponent>([&](GameObject& terrainObject,
                                                                 TerrainSurfaceComponent& terrain) {
                if (&terrainObject == &object) {
                    return;
                }
                float surfaceY = 0.0f;
                MATH::Vec3 normal{};
                if (!terrain.SampleWorldHeight(resolved.positionWS, surfaceY, normal)) {
                    return;
                }
                const float gap = resolved.positionWS.y - verticalRadius - surfaceY;
                const float contactTolerance = verticalRadius * 0.10f;
                if (gap > contactTolerance || resolved.positionWS.y + verticalRadius < surfaceY - contactTolerance) {
                    return;
                }
                const float score = std::fabs(gap);
                if (score >= bestScore) {
                    return;
                }
                bestScore = score;
                bestTarget = terrainObject.GetRuntimeHandle();
                bestNormal = normal;
                bestPenetration = (std::max)(-gap, 0.0f);
            });

            auto activeIt = editorPreviewContacts_.find(resolved.source);
            if (activeIt != editorPreviewContacts_.end() && activeIt->second.target != bestTarget) {
                EditorPreviewContact& previous = activeIt->second;
                if (previous.submitted) {
                    PublishSweptInteraction(events, stepSeconds, events.GetScopeIndex(), resolved.source,
                                            previous.target, TERRAIN::TerrainInteractionPhase::End,
                                            TERRAIN::TerrainInteractionSourceKind::RigidBody, previous.positionWS,
                                            previous.footprint, previous.normalLoadN, previous.normalWS, 0.0f, 0.0f,
                                            0.0f, previous.contactAgeSeconds);
                }
                previousCenters_.erase(ContactKey{resolved.source, previous.target});
                editorPreviewContacts_.erase(activeIt);
                activeIt = editorPreviewContacts_.end();
            }
            if (!bestTarget.IsValid()) {
                return;
            }

            const auto [insertedIt, inserted] = editorPreviewContacts_.try_emplace(
                resolved.source, EditorPreviewContact{bestTarget, resolved.positionWS, bestNormal, resolved.footprint,
                                                      resolved.normalLoadN, bestPenetration, 0.0f, false});
            EditorPreviewContact& active = insertedIt->second;
            const MATH::Vec3 previousPosition = active.positionWS;
            const bool movedThisFrame = !inserted && FootprintMoved(active.positionWS, active.footprint,
                                                                    resolved.positionWS, resolved.footprint);
            active.target = bestTarget;
            active.positionWS = resolved.positionWS;
            active.normalWS = bestNormal;
            active.footprint = resolved.footprint;
            active.normalLoadN = resolved.normalLoadN;
            active.penetrationDepthWS = bestPenetration;
            if (!active.submitted && !movedThisFrame) {
                // Edit mode は観測しただけの Dynamic/Kinematic を
                // 勝手に stamp しない。実際の authoring 移動から開始する。
                return;
            }
            if (!active.submitted) {
                previousCenters_.insert_or_assign(ContactKey{resolved.source, bestTarget}, previousPosition);
            }
            PublishSweptInteraction(events, stepSeconds, events.GetScopeIndex(), resolved.source, bestTarget,
                                    active.submitted ? TERRAIN::TerrainInteractionPhase::Persist
                                                     : TERRAIN::TerrainInteractionPhase::Begin,
                                    TERRAIN::TerrainInteractionSourceKind::RigidBody, active.positionWS,
                                    active.footprint, active.normalLoadN, active.normalWS, active.penetrationDepthWS,
                                    0.0f, movedThisFrame ? 1.0f : 0.0f, active.contactAgeSeconds);
            active.submitted = true;
            active.contactAgeSeconds += stepSeconds;
        });
    });

    for (auto it = editorPreviewContacts_.begin(); it != editorPreviewContacts_.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        if (it->second.submitted) {
            PublishSweptInteraction(events, stepSeconds, events.GetScopeIndex(), it->first, it->second.target,
                                    TERRAIN::TerrainInteractionPhase::End,
                                    TERRAIN::TerrainInteractionSourceKind::RigidBody, it->second.positionWS,
                                    it->second.footprint, it->second.normalLoadN, it->second.normalWS, 0.0f, 0.0f, 0.0f,
                                    it->second.contactAgeSeconds);
        }
        previousCenters_.erase(ContactKey{it->first, it->second.target});
        it = editorPreviewContacts_.erase(it);
    }
}

void TerrainInteractionSourceSystem::PublishFollowerSources(World& world, const FrameContext& frame) {
    std::unordered_set<TERRAIN::TerrainInteractionSourceKey, SourceKeyHash> seen{};
    world.ForEachObjectWith<TerrainSurfaceFollowerComponent>([&](GameObject& object,
                                                                 TerrainSurfaceFollowerComponent& follower) {
        if (runtimeActivityService_ != nullptr && !runtimeActivityService_->IsActive(object.GetRuntimeHandle())) {
            return;
        }
        // Physics-driven Dynamic/Kinematic collider は contact adapter が
        // 唯一の producer。Follower compatibility と二重発行しない。
        if (SuppressFollowerCompatibility(runtimeStatus_, object)) {
            return;
        }
        TERRAIN::TerrainInteractionSourceKey source{};
        source.object = object.GetRuntimeHandle();
        source.featureId = kFollowerFeature;

        const MATH::Mat4 sourceWorld = object.GetTransform().GetWorldMatrix();
        MATH::Vec3 translation{};
        MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
        MATH::Quat rotation{};
        if (!MATH::DecomposeTRS(sourceWorld, translation, rotation, scale)) {
            return;
        }
        seen.insert(source);
        const MATH::Vec3 center = TransformPoint(sourceWorld, {});
        const float radius = std::fabs(follower.ResolveRadius()) *
                             (std::max)({std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z)});
        if (!(radius > 1.0e-8f) || !std::isfinite(radius)) {
            return;
        }
        TERRAIN::TerrainInteractionFootprint footprint{};
        footprint.kind = TERRAIN::TerrainFootprintKind::Sphere;
        footprint.halfExtentsWS = {radius, radius};
        footprint.verticalRadiusWS = radius;

        RuntimeObjectHandle bestTarget{};
        MATH::Vec3 bestNormal{0.0f, 1.0f, 0.0f};
        float bestPenetration = 0.0f;
        float bestScore = (std::numeric_limits<float>::max)();
        world.ForEachObjectWith<TerrainSurfaceComponent>(
            [&](GameObject& terrainObject, TerrainSurfaceComponent& terrain) {
                float surfaceY = 0.0f;
                MATH::Vec3 normal{};
                if (!terrain.SampleWorldHeight(center, surfaceY, normal)) {
                    return;
                }
                const float gap = center.y - radius - surfaceY;
                if (gap > radius * 0.08f || center.y + radius < surfaceY) {
                    return;
                }
                const float score = std::fabs(gap);
                if (score >= bestScore) {
                    return;
                }
                bestScore = score;
                bestTarget = terrainObject.GetRuntimeHandle();
                bestNormal = normal;
                bestPenetration = (std::max)(-gap, 0.0f);
            });

        const auto previousTarget = activeFollowerTargets_.find(source);
        if (previousTarget != activeFollowerTargets_.end() && previousTarget->second.target != bestTarget) {
            if (previousTarget->second.submitted) {
                PublishSweptInteraction(world, frame, source, previousTarget->second.target,
                                        TERRAIN::TerrainInteractionPhase::End,
                                        TERRAIN::TerrainInteractionSourceKind::FollowerCompatibility,
                                        previousTarget->second.positionWS, previousTarget->second.footprint, 0.0f,
                                        {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f, 0.0f, previousTarget->second.contactAgeSeconds);
            }
            activeFollowerTargets_.erase(previousTarget);
        }
        if (!bestTarget.IsValid()) {
            return;
        }
        const auto [activeIt, inserted] = activeFollowerTargets_.try_emplace(
            source, ActiveFollowerContact{bestTarget, center, footprint, 0.0f, 0.0f, false});
        ActiveFollowerContact& active = activeIt->second;
        const MATH::Vec3 previousPosition = active.positionWS;
        const bool movedThisStep = !inserted && FootprintMoved(active.positionWS, active.footprint, center, footprint);
        active.target = bestTarget;
        active.positionWS = center;
        active.footprint = footprint;
        if (!active.submitted && !movedThisStep) {
            // Compatibility source は最初の静止観測だけでは接触を開始しない。
            // terrain 上で実際に動いた時点から自動 interaction に昇格する。
            return;
        }
        if (!active.submitted) {
            previousCenters_.insert_or_assign(ContactKey{source, bestTarget}, previousPosition);
        }
        const float fixedStepSeconds = (std::max)(frame.fixedDt, 0.0f);
        active.stationaryPublishRemainingSeconds =
            (std::max)(0.0f, active.stationaryPublishRemainingSeconds - fixedStepSeconds);
        if (!active.submitted || movedThisStep || active.stationaryPublishRemainingSeconds <= 0.0f) {
            PublishSweptInteraction(
                world, frame, source, bestTarget,
                active.submitted ? TERRAIN::TerrainInteractionPhase::Persist : TERRAIN::TerrainInteractionPhase::Begin,
                TERRAIN::TerrainInteractionSourceKind::FollowerCompatibility, center, footprint, 0.0f, bestNormal,
                bestPenetration, 0.0f, movedThisStep ? 1.0f : 0.0f, active.contactAgeSeconds);
            active.submitted = true;
            active.stationaryPublishRemainingSeconds = kStationaryPublishIntervalSeconds;
        }
        active.contactAgeSeconds += fixedStepSeconds;
    });

    for (auto it = activeFollowerTargets_.begin(); it != activeFollowerTargets_.end();) {
        if (!seen.contains(it->first)) {
            if (it->second.submitted) {
                PublishSweptInteraction(
                    world, frame, it->first, it->second.target, TERRAIN::TerrainInteractionPhase::End,
                    TERRAIN::TerrainInteractionSourceKind::FollowerCompatibility, it->second.positionWS,
                    it->second.footprint, 0.0f, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f, 0.0f, it->second.contactAgeSeconds);
            }
            previousCenters_.erase(ContactKey{it->first, it->second.target});
            it = activeFollowerTargets_.erase(it);
        } else {
            ++it;
        }
    }
}

void TerrainInteractionSourceSystem::PublishSweptInteraction(
    World& world, const FrameContext& frame, const TERRAIN::TerrainInteractionSourceKey& source,
    RuntimeObjectHandle targetSurface, TERRAIN::TerrainInteractionPhase phase,
    TERRAIN::TerrainInteractionSourceKind sourceKind, const MATH::Vec3& positionWS,
    const TERRAIN::TerrainInteractionFootprint& footprint, float normalLoadN, const MATH::Vec3& normalWS,
    float penetrationDepthWS, float normalImpulseNs, float strength, float contactAgeSeconds, uint64_t sourceShapeId,
    uint64_t definitionSignature) {
    PublishSweptInteraction(world.FixedEvents(), frame.fixedDt, frame.fixedTickIndex, source, targetSurface, phase,
                            sourceKind, positionWS, footprint, normalLoadN, normalWS, penetrationDepthWS,
                            normalImpulseNs, strength, contactAgeSeconds, sourceShapeId, definitionSignature);
}

void TerrainInteractionSourceSystem::PublishSweptInteraction(
    WorldEventStream& events, float stepSeconds, uint64_t scopeIndex,
    const TERRAIN::TerrainInteractionSourceKey& source, RuntimeObjectHandle targetSurface,
    TERRAIN::TerrainInteractionPhase phase, TERRAIN::TerrainInteractionSourceKind sourceKind,
    const MATH::Vec3& positionWS, const TERRAIN::TerrainInteractionFootprint& footprint, float normalLoadN,
    const MATH::Vec3& normalWS, float penetrationDepthWS, float normalImpulseNs, float strength,
    float contactAgeSeconds, uint64_t sourceShapeId, uint64_t definitionSignature) {
    const ContactKey contactKey{source, targetSurface, sourceShapeId, definitionSignature};
    const bool allowAutomaticSettling = sourceKind == TERRAIN::TerrainInteractionSourceKind::RigidBody ||
                                        sourceKind == TERRAIN::TerrainInteractionSourceKind::CharacterFoot ||
                                        sourceKind == TERRAIN::TerrainInteractionSourceKind::FollowerCompatibility;
    if (phase == TERRAIN::TerrainInteractionPhase::End) {
        TERRAIN::TerrainInteractionSample sample{};
        sample.source = source;
        sample.targetSurface = targetSurface;
        sample.phase = phase;
        sample.sourceKind = sourceKind;
        sample.positionWS = positionWS;
        sample.normalWS = normalWS;
        sample.footprint = footprint;
        sample.strength = 0.0f;
        sample.allowSettling = false;
        sample.contactAgeSeconds = contactAgeSeconds;
        sample.fixedTickIndex = scopeIndex;
        events.Publish(TERRAIN::SanitizeTerrainInteractionSample(sample));
        previousCenters_.erase(contactKey);
        return;
    }

    const auto previousIt = previousCenters_.find(contactKey);
    const bool hasPrevious = previousIt != previousCenters_.end();
    const MATH::Vec3 previous = hasPrevious ? previousIt->second : positionWS;
    previousCenters_.insert_or_assign(contactKey, positionWS);

    const MATH::Vec3 observedDelta = positionWS - previous;
    const float observedTravel = std::sqrt(MATH::Dot(observedDelta, observedDelta));
    const float footprintRadius =
        (std::max)({footprint.halfExtentsWS.x, footprint.halfExtentsWS.y, footprint.verticalRadiusWS});
    const TERRAIN::TerrainInteractionSweepPlan sweepPlan =
        TERRAIN::PlanTerrainInteractionSweep(hasPrevious ? observedTravel : 0.0f, footprintRadius, stepSeconds);
    // discontinuity は現在点を 1 回だけ発行し、teleport 経路全体を
    // terrain stamp へ変換しない。previousCenters_ は既に現在点へ進めた
    // ため、次 tick は通常の連続 sweep へ戻る。
    const MATH::Vec3 sweepStart = sweepPlan.discontinuity ? positionWS : previous;
    const MATH::Vec3 delta = positionWS - sweepStart;
    const uint32_t steps = sweepPlan.sampleCount;
    const MATH::Vec3 velocity = stepSeconds > 1.0e-6f ? delta * (1.0f / stepSeconds) : MATH::Vec3{};
    const float sampleSeconds = (std::max)(stepSeconds, 0.0f) / static_cast<float>(steps);
    const float sampleImpulse = normalImpulseNs / static_cast<float>(steps);

    // 静止時と discontinuity は現在点を 1 回だけ発行する。通常移動も
    // previous を再発行せず、(previous,current] の bounded sample だけを
    // 出して重複 stamp と 1 tick の無制限 fan-out を防ぐ。
    for (uint32_t step = 1u; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        TERRAIN::TerrainInteractionSample sample{};
        sample.source = source;
        sample.targetSurface = targetSurface;
        sample.sourceKind = sourceKind;
        sample.phase = step == 1u ? phase : TERRAIN::TerrainInteractionPhase::Persist;
        sample.positionWS = sweepStart + delta * t;
        sample.normalWS = normalWS;
        sample.relativeVelocityWS = velocity;
        sample.footprint = footprint;
        sample.penetrationDepthWS = penetrationDepthWS;
        sample.normalImpulseNs = sampleImpulse;
        sample.normalLoadN = normalLoadN;
        sample.strength = strength;
        sample.allowSettling = allowAutomaticSettling;
        sample.contactAgeSeconds = contactAgeSeconds + static_cast<float>(step - 1u) * sampleSeconds;
        sample.contactSeconds = sampleSeconds;
        sample.fixedTickIndex = scopeIndex;
        events.Publish(TERRAIN::SanitizeTerrainInteractionSample(sample));
    }
}

} // namespace HIKARI
