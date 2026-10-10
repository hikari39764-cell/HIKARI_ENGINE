#include "Editor/Workspaces/LookDev/HIKARI_LookDevPlacement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

constexpr float kRadiansToDegrees = 57.2957795f;

// 当たった点に最も近い箱の面の外向き。
MATH::Vec3 BoxFaceNormal(const MATH::Vec3& point, const MATH::Vec3& minimum, const MATH::Vec3& maximum) {
    const std::array<float, 6> distances{
        std::abs(point.x - minimum.x), std::abs(point.x - maximum.x), std::abs(point.y - minimum.y),
        std::abs(point.y - maximum.y), std::abs(point.z - minimum.z), std::abs(point.z - maximum.z),
    };
    const size_t face = static_cast<size_t>(std::min_element(distances.begin(), distances.end()) - distances.begin());
    constexpr std::array<MATH::Vec3, 6> kNormals{
        MATH::Vec3{-1.0f, 0.0f, 0.0f}, MATH::Vec3{1.0f, 0.0f, 0.0f},  MATH::Vec3{0.0f, -1.0f, 0.0f},
        MATH::Vec3{0.0f, 1.0f, 0.0f},  MATH::Vec3{0.0f, 0.0f, -1.0f}, MATH::Vec3{0.0f, 0.0f, 1.0f},
    };
    return kNormals[face];
}

float Snap(float value, float step) { return step > 0.0f ? std::round(value / step) * step : value; }

// 形の外接寸法（程序化の形は原点が中心）。
MATH::Vec3 ShapeExtent(const ProceduralMeshSettings& shape) {
    switch (shape.kind) {
    case ProceduralMeshKind::Sphere: return {shape.width, shape.width, shape.width};
    case ProceduralMeshKind::Cylinder:
    case ProceduralMeshKind::Capsule: return {shape.width, shape.height, shape.width};
    case ProceduralMeshKind::Plane:
    case ProceduralMeshKind::GridPlane: return {shape.width, 0.0f, shape.depth};
    case ProceduralMeshKind::Box:
    default: return {shape.width, shape.height, shape.depth};
    }
}

} // namespace

bool ProjectLookDevPoint(const Camera3D& camera, const LookDevView& view, const MATH::Vec3& point,
                         MATH::Vec2& screen) noexcept {
    const MATH::Vec4 clip = camera.GetViewProj().TransformPoint({point.x, point.y, point.z, 1.0f});
    if (clip.w <= 1.0e-4f) {
        return false;
    }
    screen = {view.x + (clip.x / clip.w * 0.5f + 0.5f) * view.width,
              view.y + (-clip.y / clip.w * 0.5f + 0.5f) * view.height};
    return true;
}

MATH::Vec3 LookDevFacingRotation(const MATH::Vec3& direction) noexcept {
    const MATH::Vec3 d = MATH::Normalize(direction);
    const float pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    const float yaw = std::abs(d.x) + std::abs(d.z) > 1.0e-5f ? std::atan2(d.x, d.z) : 0.0f;
    return {-pitch * kRadiansToDegrees, yaw * kRadiansToDegrees, 0.0f};
}

bool IsLookDevLight(LookDevPlaceItem item) noexcept {
    return item == LookDevPlaceItem::PointLight || item == LookDevPlaceItem::SpotLight ||
           item == LookDevPlaceItem::TubeLight || item == LookDevPlaceItem::RectLight;
}

RENDER3D::LIGHTING::SceneLightType LookDevLightType(LookDevPlaceItem item) noexcept {
    switch (item) {
    case LookDevPlaceItem::SpotLight: return RENDER3D::LIGHTING::SceneLightType::Spot;
    case LookDevPlaceItem::TubeLight: return RENDER3D::LIGHTING::SceneLightType::Tube;
    case LookDevPlaceItem::RectLight: return RENDER3D::LIGHTING::SceneLightType::Rect;
    case LookDevPlaceItem::PointLight:
    default: return RENDER3D::LIGHTING::SceneLightType::Point;
    }
}

ProceduralMeshSettings LookDevShape(LookDevPlaceItem item) noexcept {
    ProceduralMeshSettings shape{};
    switch (item) {
    case LookDevPlaceItem::Wall:
        shape.width = 4.0f;
        shape.height = 3.0f;
        shape.depth = 0.25f;
        break;
    case LookDevPlaceItem::Floor:
        shape.width = 4.0f;
        shape.height = 0.2f;
        shape.depth = 4.0f;
        break;
    case LookDevPlaceItem::Pillar:
        shape.kind = ProceduralMeshKind::Cylinder;
        shape.width = 0.5f;
        shape.height = 3.0f;
        break;
    case LookDevPlaceItem::Ramp:
        shape.kind = ProceduralMeshKind::Ramp;
        shape.width = 2.0f;
        shape.height = 1.0f;
        shape.depth = 3.0f;
        break;
    case LookDevPlaceItem::Stairs:
        // 一段 0.18 m・踏面 0.3 m の歩ける階段。
        shape.kind = ProceduralMeshKind::Stairs;
        shape.width = 1.5f;
        shape.height = 1.44f;
        shape.depth = 2.4f;
        shape.segmentsY = 8u;
        break;
    case LookDevPlaceItem::Sphere: shape.kind = ProceduralMeshKind::Sphere; break;
    case LookDevPlaceItem::Capsule:
        shape.kind = ProceduralMeshKind::Capsule;
        shape.width = 0.6f;
        shape.height = 1.6f;
        break;
    case LookDevPlaceItem::Person:
        // 人物の比例の目安（身長 1.8 m、肩幅 0.45 m）。
        shape.kind = ProceduralMeshKind::Capsule;
        shape.width = 0.45f;
        shape.height = 1.8f;
        break;
    case LookDevPlaceItem::Box:
    default: break;
    }
    return shape;
}

std::optional<LookDevSurfaceHit> FindLookDevSurface(const Camera3D& camera, float normalizedX, float normalizedY) {
    EditorPointerRay ray{};
    if (!BuildEditorPointerRay(camera, normalizedX, normalizedY, ray)) {
        return std::nullopt;
    }
    float nearest = (std::numeric_limits<float>::max)();
    std::optional<LookDevSurfaceHit> hit{};
    for (const RENDER3D::RUNTIME::SceneSurfaceInstance& surface :
         RenderSubmissionSystem::GetSceneRenderCache().GetSurfaceInstances()) {
        float distance = 0.0f;
        if (!surface.valid || !surface.visible || !BOUNDS::IsUsable(surface.worldBounds) ||
            !IntersectEditorBounds(ray, surface.worldBounds.min, surface.worldBounds.max, distance) ||
            distance <= 0.0f || distance >= nearest) {
            continue;
        }
        nearest = distance;
        const MATH::Vec3 point = ray.origin + ray.direction * distance;
        hit = LookDevSurfaceHit{point, BoxFaceNormal(point, surface.worldBounds.min, surface.worldBounds.max)};
    }
    // 何も無い所は地面（y = 0）に置く。
    if (ray.direction.y < -1.0e-4f) {
        const float distance = -ray.origin.y / ray.direction.y;
        if (distance > 0.0f && distance < nearest) {
            hit = LookDevSurfaceHit{ray.origin + ray.direction * distance, {0.0f, 1.0f, 0.0f}};
        }
    }
    return hit;
}

float FindLookDevGroundHeight(const MATH::Vec3& point, const std::unordered_set<uint64_t>& ignoredObjects) {
    float ground = 0.0f;
    for (const RENDER3D::RUNTIME::SceneSurfaceInstance& surface :
         RenderSubmissionSystem::GetSceneRenderCache().GetSurfaceInstances()) {
        const Bounds& bounds = surface.worldBounds;
        if (surface.valid && surface.visible && !ignoredObjects.contains(surface.objectId.value) &&
            BOUNDS::IsUsable(bounds) && point.x >= bounds.min.x && point.x <= bounds.max.x && point.z >= bounds.min.z &&
            point.z <= bounds.max.z && bounds.max.y <= point.y) {
            ground = (std::max)(ground, bounds.max.y);
        }
    }
    return ground;
}

LookDevPlacementPose ComputeLookDevPlacement(LookDevPlaceItem item, const LookDevSurfaceHit& hit, float gridStep,
                                             float yawDegrees) noexcept {
    LookDevPlacementPose pose{};
    MATH::Vec3 anchor = hit.position;
    // 上向きの面では水平の 2 軸、横の面では面に沿った軸だけを格子へ吸着する（面からは浮かせない）。
    if (std::abs(hit.normal.x) < 0.5f) {
        anchor.x = Snap(anchor.x, gridStep);
    }
    if (std::abs(hit.normal.y) < 0.5f) {
        anchor.y = Snap(anchor.y, gridStep);
    }
    if (std::abs(hit.normal.z) < 0.5f) {
        anchor.z = Snap(anchor.z, gridStep);
    }
    const bool upward = hit.normal.y > 0.5f;
    if (IsLookDevLight(item)) {
        pose.extent = {0.3f, 0.3f, 0.3f};
        switch (item) {
        case LookDevPlaceItem::SpotLight:
            // 面から 3 m 離して面を照らす。
            pose.position = anchor + hit.normal * 3.0f;
            pose.rotationDegrees = LookDevFacingRotation(hit.normal * -1.0f);
            break;
        case LookDevPlaceItem::RectLight:
            // 床なら天井の高さから下へ、壁なら壁に沿わせて外へ向ける。
            pose.position = upward ? anchor + hit.normal * 3.0f : anchor + hit.normal * 0.05f;
            pose.rotationDegrees = LookDevFacingRotation(upward ? hit.normal * -1.0f : hit.normal);
            pose.extent = {1.0f, 1.0f, 0.05f};
            break;
        case LookDevPlaceItem::TubeLight:
            pose.position = anchor + hit.normal * (upward ? 2.5f : 0.3f);
            pose.rotationDegrees = {0.0f, yawDegrees, 0.0f};
            pose.extent = {0.1f, 0.1f, 1.2f};
            break;
        case LookDevPlaceItem::PointLight:
        default: pose.position = anchor + hit.normal * (upward ? 2.5f : 0.5f); break;
        }
        return pose;
    }
    pose.extent = ShapeExtent(LookDevShape(item));
    // 底（横の面なら背中）を面に付ける。
    const float halfAlongNormal = std::abs(hit.normal.x) * pose.extent.x * 0.5f +
                                  std::abs(hit.normal.y) * pose.extent.y * 0.5f +
                                  std::abs(hit.normal.z) * pose.extent.z * 0.5f;
    pose.position = anchor + hit.normal * halfAlongNormal;
    pose.rotationDegrees = {0.0f, yawDegrees, 0.0f};
    return pose;
}

#if defined(HIKARI_WITH_EDITOR)
void DrawLookDevPlacementPreview(ImDrawList& drawList, const Camera3D& camera, const LookDevView& view,
                                 const LookDevPlacementPose& pose, const LookDevSurfaceHit& hit) {
    const auto project = [&](const MATH::Vec3& point, ImVec2& screen) {
        MATH::Vec2 projected{};
        if (!ProjectLookDevPoint(camera, view, point, projected)) {
            return false;
        }
        screen = {projected.x, projected.y};
        return true;
    };
    const MATH::Mat4 world = MATH::Mat4::TRS(pose.position,
                                             MATH::Quat::FromEulerXYZ(pose.rotationDegrees.x / kRadiansToDegrees,
                                                                      pose.rotationDegrees.y / kRadiansToDegrees,
                                                                      pose.rotationDegrees.z / kRadiansToDegrees),
                                             {1.0f, 1.0f, 1.0f});
    const MATH::Vec3 half = pose.extent * 0.5f;
    std::array<ImVec2, 8> corners{};
    bool visible = true;
    for (int index = 0; index < 8; ++index) {
        const MATH::Vec4 local{(index & 1) ? half.x : -half.x, (index & 2) ? half.y : -half.y,
                               (index & 4) ? half.z : -half.z, 1.0f};
        const MATH::Vec4 point = world.TransformPoint(local);
        visible &= project({point.x, point.y, point.z}, corners[static_cast<size_t>(index)]);
    }
    drawList.PushClipRect({view.x, view.y}, {view.x + view.width, view.y + view.height}, true);
    constexpr ImU32 kOutline = IM_COL32(120, 230, 255, 220);
    if (visible) {
        constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                       {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& edge : kEdges) {
            drawList.AddLine(corners[static_cast<size_t>(edge[0])], corners[static_cast<size_t>(edge[1])], kOutline,
                             1.5f);
        }
    }
    ImVec2 contact{};
    if (project(hit.position, contact)) {
        drawList.AddCircleFilled(contact, 3.5f, kOutline);
    }
    drawList.PopClipRect();
}
#else
void DrawLookDevPlacementPreview(ImDrawList&, const Camera3D&, const LookDevView&, const LookDevPlacementPose&,
                                 const LookDevSurfaceHit&) {}
#endif

} // namespace HIKARI::EDITOR
