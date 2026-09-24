#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

namespace WATER {
class WaterSurfaceQueryService;
}
namespace PHYSICS {
class PhysicsWorldService;
class PhysicsRuntimeStatusService;
} // namespace PHYSICS

// 水样から球の排水体積を求め、PhysicsWorldService 経由で浮力を加える。
class BuoyancySystem final : public ISystem {
public:
    std::string_view GetName() const override { return "BuoyancySystem"; }

    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    void PreFixedUpdate(World& world, const FrameContext& frame) override;

private:
    WATER::WaterSurfaceQueryService* waterQuery_ = nullptr;
    PHYSICS::PhysicsWorldService* physicsWorld_ = nullptr;
    PHYSICS::PhysicsRuntimeStatusService* physicsStatus_ = nullptr;
};

} // namespace HIKARI
