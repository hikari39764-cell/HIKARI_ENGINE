#include "Scene/Debug/HIKARI_ColliderGizmoProvider.h"

#include <cmath>
#include <string>
#include <vector>

#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_FluidObstacleComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/HIKARI_PhysicsShapeDebugDraw.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Vfx/HIKARI_FluidObstacleSystem.h"

namespace HIKARI {
namespace {
constexpr unsigned int kColliderColor = 0x4DA6FFFF;
constexpr unsigned int kSelectedColliderColor = 0xFFD166FF;
constexpr unsigned int kTriggerColor = 0x55FF99FF;
constexpr unsigned int kSelectionHaloColor = 0x101820B8;

unsigned int ResolveShapeColor(bool isTrigger) noexcept { return isTrigger ? kTriggerColor : kColliderColor; }

void SubmitColliderShape(const MATH::Mat4& bodyWorld, const PHYSICS::PhysicsShapeDesc& shape, bool selected) {
    const unsigned int typeColor = ResolveShapeColor(shape.isTrigger);
    if (selected) {
        DEBUG::DrawPhysicsShape(bodyWorld, shape, DEBUG::PhysicsShapeDebugStyle{kSelectionHaloColor, 0u, 5.4f, true});
    }
    DEBUG::DrawPhysicsShape(
        bodyWorld, shape,
        DEBUG::PhysicsShapeDebugStyle{selected ? kSelectedColliderColor : typeColor,
                                      shape.isTrigger ? (typeColor & 0xFFFFFF00u) | (selected ? 0x26u : 0x16u) : 0u,
                                      selected ? 2.8f : 1.6f, false});
}

void DrawCollider(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const bool selected = object.GetDocumentId() == context.selectedObjectId;
    MATH::Mat4 visualWorld = object.GetTransform().GetWorldMatrix();
    // SandBallFollower などは PresentationTransformService で
    // 表示位置だけを上書きする。debug 枠も表示位置に合わせないと、
    // 実際に描画されている球と物理枠がずれて判定しにくい。
    if (context.world != nullptr) {
        if (const auto* presentation = context.world->Services().Find<PresentationTransformService>()) {
            MATH::Mat4 presented{};
            if (presentation->TryGetWorldMatrix(object.GetRuntimeHandle(), presented)) {
                visualWorld = presented;
            }
        }
    }
    MATH::Vec3 objectPosition{};
    MATH::Quat objectRotation{};
    MATH::Vec3 objectScale{};
    if (!MATH::DecomposeTRS(visualWorld, objectPosition, objectRotation, objectScale)) {
        return;
    }
    const MATH::Mat4 bodyWorld = MATH::Mat4::TRS(objectPosition, objectRotation, {1.0f, 1.0f, 1.0f});

    const auto* runtimeStatus =
        context.world != nullptr ? context.world->Services().Find<PHYSICS::PhysicsRuntimeStatusService>() : nullptr;
    const auto* runtimeShapes =
        runtimeStatus != nullptr ? runtimeStatus->FindBodyDebugShapes(object.GetRuntimeHandle()) : nullptr;
    // Collision assets expand into runtime-only shapes, so those must
    // come from the physics cache. Manual and geometry-fitted
    // colliders stay component-authored and must be drawn live;
    // otherwise inspector edits keep showing the stale body cache
    // until the next world rebuild.
    if (runtimeShapes != nullptr) {
        for (const PHYSICS::PhysicsShapeDesc& shape : *runtimeShapes) {
            if (shape.key.sourceShapeId == 0u) {
                continue;
            }
            SubmitColliderShape(bodyWorld, shape, selected);
        }
    }

    uint32_t componentOrdinal = 0u;
    object.ForEachComponent<ColliderComponent>([&](const ColliderComponent& collider) {
        ++componentOrdinal;
        if (!collider.IsEnabled() || collider.UsesCollisionGeometryAsset()) {
            return;
        }
        for (const PHYSICS::PhysicsShapeDesc& shape :
             PHYSICS::BuildPhysicsShapeDescs(collider, objectScale, componentOrdinal)) {
            SubmitColliderShape(bodyWorld, shape, selected);
        }
    });
}

using RENDER3D::FLUID::FluidInteraction;
using RENDER3D::FLUID::FluidInteractionShape;

constexpr unsigned int kObstacleColor = 0xB48CFFFF;
constexpr unsigned int kSelectedObstacleColor = 0xE2D0FFFF;

// 障碍物の形（中心と向きは世界、大きさは FluidInteraction の extents）を線の形にする。
PHYSICS::PhysicsShapeDesc ToDebugShape(const FluidInteraction& interaction) {
    PHYSICS::PhysicsShapeDesc shape{};
    const MATH::Vec3& extents = interaction.extents;
    switch (interaction.shape) {
    case FluidInteractionShape::Sphere:
        shape.type = PHYSICS::PhysicsShapeType::Sphere;
        shape.radius = extents.x;
        break;
    case FluidInteractionShape::Capsule:
        shape.type = PHYSICS::PhysicsShapeType::Capsule;
        shape.radius = extents.x;
        shape.height = 2.0f * (extents.y + extents.x);
        break;
    case FluidInteractionShape::Box:
        shape.type = PHYSICS::PhysicsShapeType::Box;
        shape.halfExtents = extents;
        break;
    }
    return shape;
}

void DrawFluidObstacles(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const FluidObstacleComponent* component = object.GetComponent<FluidObstacleComponent>();
    if (component == nullptr || context.world == nullptr)
        return;
    std::vector<FluidObstacleInstance> obstacles;
    ResolveFluidObstacles(*context.world, object, *component, obstacles);
    const bool selected = object.GetDocumentId() == context.selectedObjectId;
    // 選んだ物体は奥まで透かして描く（体の中の骨の形を見て合わせる）。
    const DEBUG::PhysicsShapeDebugStyle style{selected ? kSelectedObstacleColor : kObstacleColor, 0u,
                                              selected ? 2.2f : 1.2f, selected};
    for (const FluidObstacleInstance& obstacle : obstacles) {
        const FluidInteraction& interaction = obstacle.interaction;
        DEBUG::DrawPhysicsShape(
            MATH::Mat4::TRS(interaction.worldPosition, interaction.worldRotation, {1.0f, 1.0f, 1.0f}),
            ToDebugShape(interaction), style);
    }
}
} // namespace

void RegisterColliderGizmoProvider(ComponentGizmoRegistry& registry) {
    (void)registry.Register(
        ComponentGizmoProvider{std::string(kColliderGizmoProviderId), "Colliders", true, DrawCollider});
}

void RegisterFluidObstacleGizmoProvider(ComponentGizmoRegistry& registry) {
    (void)registry.Register(ComponentGizmoProvider{"FluidObstacles", "Fluid Obstacles", true, DrawFluidObstacles});
}

} // namespace HIKARI
