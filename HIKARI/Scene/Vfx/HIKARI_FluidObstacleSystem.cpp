#include "Scene/Vfx/HIKARI_FluidObstacleSystem.h"

#include <algorithm>

#include "Core/HIKARI_MathConfig.h"
#include "HIKARI/Project/FrameContext.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_FluidObstacleComponent.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Vfx/HIKARI_FluidInteractionService.h"
#include "Scene/Vfx/HIKARI_VfxAttachmentResolver.h"

namespace HIKARI {
namespace {
using RENDER3D::FLUID::FluidInteraction;
using RENDER3D::FLUID::FluidInteractionKind;
using RENDER3D::FLUID::FluidInteractionShape;

ResolvedColliderShape ToColliderShape(const FluidObstacleShape& authored) {
    ResolvedColliderShape shape{};
    shape.type = authored.shape == FluidObstacleShapeType::Sphere    ? PHYSICS::PhysicsShapeType::Sphere
                 : authored.shape == FluidObstacleShapeType::Capsule ? PHYSICS::PhysicsShapeType::Capsule
                                                                     : PHYSICS::PhysicsShapeType::Box;
    shape.center = authored.center;
    shape.rotationEulerDegrees = authored.rotation;
    shape.size = authored.size;
    shape.radius = authored.radius;
    shape.height = (std::max)(authored.height, 2.0f * authored.radius);
    return shape;
}

// 付け先の局所の形を、付け先の拡縮を掛けて世界へ置く。
FluidInteraction Place(const ResolvedColliderShape& local, const VFX::VfxWorldTransform& anchor) {
    const ResolvedColliderShape shape = ScaleColliderShape(local, anchor.scale);
    const MATH::Vec3& degrees = shape.rotationEulerDegrees;
    FluidInteraction interaction{};
    interaction.kind = FluidInteractionKind::Obstacle;
    interaction.worldPosition = anchor.position + MATH::RotateVector(anchor.rotation, shape.center);
    interaction.worldRotation =
        MATH::MultiplyQ(anchor.rotation, MATH::Quat::FromEulerXYZ(degrees.x * MATH::kDegreesToRadians,
                                                                  degrees.y * MATH::kDegreesToRadians,
                                                                  degrees.z * MATH::kDegreesToRadians));
    switch (shape.type) {
    case PHYSICS::PhysicsShapeType::Sphere:
        interaction.shape = FluidInteractionShape::Sphere;
        interaction.extents = {shape.radius, shape.radius, shape.radius};
        break;
    case PHYSICS::PhysicsShapeType::Capsule:
        interaction.shape = FluidInteractionShape::Capsule;
        interaction.extents = {shape.radius, (std::max)(0.5f * shape.height - shape.radius, 0.0f), shape.radius};
        break;
    default:
        interaction.shape = FluidInteractionShape::Box;
        interaction.extents = shape.size * 0.5f;
        break;
    }
    return interaction;
}
} // namespace

void ResolveFluidObstacles(const World& world, const GameObject& object, const FluidObstacleComponent& component,
                           std::vector<FluidObstacleInstance>& out) {
    const FluidObstacleSettings& settings = component.Settings();
    if (!settings.enabled)
        return;
    const RuntimeObjectHandle handle = object.GetRuntimeHandle();
    for (size_t index = 0; index < settings.shapes.size(); ++index) {
        const FluidObstacleShape& authored = settings.shapes[index];
        VFX::VfxWorldTransform anchor{};
        if (VFX::ResolveVfxAnimationAttachment(world, handle, authored.socket, anchor))
            out.push_back({index + 1u, Place(ToColliderShape(authored), anchor)});
    }
    if (!settings.useColliders)
        return;
    VFX::VfxWorldTransform anchor{};
    if (!VFX::ResolveVfxAnimationAttachment(world, handle, {}, anchor))
        return;
    // 触发器は烟を通し、碰撞资产の形は物理の実行時にしか展開されないので使わない。
    uint64_t instance = kFluidObstacleColliderInstanceBase;
    object.ForEachComponent<ColliderComponent>([&](const ColliderComponent& collider) {
        if (!collider.IsEnabled() || collider.IsTrigger() || collider.UsesCollisionGeometryAsset())
            return;
        for (const ResolvedColliderShape& shape : collider.ResolveShapes())
            out.push_back({instance++, Place(shape, anchor)});
    });
}

void FluidObstacleSystem::PreRender(World& world, const FrameContext& frame) {
    auto* service = world.Services().Find<FluidInteractionService>();
    if (service == nullptr)
        return;
    world.ForEachObjectWith<FluidObstacleComponent>([&](GameObject& object, FluidObstacleComponent& component) {
        obstacles_.clear();
        ResolveFluidObstacles(world, object, component, obstacles_);
        const uint64_t owner = object.GetRuntimeHandle().ToValue();
        for (const FluidObstacleInstance& obstacle : obstacles_)
            (void)service->Submit(FluidInteractionSource::Component, owner, obstacle.instance, obstacle.interaction,
                                  frame.frameIndex);
    });
}

} // namespace HIKARI
