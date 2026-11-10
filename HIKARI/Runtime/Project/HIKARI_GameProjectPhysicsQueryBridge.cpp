#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Physics/HIKARI_PhysicsTypes.h"
#include "HIKARI/Project/WorldView.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace HIKARI {
using namespace PROJECT::INTERNAL;
namespace {
bool ToPhysicsFilter(World& world, const GameProjectPhysicsQueryFilter& source,
                     PHYSICS::PhysicsQueryFilter& outFilter) noexcept {
    outFilter = {};
    outFilter.layerMask = source.layerMask;
    outFilter.includeTriggers = source.includeTriggers != 0u;
    if (source.ignoredObjectHandle == 0u) {
        return true;
    }
    const RuntimeObjectHandle ignored = RuntimeObjectHandle::FromValue(source.ignoredObjectHandle);
    if (!ignored.IsValid() || world.FindObject(ignored) == nullptr) {
        return false;
    }
    outFilter.ignoredObject = ignored;
    return true;
}

bool ToPhysicsShape(const GameProjectPhysicsShape& source, PHYSICS::PhysicsShapeDesc& outShape) noexcept {
    if (!IsFinite(source.center) || !IsFinite(source.rotation)) {
        return false;
    }
    if (!IsFiniteNonZeroRotation(source.rotation)) {
        return false;
    }
    outShape = {};
    outShape.localCenter = ToRuntime(source.center);
    outShape.localRotation = ToRuntime(source.rotation);
    switch (source.type) {
    case GameProjectPhysicsShapeType::Box:
        if (!IsFinite(source.halfExtents) || source.halfExtents.x <= 0.0f || source.halfExtents.y <= 0.0f ||
            source.halfExtents.z <= 0.0f) {
            return false;
        }
        outShape.type = PHYSICS::PhysicsShapeType::Box;
        outShape.halfExtents = ToRuntime(source.halfExtents);
        break;
    case GameProjectPhysicsShapeType::Sphere:
        if (!IsFinitePositive(source.radius)) {
            return false;
        }
        outShape.type = PHYSICS::PhysicsShapeType::Sphere;
        outShape.radius = source.radius;
        break;
    case GameProjectPhysicsShapeType::Capsule:
        if (!IsFinitePositive(source.radius) || !IsFinitePositive(source.height)) {
            return false;
        }
        outShape.type = PHYSICS::PhysicsShapeType::Capsule;
        outShape.radius = source.radius;
        outShape.height = source.height;
        break;
    default: return false;
    }
    outShape.isTrigger = source.isTrigger != 0u;
    return true;
}

GameProjectPhysicsHit ToProjectHit(const PHYSICS::PhysicsHit& source) noexcept {
    GameProjectPhysicsHit result{};
    result.objectHandle = source.object.ToValue();
    result.bodyHandle = source.body.ToValue();
    result.sourceShapeId = source.shapeKey.sourceShapeId;
    result.componentOrdinal = source.shapeKey.componentOrdinal;
    result.position = FiniteOr(source.position);
    result.normal = FiniteOr(source.normal);
    result.distance = std::isfinite(source.distance) ? source.distance : 0.0f;
    result.fraction = std::isfinite(source.fraction) ? source.fraction : 0.0f;
    result.shapeIndex = source.shapeIndex;
    result.isTrigger = source.isTrigger ? 1u : 0u;
    return result;
}
} // namespace

PhysicsQueryView GameProjectSdkAccess::MakePhysicsQueryView(World& world) noexcept { return PhysicsQueryView(&world); }

PhysicsQueryView WorldView::GetPhysicsQueries() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakePhysicsQueryView(*world) : PhysicsQueryView{};
}

bool PhysicsQueryView::Raycast(const GameProjectPhysicsRaycastQuery& query,
                               GameProjectPhysicsHit& outHit) const noexcept {
    outHit = {};
    try {
        if (world_ == nullptr || !IsFinite(query.origin) || !IsFiniteNonZeroDirection(query.direction) ||
            !IsFinitePositive(query.maxDistance)) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        PHYSICS::PhysicsWorldService* service = world->Services().Find<PHYSICS::PhysicsWorldService>();
        if (service == nullptr) {
            return false;
        }
        PHYSICS::PhysicsRaycastQuery runtimeQuery{};
        runtimeQuery.origin = ToRuntime(query.origin);
        runtimeQuery.direction = ToRuntime(query.direction);
        runtimeQuery.maxDistance = query.maxDistance;
        if (!ToPhysicsFilter(*world, query.filter, runtimeQuery.filter)) {
            return false;
        }
        PHYSICS::PhysicsHit hit{};
        if (!service->Raycast(runtimeQuery, hit)) {
            return false;
        }
        outHit = ToProjectHit(hit);
        return true;
    } catch (...) {
        outHit = {};
        return false;
    }
}

bool PhysicsQueryView::ShapeCast(const GameProjectPhysicsShapeCastQuery& query,
                                 GameProjectPhysicsHit& outHit) const noexcept {
    outHit = {};
    try {
        if (world_ == nullptr || !IsFinite(query.startPosition) || !IsFinite(query.startRotation) ||
            !IsFiniteNonZeroRotation(query.startRotation) || !IsFiniteNonZeroDirection(query.direction) ||
            !IsFinitePositive(query.maxDistance)) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        PHYSICS::PhysicsWorldService* service = world->Services().Find<PHYSICS::PhysicsWorldService>();
        if (service == nullptr) {
            return false;
        }
        PHYSICS::PhysicsShapeCastQuery runtimeQuery{};
        if (!ToPhysicsShape(query.shape, runtimeQuery.shape) ||
            !ToPhysicsFilter(*world, query.filter, runtimeQuery.filter)) {
            return false;
        }
        runtimeQuery.startPose.position = ToRuntime(query.startPosition);
        runtimeQuery.startPose.rotation = ToRuntime(query.startRotation);
        runtimeQuery.direction = ToRuntime(query.direction);
        runtimeQuery.maxDistance = query.maxDistance;
        PHYSICS::PhysicsHit hit{};
        if (!service->ShapeCast(runtimeQuery, hit)) {
            return false;
        }
        outHit = ToProjectHit(hit);
        return true;
    } catch (...) {
        outHit = {};
        return false;
    }
}

uint32_t PhysicsQueryView::Overlap(const GameProjectPhysicsOverlapQuery& query, GameProjectPhysicsHit* outHits,
                                   uint32_t capacity) const noexcept {
    try {
        if (world_ == nullptr || outHits == nullptr || capacity == 0u || !IsFinite(query.position) ||
            !IsFinite(query.rotation) || !IsFiniteNonZeroRotation(query.rotation)) {
            return 0u;
        }
        World* world = static_cast<World*>(world_);
        PHYSICS::PhysicsWorldService* service = world->Services().Find<PHYSICS::PhysicsWorldService>();
        if (service == nullptr) {
            return 0u;
        }
        PHYSICS::PhysicsOverlapQuery runtimeQuery{};
        if (!ToPhysicsShape(query.shape, runtimeQuery.shape) ||
            !ToPhysicsFilter(*world, query.filter, runtimeQuery.filter)) {
            return 0u;
        }
        runtimeQuery.pose.position = ToRuntime(query.position);
        runtimeQuery.pose.rotation = ToRuntime(query.rotation);
        const std::vector<PHYSICS::PhysicsHit> hits = service->Overlap(runtimeQuery);
        const size_t count = (std::min)(static_cast<size_t>(capacity), hits.size());
        for (size_t index = 0u; index < count; ++index) {
            outHits[index] = ToProjectHit(hits[index]);
        }
        return static_cast<uint32_t>(count);
    } catch (...) {
        return 0u;
    }
}

uint32_t PhysicsQueryView::ReadTriggerEvents(GameProjectPhysicsTriggerEvent* outEvents,
                                             uint32_t capacity) const noexcept {
    try {
        if (world_ == nullptr || outEvents == nullptr || capacity == 0u) {
            return 0u;
        }
        const auto* world = static_cast<const World*>(world_);
        const auto events = world->FixedEvents().Read<PHYSICS::PhysicsContactEvent>();
        uint32_t written = 0u;
        for (const PHYSICS::PhysicsContactEvent& event : events) {
            if (!event.isTrigger || written >= capacity) {
                continue;
            }
            GameProjectPhysicsTriggerEvent result{};
            result.objectA = event.objectA.ToValue();
            result.objectB = event.objectB.ToValue();
            result.bodyA = event.bodyA.ToValue();
            result.bodyB = event.bodyB.ToValue();
            result.sourceShapeIdA = event.shapeKeyA.sourceShapeId;
            result.sourceShapeIdB = event.shapeKeyB.sourceShapeId;
            result.position = FiniteOr(event.position);
            result.normal = FiniteOr(event.normal);
            result.penetrationDepth = std::isfinite(event.penetrationDepth) ? event.penetrationDepth : 0.0f;
            result.impulse = std::isfinite(event.impulse) ? event.impulse : 0.0f;
            result.shapeIndexA = event.shapeIndexA;
            result.shapeIndexB = event.shapeIndexB;
            result.componentOrdinalA = event.shapeKeyA.componentOrdinal;
            result.componentOrdinalB = event.shapeKeyB.componentOrdinal;
            switch (event.phase) {
            case PHYSICS::PhysicsContactPhase::Persist: result.phase = GameProjectPhysicsContactPhase::Persist; break;
            case PHYSICS::PhysicsContactPhase::End: result.phase = GameProjectPhysicsContactPhase::End; break;
            case PHYSICS::PhysicsContactPhase::Begin:
            default: result.phase = GameProjectPhysicsContactPhase::Begin; break;
            }
            outEvents[written++] = result;
        }
        return written;
    } catch (...) {
        return 0u;
    }
}
uint64_t WorldView::AddStaticHeightField(GameProjectObjectHandle owner, float originX, float originZ,
                                         float spacingMeters, uint32_t sampleCount,
                                         std::span<const float> heights) const noexcept {
    try {
        const World* world = GameProjectSdkAccess::GetWorld(*this);
        PHYSICS::PhysicsWorldService* service =
            world != nullptr ? const_cast<World*>(world)->Services().Find<PHYSICS::PhysicsWorldService>() : nullptr;
        if (service == nullptr || sampleCount < 3u || spacingMeters <= 0.0f ||
            heights.size() != static_cast<size_t>(sampleCount) * sampleCount) {
            return 0u;
        }
        PHYSICS::PhysicsBodyCreateInfo info{};
        info.body.object = RuntimeObjectHandle::FromValue(owner.value);
        info.body.motionType = PHYSICS::PhysicsMotionType::Static;
        PHYSICS::PhysicsShapeDesc shape{};
        shape.type = PHYSICS::PhysicsShapeType::HeightField;
        shape.localCenter = {originX, 0.0f, originZ};
        shape.heightSamples = std::make_shared<const std::vector<float>>(heights.begin(), heights.end());
        shape.heightSampleCount = sampleCount;
        shape.heightSampleSpacing = spacingMeters;
        shape.material = PHYSICS::PhysicsMaterialDesc{0.8f, 0.0f, 1.0f};
        info.shapes.push_back(std::move(shape));
        const PHYSICS::PhysicsBodyCreateResult created = service->CreateBody(info);
        return created.Succeeded() ? created.handle.ToValue() : 0u;
    } catch (...) {
        return 0u;
    }
}

void WorldView::RemoveStaticHeightField(uint64_t body) const noexcept {
    try {
        const World* world = GameProjectSdkAccess::GetWorld(*this);
        PHYSICS::PhysicsWorldService* service =
            world != nullptr ? const_cast<World*>(world)->Services().Find<PHYSICS::PhysicsWorldService>() : nullptr;
        if (service != nullptr && body != 0u) {
            (void)service->DestroyBody(PHYSICS::PhysicsBodyHandle::FromValue(body));
        }
    } catch (...) {
    }
}

} // namespace HIKARI
