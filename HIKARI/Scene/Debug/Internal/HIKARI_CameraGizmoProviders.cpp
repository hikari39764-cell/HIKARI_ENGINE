#include "Scene/Debug/Internal/HIKARI_CameraGizmoProviders.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/Internal/HIKARI_ComponentGizmoDrawing.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
namespace {
namespace DRAW = COMPONENT_GIZMO_DRAW;

constexpr unsigned int kSpawnColor = 0x55FF66FF;
constexpr unsigned int kCameraColor = 0x65D9FFFF;
constexpr unsigned int kCameraVolumeColor = 0x55D98BFF;

void SubmitArrow(const MATH::Vec3& from, const MATH::Vec3& to, unsigned int color) {
    DRAW::SubmitStroke(from, to, color, 2.0f);
    const MATH::Vec3 direction = MATH::Normalize(to - from);
    MATH::Vec3 side = {-direction.z, 0.0f, direction.x};
    if (MATH::Length(side) <= 1.0e-5f) {
        side = {1.0f, 0.0f, 0.0f};
    } else {
        side = MATH::Normalize(side);
    }
    const MATH::Vec3 headBase = to - direction * 0.25f;
    DRAW::SubmitStroke(to, headBase + side * 0.12f, color, 2.0f);
    DRAW::SubmitStroke(to, headBase - side * 0.12f, color, 2.0f);
}

void SubmitCameraBody(const MATH::Mat4& world, float scale, bool selected) {
    DRAW::SubmitStyledBox(world, {}, {0.46f * scale, 0.30f * scale, 0.32f * scale}, kCameraColor, selected, true);

    const float bodyFront = 0.16f * scale;
    const float lensFront = 0.42f * scale;
    const float bodyHalfWidth = 0.23f * scale;
    const float bodyHalfHeight = 0.15f * scale;
    const float lensHalf = 0.10f * scale;
    const std::array<MATH::Vec3, 4> bodyCorners{{
        {-bodyHalfWidth, -bodyHalfHeight, bodyFront},
        {bodyHalfWidth, -bodyHalfHeight, bodyFront},
        {bodyHalfWidth, bodyHalfHeight, bodyFront},
        {-bodyHalfWidth, bodyHalfHeight, bodyFront},
    }};
    const std::array<MATH::Vec3, 4> lensCorners{{
        {-lensHalf, -lensHalf, lensFront},
        {lensHalf, -lensHalf, lensFront},
        {lensHalf, lensHalf, lensFront},
        {-lensHalf, lensHalf, lensFront},
    }};
    for (size_t index = 0; index < lensCorners.size(); ++index) {
        DRAW::SubmitStyledStroke(MATH::TransformPoint(world, bodyCorners[index]),
                                 MATH::TransformPoint(world, lensCorners[index]), kCameraColor, selected);
        DRAW::SubmitStyledStroke(MATH::TransformPoint(world, lensCorners[index]),
                                 MATH::TransformPoint(world, lensCorners[(index + 1u) % lensCorners.size()]),
                                 kCameraColor, selected);
    }
    const std::array<MATH::Vec3, 3> handle{{
        {-0.13f * scale, bodyHalfHeight, -0.05f * scale},
        {0.00f, 0.27f * scale, -0.05f * scale},
        {0.13f * scale, bodyHalfHeight, -0.05f * scale},
    }};
    for (size_t index = 0; index < handle.size(); ++index) {
        DRAW::SubmitStyledStroke(MATH::TransformPoint(world, handle[index]),
                                 MATH::TransformPoint(world, handle[(index + 1u) % handle.size()]), kCameraColor,
                                 selected);
    }
}

// 機位の視錐台と本体。選んだ機位は選択色で描き、選んでいない機位は種類の色を薄めて描く。
void SubmitCameraShape(const GameObject& object, const CameraComponent& cameraComponent, const ComponentGizmoDrawContext& context,
                       bool selected) {
    const CameraComponent* camera = &cameraComponent;
    const float aspect =
        std::isfinite(context.cameraAspect) && context.cameraAspect > 0.0001f ? context.cameraAspect : 16.0f / 9.0f;
    const float nearDistance = camera->GetNearClip();
    const float farDistance = (std::min)(camera->GetFarClip(), (std::max)(8.0f, nearDistance * 4.0f));
    const float tangent = std::tan(camera->GetFovYRad() * 0.5f);
    const float nearHeight = tangent * nearDistance;
    const float nearWidth = nearHeight * aspect;
    const float farHeight = tangent * farDistance;
    const float farWidth = farHeight * aspect;
    const MATH::Mat4 world = object.GetTransform().GetWorldMatrix();
    const std::array<MATH::Vec3, 8> corners{{
        MATH::TransformPoint(world, {-nearWidth, -nearHeight, nearDistance}),
        MATH::TransformPoint(world, {nearWidth, -nearHeight, nearDistance}),
        MATH::TransformPoint(world, {nearWidth, nearHeight, nearDistance}),
        MATH::TransformPoint(world, {-nearWidth, nearHeight, nearDistance}),
        MATH::TransformPoint(world, {-farWidth, -farHeight, farDistance}),
        MATH::TransformPoint(world, {farWidth, -farHeight, farDistance}),
        MATH::TransformPoint(world, {farWidth, farHeight, farDistance}),
        MATH::TransformPoint(world, {-farWidth, farHeight, farDistance}),
    }};
    constexpr std::array<std::array<int, 2>, 12> edges{{
        {0, 1},
        {1, 2},
        {2, 3},
        {3, 0},
        {4, 5},
        {5, 6},
        {6, 7},
        {7, 4},
        {0, 4},
        {1, 5},
        {2, 6},
        {3, 7},
    }};
    const unsigned int color = selected ? kCameraColor : DRAW::WithAlpha(kCameraColor, 0x60u);
    for (const auto& edge : edges) {
        DRAW::SubmitStyledStroke(corners[edge[0]], corners[edge[1]], color, selected);
    }
    if (!selected) {
        return;
    }
    const MATH::Vec3 origin = MATH::TransformPoint(world, {});
    const float markerScale = std::clamp(MATH::Length(origin - context.viewportCameraPosition) * 0.035f, 0.25f, 1.25f);
    SubmitCameraBody(world, markerScale, selected);
}

const CameraComponent* DrawableCamera(const GameObject& object) {
    const CameraComponent* camera = object.GetComponent<CameraComponent>();
    return camera != nullptr && camera->IsEnabled() ? camera : nullptr;
}

// 「機位」：選んだ機位だけ視錐台を描く。選んでいない機位は視口の画面上の印（エディタ側）で示す。
void DrawSelectedCamera(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const CameraComponent* camera = DrawableCamera(object);
    if (camera != nullptr && object.GetDocumentId() == context.selectedObjectId) {
        SubmitCameraShape(object, *camera, context, true);
    }
}

// 「すべての視錐台」（既定で切）：選んでいない機位の視錐台も薄く描く（機位どうしの向きを見比べる時）。
void DrawOtherCameraFrustums(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const CameraComponent* camera = DrawableCamera(object);
    if (camera != nullptr && object.GetDocumentId() != context.selectedObjectId) {
        SubmitCameraShape(object, *camera, context, false);
    }
}

void DrawSpawnPoint(const GameObject& object, const ComponentGizmoDrawContext&) {
    const SpawnPointComponent* spawn = object.GetComponent<SpawnPointComponent>();
    if (spawn == nullptr || !spawn->IsEnabled()) {
        return;
    }
    const Transform3D& transform = object.GetTransform();
    RENDERER3D::DEBUG::Box3D marker{};
    marker.transform = transform;
    marker.size = {0.35f, 0.35f, 0.35f};
    marker.outlineRgba = kSpawnColor;
    marker.fillRgba = DRAW::WithAlpha(kSpawnColor, 0x20u);
    marker.outlineThicknessPixels = 2.0f;
    RENDERER3D::DEBUG::SubmitBox3D(marker);
    SubmitArrow(transform.position, transform.position + MATH::Vec3{0.0f, 0.8f, 0.0f}, kSpawnColor);
}

void DrawCameraActivationVolume(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const auto* volume = object.GetComponent<CameraActivationVolumeComponent>();
    if (volume == nullptr || !volume->IsEnabled()) {
        return;
    }
    DRAW::SubmitStyledBox(object.GetTransform().GetWorldMatrix(), {}, volume->GetHalfExtents() * 2.0f,
                          kCameraVolumeColor, object.GetDocumentId() == context.selectedObjectId);
}
} // namespace

void RegisterCameraGizmoProviders(ComponentGizmoRegistry& registry) {
    (void)registry.Register(
        ComponentGizmoProvider{std::string(kCameraFrustumGizmoProviderId), "Camera Frustums", true, DrawSelectedCamera});
    (void)registry.Register(ComponentGizmoProvider{std::string(kAllCameraFrustumsGizmoProviderId), "All Camera Frustums",
                                                   false, DrawOtherCameraFrustums});
    (void)registry.Register(
        ComponentGizmoProvider{std::string(kSpawnPointGizmoProviderId), "Spawn Points", false, DrawSpawnPoint});
    (void)registry.Register(ComponentGizmoProvider{std::string(kCameraActivationVolumeGizmoProviderId),
                                                   "Camera Activation Volumes", true, DrawCameraActivationVolume});
}

} // namespace HIKARI
