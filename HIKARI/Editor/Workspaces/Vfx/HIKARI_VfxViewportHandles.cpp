#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include <algorithm>
#include <cmath>
#include <limits>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
MATH::Vec3 Transform(const MATH::Mat4 &matrix, const MATH::Vec3 &v, float w = 1) {
    const auto result = matrix.TransformPoint({v.x, v.y, v.z, w});
    return {result.x, result.y, result.z};
}
} // namespace
bool VfxWorkspaceController::DrawLayerHandles(const EditorViewportRect &rect, bool hovered,
                                              bool gizmoInteracting, std::string &pickedLayer) {
#if defined(HIKARI_WITH_EDITOR)
    if (!document_.IsOpen() || !previewProgram_)
        return false;
    const auto &camera = cameraController_.GetCamera();
    const auto vp = camera.GetViewProj();
    const auto mouse = ImGui::GetIO().MousePos;
    auto &draw = *ImGui::GetWindowDrawList();
    const auto project = [&](MATH::Vec3 world, ImVec2 &screen) {
        const auto clip = vp.TransformPoint({world.x, world.y, world.z, 1});
        if (clip.w < 0.00001f || clip.z < 0)
            return false;
        screen = {rect.x + (clip.x / clip.w * 0.5f + 0.5f) * rect.width,
                  rect.y + (0.5f - clip.y / clip.w * 0.5f) * rect.height};
        return true;
    };
    draw.PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
    EditorPointerRay ray;
    const bool rayValid =
        BuildEditorPointerRay(camera, (mouse.x - rect.x) / rect.width, (mouse.y - rect.y) / rect.height, ray);
    const bool canPick = hovered && rayValid && !ImGui::GetIO().KeyAlt && !gizmoInteracting &&
                         !timelineEdit_.baseline && !timelineEdit_.suppressDragUntilRelease;
    bool handleHit = false;
    float nearest = std::numeric_limits<float>::max();
    std::string picked;
    const auto asset = document_.Asset();
    const auto marker = [&](VfxViewportHandleState handle, MATH::Vec3 point, MATH::Vec3 axis) {
        ImVec2 screen, unit;
        if (!project(point, screen))
            return;
        const float dx = mouse.x - screen.x, dy = mouse.y - screen.y;
        const bool over = dx * dx + dy * dy < 81;
        draw.AddCircleFilled(screen, over ? 6.5f : 4.5f,
                             handle.pathKey ? IM_COL32(100, 215, 255, 255) : IM_COL32(255, 200, 78, 255));
        draw.AddCircle(screen, over ? 8.5f : 6.5f, IM_COL32(10, 15, 25, 230));
        if (over && canPick && !handleHit) {
            handleHit = true;
            if (ImGui::IsMouseClicked(0)) {
                BeginTimelineEdit();
                viewportHandle_ = std::move(handle);
                viewportHandle_.worldPoint = point;
                viewportHandle_.startMouse = {mouse.x, mouse.y};
                if (project(point + axis, unit))
                    viewportHandle_.screenAxis = {unit.x - screen.x, unit.y - screen.y};
                const auto inverseView = MATH::Inverse(camera.GetView());
                viewportHandle_.planeNormal = MATH::Normalize(Transform(inverseView, {0, 0, 1}, 0));
                if (viewportHandle_.pathKey)
                    (void)SeekPreview(viewportHandle_.seconds, statusMessage_);
            }
        }
    };
    for (const auto &layer : asset.emitters) {
        if (layer.kind == VFX::VfxLayerKind::Adjustment || !IsEmitterPreviewVisible(layer.id))
            continue;
        VFX::VfxWorldTransform world, frame;
        if (!playbackService_.GetLayerPoseAtTime(previewHandle_, layer.id, previewElapsedSeconds_, world,
                                                 frame))
            continue;
        if (std::abs(world.scale.x * world.scale.y * world.scale.z) < 0.000001f)
            world = frame;
        if (std::abs(world.scale.x * world.scale.y * world.scale.z) < 0.000001f)
            continue;
        const auto matrix = MATH::Mat4::TRS(world.position, world.rotation, world.scale);
        const auto inverse = MATH::Inverse(matrix);
        const bool selected = selection_.emitterId == layer.id;
        const bool particles = layer.kind == VFX::VfxLayerKind::Particles;
        const float span =
            layer.activeDuration > 0 ? layer.activeDuration : asset.durationSeconds - layer.startDelay;
        auto compiled = std::find_if(previewProgram_->emitters.begin(), previewProgram_->emitters.end(),
                                     [&](const auto &e) { return e.sourceEmitterId == layer.id; });
        if (compiled == previewProgram_->emitters.end())
            continue;
        auto render = compiled->render;
        VFX::EvaluateVfxLayerRenderParameters(
            *compiled, span > 0 ? (previewElapsedSeconds_ - layer.startDelay) / span : 0, render, span);
        const bool ring = layer.renderShape.type == VFX::VfxRenderShapeType::Ring && !particles;
        MATH::Vec3 half{0.5f, 0.5f, 0.5f};
        if (ring)
            half = {render.ringOuterRadius, render.ringOuterRadius, 0.03f};
        else if (particles) {
            const float radius =
                (std::max)(0.15f, layer.shape.radius.mode == VFX::VfxDistributionMode::Constant
                                      ? layer.shape.radius.minimum
                                      : layer.shape.radius.maximum);
            half = layer.shape.type == VFX::VfxShapeType::Box ? layer.shape.boxHalfExtents
                                                              : MATH::Vec3{radius, radius, radius};
        } else if (layer.renderShape.type == VFX::VfxRenderShapeType::Sprite)
            half.z = 0.03f;
        if (canPick && layer.kind != VFX::VfxLayerKind::Group && layer.kind != VFX::VfxLayerKind::Adjustment) {
            EditorPointerRay local{Transform(inverse, ray.origin), Transform(inverse, ray.direction, 0)};
            float distance = 0;
            bool hit = IntersectEditorBounds(local, half * -1, half, distance);
            if (ring && std::abs(local.direction.z) > 0.000001f) {
                distance = -local.origin.z / local.direction.z;
                const auto p = local.origin + local.direction * distance;
                const float r = std::sqrt(p.x * p.x + p.y * p.y);
                float angle = std::atan2(p.y, p.x), start = render.ringStartAngleRadians,
                      end = render.ringEndAngleRadians;
                if (end < start)
                    std::swap(start, end);
                angle += std::ceil((start - angle) / 6.2831853f) * 6.2831853f;
                hit = distance >= 0 && r >= render.ringInnerRadius && r <= render.ringOuterRadius &&
                      angle <= end;
            }
            if (hit && distance < nearest) {
                nearest = distance;
                picked = layer.id;
            }
        }
        if (!selected)
            continue;
        const auto line = [&](MATH::Vec3 a, MATH::Vec3 b) {
            ImVec2 x, y;
            if (project(Transform(matrix, a), x) && project(Transform(matrix, b), y))
                draw.AddLine(x, y, IM_COL32(85, 200, 226, 180), 1.2f);
        };
        if (ring) {
            const float start = render.ringStartAngleRadians, end = render.ringEndAngleRadians;
            for (int i = 1; i <= 48; ++i)
                for (float radius : {render.ringInnerRadius, render.ringOuterRadius}) {
                    const float a = start + (end - start) * (i - 1) / 48, b = start + (end - start) * i / 48;
                    line({std::cos(a) * radius, std::sin(a) * radius, 0},
                         {std::cos(b) * radius, std::sin(b) * radius, 0});
                }
            for (float a : {start, end})
                line({std::cos(a) * render.ringInnerRadius, std::sin(a) * render.ringInnerRadius, 0},
                     {std::cos(a) * render.ringOuterRadius, std::sin(a) * render.ringOuterRadius, 0});
            const float mid = (start + end) * 0.5f;
            for (int i = 0; i < 4; ++i) {
                const float angle = i < 2 ? mid : i == 2 ? start : end;
                const float radius = i == 0 ? render.ringInnerRadius : render.ringOuterRadius;
                const char *properties[]{"ringInnerRadius", "ringOuterRadius", "ringStartAngleDegrees",
                                         "ringEndAngleDegrees"};
                VfxViewportHandleState handle;
                handle.id = properties[i];
                handle.layerId = layer.id;
                handle.targetId = layer.renderShape.id;
                handle.propertyId = properties[i];
                handle.initialValue = i < 2 ? radius : angle * 57.2957795f;
                handle.angular = i >= 2;
                handle.frame = matrix;
                marker(handle, Transform(matrix, {std::cos(angle) * radius, std::sin(angle) * radius, 0}),
                       Transform(matrix, {std::cos(angle), std::sin(angle), 0}, 0));
            }
        } else {
            MATH::Vec3 corners[8];
            for (int i = 0; i < 8; ++i)
                corners[i] = {(i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y,
                              (i & 4) ? half.z : -half.z};
            for (int i = 0; i < 8; ++i)
                for (int axis = 1; axis <= 4; axis *= 2)
                    if (!(i & axis))
                        line(corners[i], corners[i | axis]);
            if (!particles && (layer.renderShape.type == VFX::VfxRenderShapeType::MeshCylinder ||
                               layer.renderShape.type == VFX::VfxRenderShapeType::MeshCone ||
                               layer.renderShape.type == VFX::VfxRenderShapeType::CrossPlane)) {
                VfxViewportHandleState handle;
                handle.id = "beam.length";
                handle.layerId = layer.id;
                handle.propertyId = "length";
                const auto sample = VFX::EvaluateVfxLayerAnimation(
                    *compiled, previewElapsedSeconds_ - layer.startDelay, span, 0);
                handle.scaleValue = sample.scale;
                handle.initialValue =
                    timelineEdit_.autoKey ? sample.scale.y : layer.emitterTransform.transform.scale.y;
                handle.frame = matrix;
                const float inverseLength = 1 / (std::max)(std::abs(handle.initialValue), 0.001f);
                marker(handle, Transform(matrix, {0, 0.5f, 0}),
                       Transform(matrix, {0, 0.5f * inverseLength, 0}, 0));
            }
            if (particles && (layer.shape.type == VFX::VfxShapeType::Sphere ||
                              layer.shape.type == VFX::VfxShapeType::Cone ||
                              layer.shape.type == VFX::VfxShapeType::Cylinder ||
                              layer.shape.type == VFX::VfxShapeType::Circle)) {
                VfxViewportHandleState handle;
                handle.id = "emitter.radius";
                handle.layerId = layer.id;
                handle.targetId = layer.shape.id;
                handle.propertyId = "radius";
                handle.initialValue = layer.shape.radius.mode == VFX::VfxDistributionMode::Constant
                                          ? layer.shape.radius.minimum
                                          : layer.shape.radius.maximum;
                handle.frame = matrix;
                marker(handle, Transform(matrix, {half.x, 0, 0}), Transform(matrix, {1, 0, 0}, 0));
                if (layer.shape.type == VFX::VfxShapeType::Cone) {
                    const float angle = layer.shape.coneAngleDegrees * 0.01745329252f;
                    const float radius = std::tan((std::min)(angle, 1.4f));
                    for (int i = 0; i < 16; ++i) {
                        const float a = 6.2831853f * i / 16, b = 6.2831853f * (i + 1) / 16;
                        line({std::cos(a) * radius, 1, std::sin(a) * radius},
                             {std::cos(b) * radius, 1, std::sin(b) * radius});
                        if (i % 4 == 0)
                            line({0, 0, 0}, {std::cos(a) * radius, 1, std::sin(a) * radius});
                    }
                    handle.id = "emitter.cone";
                    handle.propertyId = "coneAngleDegrees";
                    handle.initialValue = layer.shape.coneAngleDegrees;
                    const float derivative = 0.01745329252f / (std::cos(angle) * std::cos(angle));
                    marker(handle, Transform(matrix, {radius, 1, 0}),
                           Transform(matrix, {derivative, 0, 0}, 0));
                }
            }
        }
        if (timelineEdit_.showMotionPath && !particles) {
            ImVec2 previous;
            bool valid = false;
            for (int i = 0; i <= 48; ++i) {
                VFX::VfxWorldTransform pose, parent;
                if (!playbackService_.GetLayerPoseAtTime(previewHandle_, layer.id,
                                                         layer.startDelay + span * i / 48, pose, parent))
                    break;
                ImVec2 point;
                const bool next = project(pose.position, point);
                if (valid && next)
                    draw.AddLine(previous, point, IM_COL32(100, 180, 230, 170), 1.5f);
                previous = point;
                valid = next;
            }
            if (layer.position.value.mode == VFX::VfxAnimatedValueMode::Curve) {
                std::vector<const VFX::VfxCurveKeyData *> keys;
                const auto &curves = layer.position.value.curve;
                for (const auto *curve : {&curves.x, &curves.y, &curves.z})
                    for (const auto &key : curve->keys)
                        if (std::none_of(keys.begin(), keys.end(), [&](const auto *k) {
                                return std::abs(k->time - key.time) < 0.00001f;
                            }))
                            keys.push_back(&key);
                // 同じ位置のキーは再生ヘッドに最も近い時刻から操作する。
                std::stable_sort(keys.begin(), keys.end(), [&](const auto* a, const auto* b) {
                    const auto distance = [&](const auto* key) {
                        return std::abs(layer.startDelay + layer.animationTiming.LayerSeconds(key->time, span) -
                                        previewElapsedSeconds_);
                    };
                    return distance(a) < distance(b);
                });
                const auto pathKnots = ReadVfxBezierPath(layer,span);
                for (const auto *key : keys) {
                    VFX::VfxWorldTransform pose, parent;
                    const float local = layer.animationTiming.LayerSeconds(key->time, span);
                    if (local < 0 || local > span)
                        continue;
                    const float seconds = layer.startDelay + local;
                    if (!playbackService_.GetLayerPoseAtTime(previewHandle_, layer.id, seconds, pose, parent))
                        continue;
                    VfxViewportHandleState handle;
                    handle.id = key->id;
                    handle.layerId = layer.id;
                    handle.pathKey = true;
                    handle.seconds = seconds;
                    handle.frame = MATH::Mat4::TRS(parent.position, parent.rotation, parent.scale);
                    marker(handle, pose.position, {});
                    const auto knot = std::find_if(pathKnots.begin(),pathKnots.end(),[&](const auto& k) {
                        return std::abs(k.seconds-seconds)<0.0001f;
                    });
                    if (knot!=pathKnots.end()) {
                        const auto control = [&](const MATH::Vec3& localPoint, const char* property) {
                            const auto worldPoint=Transform(handle.frame,localPoint);
                            ImVec2 a,b;
                            if (project(pose.position,a) && project(worldPoint,b)) draw.AddLine(a,b,IM_COL32(190,160,90,190));
                            auto tangent=handle; tangent.id+=".";tangent.id+=property;tangent.propertyId=property;
                            marker(tangent,worldPoint,{});
                        };
                        if (knot!=pathKnots.begin()) control(knot->incoming,"path.in");
                        if (std::next(knot)!=pathKnots.end()) control(knot->outgoing,"path.out");
                    }
                }
            }
        }
    }
    if (!viewportHandle_.id.empty()) {
        handleHit = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            EndTimelineEdit(true);
            viewportHandle_ = {};
            timelineEdit_.suppressDragUntilRelease = true;
        } else if (!ImGui::IsMouseDown(0)) {
            EndTimelineEdit(false);
            viewportHandle_ = {};
        } else if (rayValid && ImGui::IsMouseDragging(0)) {
            auto &h = viewportHandle_;
            if (h.pathKey) {
                const float denom = MATH::Dot(ray.direction, h.planeNormal);
                if (std::abs(denom) > 0.00001f) {
                    const float distance = MATH::Dot(h.worldPoint - ray.origin, h.planeNormal) / denom;
                    auto local = Transform(MATH::Inverse(h.frame), ray.origin + ray.direction * distance);
                    if (layerGizmoState_.snapEnabled) {
                        const auto s = layerGizmoState_.translateSnap;
                        if (s.x > 0)
                            local.x = std::round(local.x / s.x) * s.x;
                        if (s.y > 0)
                            local.y = std::round(local.y / s.y) * s.y;
                        if (s.z > 0)
                            local.z = std::round(local.z / s.z) * s.z;
                    }
                    if (h.propertyId=="path.in" || h.propertyId=="path.out")
                        (void)SetLayerPathControl(h.layerId,h.seconds,h.propertyId=="path.in",local,statusMessage_,VfxTimelineEditState::mergeGroup);
                    else (void)SetLayerTransformKey(h.layerId, "position", h.seconds, local, statusMessage_,
                                               VfxTimelineEditState::mergeGroup);
                }
            } else {
                const float axisLength = h.screenAxis.x * h.screenAxis.x + h.screenAxis.y * h.screenAxis.y;
                float value = h.initialValue;
                if (h.angular) {
                    const auto inverse = MATH::Inverse(h.frame);
                    const auto o = Transform(inverse, ray.origin), d = Transform(inverse, ray.direction, 0);
                    if (std::abs(d.z) > 0.00001f) {
                        const auto p = o + d * (-o.z / d.z);
                        value = std::atan2(p.y, p.x) * 57.2957795f;
                        value += std::round((h.initialValue - value) / 360) * 360;
                    }
                } else if (axisLength > 0.01f)
                    value += (mouse.x - h.startMouse.x) * h.screenAxis.x / axisLength +
                             (mouse.y - h.startMouse.y) * h.screenAxis.y / axisLength;
                auto next = document_.Asset();
                auto *layer = FindEmitterMutable(next, h.layerId);
                if (layer) {
                    if (h.propertyId == "radius") {
                        layer->shape.radius = VFX::MakeVfxFloatConstant((std::max)(0.0f, value));
                        (void)ApplyAsset(std::move(next), statusMessage_, VfxTimelineEditState::mergeGroup);
                    } else if (h.propertyId == "length") {
                        value = (std::max)(0.001f, value);
                        if (timelineEdit_.autoKey) {
                            const float end = layer->activeDuration > 0
                                                  ? layer->startDelay + layer->activeDuration
                                                  : next.durationSeconds;
                            auto scale = h.scaleValue;
                            scale.y = value;
                            (void)SetLayerTransformKey(
                                h.layerId, "scale",
                                std::clamp(previewElapsedSeconds_, layer->startDelay, end), scale,
                                statusMessage_, VfxTimelineEditState::mergeGroup);
                        } else {
                            layer->emitterTransform.transform.scale.y = value;
                            (void)ApplyAsset(std::move(next), statusMessage_,
                                             VfxTimelineEditState::mergeGroup);
                        }
                    } else if (h.propertyId == "coneAngleDegrees") {
                        layer->shape.coneAngleDegrees = std::clamp(value, 0.0f, 80.0f);
                        (void)ApplyAsset(std::move(next), statusMessage_, VfxTimelineEditState::mergeGroup);
                    } else {
                        const bool animated =
                            timelineEdit_.autoKey ||
                            std::any_of(layer->parameterTracks.begin(), layer->parameterTracks.end(),
                                        [&](const auto &t) {
                                            return t.targetId == h.targetId && t.propertyId == h.propertyId;
                                        });
                        if (animated) {
                            const float end = layer->activeDuration > 0
                                                  ? layer->startDelay + layer->activeDuration
                                                  : next.durationSeconds;
                            (void)SetLayerParameterKey(
                                h.layerId, h.targetId, h.propertyId,
                                std::clamp(previewElapsedSeconds_, layer->startDelay, end), value,
                                statusMessage_, VfxTimelineEditState::mergeGroup);
                        } else
                            (void)SetProperty(h.targetId, h.propertyId, value, statusMessage_,
                                              VfxTimelineEditState::mergeGroup);
                    }
                }
            }
        }
    } else if (canPick && !handleHit)
        pickedLayer = std::move(picked);
    draw.PopClipRect();
    return handleHit;
#else
    (void)rect;
    (void)hovered;
    (void)gizmoInteracting;
    (void)pickedLayer;
    return false;
#endif
}
} // namespace HIKARI::EDITOR
