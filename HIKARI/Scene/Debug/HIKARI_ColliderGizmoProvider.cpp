#include "Scene/Debug/HIKARI_ColliderGizmoProvider.h"

#include <cmath>
#include <string>

#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/HIKARI_PhysicsShapeDebugDraw.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {
        constexpr unsigned int kColliderColor = 0x4DA6FFFF;
        constexpr unsigned int kSelectedColliderColor = 0xFFD166FF;
        constexpr unsigned int kTriggerColor = 0x55FF99FF;

        unsigned int ResolveShapeColor(
            bool isTrigger,
            bool selected) noexcept {
            if (isTrigger) {
                return kTriggerColor;
            }
            return selected
                ? kSelectedColliderColor
                : kColliderColor;
        }

        void DrawCollider(
            const GameObject& object,
            const ComponentGizmoDrawContext& context) {
            const bool selected =
                object.GetDocumentId() == context.selectedObjectId;
            MATH::Vec3 objectPosition{};
            MATH::Quat objectRotation{};
            MATH::Vec3 objectScale{};
            if (!MATH::DecomposeTRS(
                    object.GetTransform().GetWorldMatrix(),
                    objectPosition,
                    objectRotation,
                    objectScale)) {
                return;
            }
            const MATH::Mat4 bodyWorld = MATH::Mat4::TRS(
                objectPosition,
                objectRotation,
                { 1.0f, 1.0f, 1.0f });

            const auto* runtimeStatus = context.world != nullptr
                ? context.world->Services().Find<
                    PHYSICS::PhysicsRuntimeStatusService>()
                : nullptr;
            const auto* runtimeShapes = runtimeStatus != nullptr
                ? runtimeStatus->FindBodyDebugShapes(
                    object.GetRuntimeHandle())
                : nullptr;
            // Collision assets expand into runtime-only shapes, so those must
            // come from the physics cache. Manual and geometry-fitted
            // colliders stay component-authored and must be drawn live;
            // otherwise inspector edits keep showing the stale body cache
            // until the next world rebuild.
            if (runtimeShapes != nullptr) {
                for (const PHYSICS::PhysicsShapeDesc& shape :
                        *runtimeShapes) {
                    if (shape.key.sourceShapeId == 0u) {
                        continue;
                    }
                    DEBUG::DrawPhysicsShape(
                        bodyWorld,
                        shape,
                        ResolveShapeColor(shape.isTrigger, selected),
                        selected);
                }
            }

            uint32_t componentOrdinal = 0u;
            object.ForEachComponent<ColliderComponent>(
                [&](const ColliderComponent& collider) {
                    ++componentOrdinal;
                    if (!collider.IsEnabled() ||
                        collider.UsesCollisionGeometryAsset()) {
                        return;
                    }
                    const PHYSICS::PhysicsShapeDesc shape =
                        PHYSICS::BuildPhysicsShapeDesc(
                            collider,
                            objectScale,
                            componentOrdinal);
                    DEBUG::DrawPhysicsShape(
                        bodyWorld,
                        shape,
                        ResolveShapeColor(shape.isTrigger, selected),
                        selected);
                });
        }
    }

    void RegisterColliderGizmoProvider(
        ComponentGizmoRegistry& registry) {
        (void)registry.Register(ComponentGizmoProvider{
            std::string(kColliderGizmoProviderId),
            "Colliders",
            true,
            DrawCollider
        });
    }

} // namespace HIKARI
