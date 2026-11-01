#include "Editor/Workspaces/LookDev/HIKARI_LookDevLightTools.h"

#include <algorithm>
#include <cmath>

#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevPlacement.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {

using RENDER3D::LIGHTING::SceneLightType;

constexpr float kDegreesToRadians = 0.0174532925f;
constexpr MATH::Vec3 kUp{0.0f, 1.0f, 0.0f};

// 水平の向き horizontal から elevation 度だけ上げた単位ベクトル。
MATH::Vec3 Elevate(const MATH::Vec3& horizontal, float elevationDegrees) {
    const float elevation = elevationDegrees * kDegreesToRadians;
    return MATH::Normalize(horizontal * std::cos(elevation) + kUp * std::sin(elevation));
}

MATH::Vec3 Axis(const MATH::Mat4& world, int row, const MATH::Vec3& fallback) {
    const MATH::Vec3 axis{world.m[row][0], world.m[row][1], world.m[row][2]};
    return MATH::Length(axis) > 1.0e-6f ? MATH::Normalize(axis) : fallback;
}

// 相機から見た右（画面の右）。真下を見ていても決まるように、上向きと平行なら世界の X。
MATH::Vec3 CameraRight(const Camera3D& camera) {
    const MATH::Vec3 forward = camera.GetTarget() - camera.GetPosition();
    const MATH::Vec3 right = MATH::Cross(kUp, forward);
    return MATH::Length(right) > 1.0e-5f ? MATH::Normalize(right) : MATH::Vec3{1.0f, 0.0f, 0.0f};
}

MATH::Vec3 CameraForward(const Camera3D& camera) {
    const MATH::Vec3 forward = camera.GetTarget() - camera.GetPosition();
    return MATH::Length(forward) > 1.0e-5f ? MATH::Normalize(forward) : MATH::Vec3{0.0f, 0.0f, 1.0f};
}

// 円錐の縁の調整点を置く、軸に垂直な向き（画面の右に近い方）。
MATH::Vec3 ConeSide(const Camera3D& camera, const MATH::Vec3& axis, const MATH::Vec3& fallback) {
    const MATH::Vec3 right = CameraRight(camera);
    const MATH::Vec3 side = right - axis * MATH::Dot(right, axis);
    return MATH::Length(side) > 1.0e-4f ? MATH::Normalize(side) : fallback;
}

} // namespace

std::array<LookDevRigLight, 3> BuildThreePointLights(const Bounds& subject, const MATH::Vec3& cameraPosition) {
    const MATH::Vec4 sphere = BOUNDS::ComputeCenterRadius(subject);
    const MATH::Vec3 center{sphere.x, sphere.y, sphere.z};
    const float radius = (std::max)(sphere.w, 0.3f);
    MATH::Vec3 forward = center - cameraPosition;
    forward.y = 0.0f;
    forward = MATH::Length(forward) > 1.0e-4f ? MATH::Normalize(forward) : MATH::Vec3{0.0f, 0.0f, 1.0f};
    const MATH::Vec3 right = MATH::Normalize(MATH::Cross(kUp, forward));
    const float distance = (std::max)(2.0f, radius * 3.0f);
    // 被写体を覆う円錐。内側は外側の 6 割で縁を柔らかくする。
    const float outer = std::clamp(std::atan2(radius * 1.6f, distance) / kDegreesToRadians, 10.0f, 60.0f);
    struct Spec {
        std::string_view nameKey;
        MATH::Vec3 horizontal;
        float elevation;
        float ratio;
        MATH::Vec3 color;
        bool shadows;
    };
    const auto around = [&](float yawDegrees, float frontSign) {
        const float yaw = yawDegrees * kDegreesToRadians;
        return MATH::Normalize(forward * (frontSign * std::cos(yaw)) + right * std::sin(yaw));
    };
    const Spec specs[3] = {
        {"ui.lookDev.rig.key", around(45.0f, -1.0f), 35.0f, 1.0f, {1.0f, 0.96f, 0.9f}, true},
        {"ui.lookDev.rig.fill", around(-60.0f, -1.0f), 15.0f, 0.3f, {0.88f, 0.93f, 1.0f}, false},
        {"ui.lookDev.rig.rim", around(-25.0f, 1.0f), 40.0f, 0.8f, {1.0f, 0.97f, 0.92f}, false},
    };
    std::array<LookDevRigLight, 3> rig{};
    for (size_t index = 0; index < rig.size(); ++index) {
        const Spec& spec = specs[index];
        const MATH::Vec3 direction = Elevate(spec.horizontal, spec.elevation);
        LookDevRigLight& light = rig[index];
        light.nameKey = spec.nameKey;
        light.position = center + direction * distance;
        light.rotationDegrees = LookDevFacingRotation(direction * -1.0f);
        light.settings.type = SceneLightType::Spot;
        light.settings.intensityUnit = LightIntensityUnit::SceneRelative;
        // 相対の 1 は 1 m 先が環境の照度と同じ明るさ。被写体の位置で ratio 倍にする。
        light.settings.sceneRelativeIntensity = spec.ratio * distance * distance;
        light.settings.color = spec.color;
        light.settings.range = distance * 2.0f;
        light.settings.outerConeAngleDegrees = outer;
        light.settings.innerConeAngleDegrees = outer * 0.6f;
        light.settings.castsShadows = spec.shadows;
        light.settings = NormalizeLightSettings(light.settings);
    }
    return rig;
}

std::vector<LookDevLightHandlePoint> BuildLookDevLightHandles(const LightSettings& settings, const MATH::Mat4& world,
                                                              const Camera3D& camera) {
    const MATH::Vec3 origin = LookDevTranslation(world);
    const MATH::Vec3 axisX = Axis(world, 0, {1.0f, 0.0f, 0.0f});
    const MATH::Vec3 axisY = Axis(world, 1, {0.0f, 1.0f, 0.0f});
    const MATH::Vec3 axisZ = Axis(world, 2, {0.0f, 0.0f, 1.0f});
    switch (settings.type) {
    case SceneLightType::Spot: {
        const MATH::Vec3 tip = origin + axisZ * settings.range;
        const MATH::Vec3 side = ConeSide(camera, axisZ, axisX);
        return {
            {LookDevLightHandle::Range, tip},
            {LookDevLightHandle::OuterCone,
             tip + side * (settings.range * std::tan(settings.outerConeAngleDegrees * kDegreesToRadians))},
            {LookDevLightHandle::InnerCone,
             tip - side * (settings.range * std::tan(settings.innerConeAngleDegrees * kDegreesToRadians))},
        };
    }
    case SceneLightType::Rect:
        return {
            {LookDevLightHandle::Width, origin + axisX * (settings.length * 0.5f)},
            {LookDevLightHandle::Height, origin + axisY * (settings.height * 0.5f)},
        };
    case SceneLightType::Tube: return {{LookDevLightHandle::Length, origin + axisX * (settings.length * 0.5f)}};
    case SceneLightType::Point:
    default: return {{LookDevLightHandle::Range, origin + CameraRight(camera) * settings.range}};
    }
}

bool ApplyLookDevLightHandle(LookDevLightHandle handle, const LightSettings& before, const MATH::Mat4& world,
                             const EditorPointerRay& ray, const Camera3D& camera, LightSettings& settings) {
    const MATH::Vec3 origin = LookDevTranslation(world);
    const MATH::Vec3 axisX = Axis(world, 0, {1.0f, 0.0f, 0.0f});
    const MATH::Vec3 axisY = Axis(world, 1, {0.0f, 1.0f, 0.0f});
    const MATH::Vec3 axisZ = Axis(world, 2, {0.0f, 0.0f, 1.0f});
    settings = before;
    MATH::Vec3 hit{};
    float along = 0.0f;
    switch (handle) {
    case LookDevLightHandle::Range:
        if (before.type == SceneLightType::Spot) {
            if (!ClosestEditorLineParameter(ray, origin, axisZ, along)) {
                return false;
            }
            settings.range = along;
        } else {
            if (!IntersectEditorPlane(ray, origin, CameraForward(camera), hit)) {
                return false;
            }
            settings.range = MATH::Length(hit - origin);
        }
        break;
    case LookDevLightHandle::OuterCone:
    case LookDevLightHandle::InnerCone: {
        const MATH::Vec3 tip = origin + axisZ * before.range;
        if (!IntersectEditorPlane(ray, tip, axisZ, hit)) {
            return false;
        }
        const float angle = std::atan2(MATH::Length(hit - tip), before.range) / kDegreesToRadians;
        (handle == LookDevLightHandle::OuterCone ? settings.outerConeAngleDegrees : settings.innerConeAngleDegrees) =
            angle;
        // 内側を広げて外側を越えたら外側も広げる（外側を狭めた時は内側を合わせる）。
        if (handle == LookDevLightHandle::OuterCone) {
            settings.innerConeAngleDegrees = (std::min)(settings.innerConeAngleDegrees, angle);
        }
        break;
    }
    case LookDevLightHandle::Width:
    case LookDevLightHandle::Height:
        if (!IntersectEditorPlane(ray, origin, axisZ, hit)) {
            return false;
        }
        (handle == LookDevLightHandle::Width ? settings.length : settings.height) =
            2.0f * std::abs(MATH::Dot(hit - origin, handle == LookDevLightHandle::Width ? axisX : axisY));
        break;
    case LookDevLightHandle::Length:
        if (!ClosestEditorLineParameter(ray, origin, axisX, along)) {
            return false;
        }
        settings.length = 2.0f * std::abs(along);
        break;
    case LookDevLightHandle::None:
    default: return false;
    }
    settings = NormalizeLightSettings(settings);
    return settings != before;
}

void DrawLookDevLightShape(ImDrawList& drawList, const Camera3D& camera, float x, float y, float width, float height,
                           const LightSettings& settings, const MATH::Mat4& world) {
    const LookDevView view{x, y, width, height};
    constexpr ImU32 kShape = IM_COL32(255, 214, 120, 190);
    constexpr ImU32 kInner = IM_COL32(255, 214, 120, 90);
    const auto line = [&](const MATH::Vec3& from, const MATH::Vec3& to, ImU32 color) {
        MATH::Vec2 a{};
        MATH::Vec2 b{};
        if (ProjectLookDevPoint(camera, view, from, a) && ProjectLookDevPoint(camera, view, to, b)) {
            drawList.AddLine({a.x, a.y}, {b.x, b.y}, color, 1.5f);
        }
    };
    // center を中心に u・v の平面の半径 radius の円。
    const auto circle = [&](const MATH::Vec3& center, const MATH::Vec3& u, const MATH::Vec3& v, float radius,
                            ImU32 color) {
        constexpr int kSegments = 48;
        MATH::Vec3 previous = center + u * radius;
        for (int index = 1; index <= kSegments; ++index) {
            const float angle = 6.2831853f * static_cast<float>(index) / static_cast<float>(kSegments);
            const MATH::Vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
            line(previous, next, color);
            previous = next;
        }
    };
    const MATH::Vec3 origin = LookDevTranslation(world);
    const MATH::Vec3 axisX = Axis(world, 0, {1.0f, 0.0f, 0.0f});
    const MATH::Vec3 axisY = Axis(world, 1, {0.0f, 1.0f, 0.0f});
    const MATH::Vec3 axisZ = Axis(world, 2, {0.0f, 0.0f, 1.0f});
    drawList.PushClipRect({x, y}, {x + width, y + height}, true);
    switch (settings.type) {
    case SceneLightType::Spot: {
        const MATH::Vec3 tip = origin + axisZ * settings.range;
        const float outer = settings.range * std::tan(settings.outerConeAngleDegrees * kDegreesToRadians);
        const float inner = settings.range * std::tan(settings.innerConeAngleDegrees * kDegreesToRadians);
        circle(tip, axisX, axisY, outer, kShape);
        circle(tip, axisX, axisY, inner, kInner);
        for (const MATH::Vec3& side : {axisX, axisX * -1.0f, axisY, axisY * -1.0f}) {
            line(origin, tip + side * outer, kShape);
        }
        line(origin, tip, kInner);
        break;
    }
    case SceneLightType::Rect: {
        const MATH::Vec3 halfX = axisX * (settings.length * 0.5f);
        const MATH::Vec3 halfY = axisY * (settings.height * 0.5f);
        const MATH::Vec3 corners[4] = {origin - halfX - halfY, origin + halfX - halfY, origin + halfX + halfY,
                                       origin - halfX + halfY};
        for (int index = 0; index < 4; ++index) {
            line(corners[index], corners[(index + 1) % 4], kShape);
        }
        line(origin, origin + axisZ * (std::max)(0.5f, (std::min)(settings.range * 0.25f, 2.0f)), kInner);
        break;
    }
    case SceneLightType::Tube:
        line(origin - axisX * (settings.length * 0.5f), origin + axisX * (settings.length * 0.5f), kShape);
        break;
    case SceneLightType::Point:
    default: {
        // 画面に向いた円（範囲の輪郭）と水平の円。
        const MATH::Vec3 right = CameraRight(camera);
        const MATH::Vec3 up = MATH::Normalize(MATH::Cross(CameraForward(camera), right));
        circle(origin, right, up, settings.range, kShape);
        circle(origin, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, settings.range, kInner);
        break;
    }
    }
    drawList.PopClipRect();
}

} // namespace HIKARI::EDITOR
