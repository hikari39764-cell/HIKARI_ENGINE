#include "Scene/Features/HIKARI_BuoyancySystem.h"
#include "Scene/Water/HIKARI_WorldWater.h"

#include <algorithm>

#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Features/HIKARI_BuoyancyComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

namespace HIKARI {

namespace {
constexpr float kGravity = 9.81f;
constexpr float kPi = 3.14159265358979323846f;
} // namespace

void BuoyancySystem::OnWorldAttached(World& world) {
    waterQuery_ = world.Services().Find<WATER::WaterSurfaceQueryService>();
    physicsWorld_ = world.Services().Find<PHYSICS::PhysicsWorldService>();
    physicsStatus_ = world.Services().Find<PHYSICS::PhysicsRuntimeStatusService>();
}

void BuoyancySystem::OnWorldDetached(World&) {
    waterQuery_ = nullptr;
    physicsWorld_ = nullptr;
    physicsStatus_ = nullptr;
}

void BuoyancySystem::PreFixedUpdate(World& world, const FrameContext& frame) {
    (void)frame;
    if (waterQuery_ == nullptr || physicsWorld_ == nullptr || physicsStatus_ == nullptr) {
        return;
    }

    world.ForEachObjectWith<BuoyancyComponent, ColliderComponent>(
        WorldComponentQueryAnchor::FirstRequiredType,
        [&](GameObject& object, BuoyancyComponent& buoyancy, ColliderComponent& collider) {
            (void)collider;
            const PHYSICS::PhysicsBodyRuntimeStatus* status = physicsStatus_->FindBodyStatus(object.GetRuntimeHandle());
            if (status == nullptr || !status->body.IsValid()) {
                return;
            }

            const MATH::Vec3 center = object.GetTransform().position;
            WATER::WaterSurfaceQuery query{};
            query.positionWS = center;
            WATER::WaterSurfaceSample sample{};
            if (!WATER::SampleWorldWater(world, query, sample) || !sample.valid) {
                return;
            }

            const float radius = buoyancy.GetRadius();
            const float submerged = (std::min)((std::max)(sample.submergenceDepth + radius, 0.0f), 2.0f * radius);
            if (submerged <= 1.0e-5f) {
                return;
            }
            const float volume = kPi * submerged * submerged * (3.0f * radius - submerged) / 3.0f;
            const float magnitude = buoyancy.GetDensity() * kGravity * volume;
            physicsWorld_->AddForce(status->body, sample.normalWS * magnitude);
        });
}

} // namespace HIKARI
