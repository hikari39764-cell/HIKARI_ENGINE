#include "Scene/Debug/HIKARI_ColliderGizmoProvider.h"

#include <cmath>
#include <string>

#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/HIKARI_PhysicsShapeDebugDraw.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"

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
} // namespace

void RegisterColliderGizmoProvider(ComponentGizmoRegistry& registry) {
    (void)registry.Register(
        ComponentGizmoProvider{std::string(kColliderGizmoProviderId), "Colliders", true, DrawCollider});
}

} // namespace HIKARI
