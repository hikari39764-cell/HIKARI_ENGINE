#include "Scene/Debug/Internal/HIKARI_DecalGizmoProvider.h"

#include "Scene/Components/Rendering/Decal/HIKARI_DecalComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/Internal/HIKARI_ComponentGizmoDrawing.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
namespace {
namespace DRAW = COMPONENT_GIZMO_DRAW;

constexpr unsigned int kDecalColor = 0x7FE0A8FF;

// 投影の箱、投影の向き（ローカル +Z）の矢印、テクスチャの上（ローカル +Y）の目印。
void DrawDecal(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const DecalComponent* decal = object.GetComponent<DecalComponent>();
    if (decal == nullptr || !decal->GetSettings().enabled) {
        return;
    }
    const bool selected = object.GetDocumentId() == context.selectedObjectId;
    const MATH::Mat4 world = object.GetTransform().GetWorldMatrix();
    const MATH::Vec3 size = decal->GetSettings().size;
    DRAW::SubmitStyledBox(world, {}, size, kDecalColor, selected);
    const MATH::Vec3 back = DRAW::TransformPoint(world, {0.0f, 0.0f, -0.5f * size.z});
    const MATH::Vec3 front = DRAW::TransformPoint(world, {0.0f, 0.0f, 0.5f * size.z});
    DRAW::SubmitStyledStroke(back, front, kDecalColor, selected);
    const MATH::Vec3 head = DRAW::TransformPoint(world, {0.0f, 0.12f * size.y, 0.3f * size.z});
    DRAW::SubmitStyledStroke(front, head, kDecalColor, selected);
    const MATH::Vec3 top = DRAW::TransformPoint(world, {0.0f, 0.5f * size.y, -0.5f * size.z});
    const MATH::Vec3 topMark = DRAW::TransformPoint(world, {0.0f, 0.65f * size.y, -0.5f * size.z});
    DRAW::SubmitStyledStroke(top, topMark, kDecalColor, selected);
}

} // namespace

void RegisterDecalGizmoProvider(ComponentGizmoRegistry& registry) {
    (void)registry.Register(ComponentGizmoProvider{"scene.decal", "Decals", true, DrawDecal});
}

} // namespace HIKARI
