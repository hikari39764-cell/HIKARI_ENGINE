#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"

#include "Core/HIKARI_MathConfig.h"

#include "imgui.h"
#include "ImGuizmo.h"

#include "Core/HIKARI_Logger.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/HIKARI_GameObject.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {

namespace {
constexpr float kDegreesToRadians = MATH::kDegreesToRadians;

struct DecomposedGizmoMatrix {
    TransformData transform{};
    MATH::Quat rotation = MATH::Quat::Identity();
};

void CopyMat4ToFloat16(const MATH::Mat4& matrix, float out[16]) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            out[column * 4 + row] = matrix.m[column][row];
        }
    }
}

bool IsFinite(const DecomposedGizmoMatrix& value) {
    return MATH::IsFinite(value.transform.position) && MATH::IsFinite(value.transform.rotationEulerDeg) &&
           MATH::IsFinite(value.transform.scale) && MATH::IsFinite(value.rotation);
}
ImGuizmo::OPERATION ToImGuizmoOperation(EditorTransformGizmoOperation operation) {
    switch (operation) {
    case EditorTransformGizmoOperation::Rotate: return ImGuizmo::ROTATE;
    case EditorTransformGizmoOperation::Scale: return ImGuizmo::SCALE;
    case EditorTransformGizmoOperation::Translate:
    default: return ImGuizmo::TRANSLATE;
    }
}

ImGuizmo::MODE ToImGuizmoMode(EditorTransformGizmoMode mode) {
    return mode == EditorTransformGizmoMode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
}

void BuildSnapValues(const EditorTransformGizmoState& state, float outSnap[3]) {
    switch (state.operation) {
    case EditorTransformGizmoOperation::Rotate:
        outSnap[0] = state.rotateSnapDeg;
        outSnap[1] = state.rotateSnapDeg;
        outSnap[2] = state.rotateSnapDeg;
        break;
    case EditorTransformGizmoOperation::Scale:
        outSnap[0] = state.scaleSnap;
        outSnap[1] = state.scaleSnap;
        outSnap[2] = state.scaleSnap;
        break;
    case EditorTransformGizmoOperation::Translate:
    default:
        outSnap[0] = state.translateSnap.x;
        outSnap[1] = state.translateSnap.y;
        outSnap[2] = state.translateSnap.z;
        break;
    }
}

DecomposedGizmoMatrix DecomposeEditedMatrix(const float matrix[16]) {
    MATH::Mat4 value{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            value.m[column][row] = matrix[column * 4 + row];
    DecomposedGizmoMatrix result{};
    if (!MATH::DecomposeTRS(value, result.transform.position, result.rotation, result.transform.scale)) {
        result.transform.scale = {NAN, NAN, NAN};
    }
    result.transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(result.rotation);
    return result;
}
DecomposedGizmoMatrix MakeGizmoTransform(const Transform3D& transform) {
    if (transform.useExplicitMatrix) {
        float matrix[16]{};
        CopyMat4ToFloat16(transform.explicitMatrix, matrix);
        return DecomposeEditedMatrix(matrix);
    }

    DecomposedGizmoMatrix result{};
    result.transform.position = transform.position;
    result.transform.scale = transform.scale;
    result.rotation = MATH::NormalizeQ(transform.rotation);
    result.transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(result.rotation);
    return result;
}

DecomposedGizmoMatrix MakeGizmoTransform(const TransformData& transform) {
    DecomposedGizmoMatrix result{};
    result.transform = transform;
    result.rotation = MATH::NormalizeQ(MATH::Quat::FromEulerXYZ(transform.rotationEulerDeg.x * kDegreesToRadians,
                                                                transform.rotationEulerDeg.y * kDegreesToRadians,
                                                                transform.rotationEulerDeg.z * kDegreesToRadians));
    return result;
}

bool BuildRotationFrame(const Transform3D& transform, MATH::Mat4& frame) {
    const auto local = MakeGizmoTransform(transform);
    if (!IsFinite(local))
        return false;
    frame = MATH::Mat4::Rotate(local.rotation);
    if (transform.GetParent() && !transform.useExplicitMatrix) {
        MATH::Mat4 parent;
        if (!BuildRotationFrame(*transform.GetParent(), parent))
            return false;
        frame = parent * frame;
    }
    return true;
}

DecomposedGizmoMatrix SelectEditedComponent(const DecomposedGizmoMatrix& initial, const DecomposedGizmoMatrix& edited,
                                            EditorTransformGizmoOperation operation) {
    DecomposedGizmoMatrix result = initial;
    switch (operation) {
    case EditorTransformGizmoOperation::Rotate:
        result.rotation = edited.rotation;
        result.transform.rotationEulerDeg =
            MATH::EulerXYZDegreesFromQuatNearest(result.rotation, initial.transform.rotationEulerDeg);
        break;
    case EditorTransformGizmoOperation::Scale:
        result.transform.scale = {
            std::copysign(std::max(0.001f, std::abs(edited.transform.scale.x)), initial.transform.scale.x),
            std::copysign(std::max(0.001f, std::abs(edited.transform.scale.y)), initial.transform.scale.y),
            std::copysign(std::max(0.001f, std::abs(edited.transform.scale.z)), initial.transform.scale.z)};
        break;
    case EditorTransformGizmoOperation::Translate:
    default: result.transform.position = edited.transform.position; break;
    }
    return result;
}

EditorTransformGizmoResult DrawTransformMatrix(const Camera3D& camera, const EditorTransformGizmoState& state,
                                               const EditorViewportRect& viewportRect, const void* stableId,
                                               const DecomposedGizmoMatrix& initial, float model[16]) {
    EditorTransformGizmoResult result{};
    if (!state.enabled || !IsFinite(initial) || viewportRect.width <= 1.0f || viewportRect.height <= 1.0f) {
        return result;
    }

    // 回転ハンドルにはスケールを渡さず、鏡像や非一様スケールを保持する。
    if (state.operation == EditorTransformGizmoOperation::Rotate)
        CopyMat4ToFloat16(MATH::Mat4::TRS(initial.transform.position, initial.rotation, {1, 1, 1}), model);

    float view[16]{};
    float projection[16]{};
    CopyMat4ToFloat16(camera.GetView(), view);
    CopyMat4ToFloat16(camera.GetProj(), projection);

    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewportRect.x, viewportRect.y, viewportRect.width, viewportRect.height);

    float snap[3]{};
    const float* snapPtr = nullptr;
    if (state.snapEnabled) {
        BuildSnapValues(state, snap);
        snapPtr = snap;
    }

    // The caller applies the per-object transform delta.
    ImGuizmo::PushID(stableId);
    result.changed = ImGuizmo::Manipulate(view, projection, ToImGuizmoOperation(state.operation),
                                          ToImGuizmoMode(state.mode), model, nullptr, snapPtr);
    result.manipulating = ImGuizmo::IsUsing();
    result.interacting = ImGuizmo::IsOver() || result.manipulating;

    if (result.changed) {
        const DecomposedGizmoMatrix edited = DecomposeEditedMatrix(model);
        if (IsFinite(edited)) {
            const DecomposedGizmoMatrix selected = SelectEditedComponent(initial, edited, state.operation);
            result.transform = selected.transform;
            result.rotation = selected.rotation;
        } else {
            result.changed = false;
            HIKARI_LOG_WARN("[EditorTransformGizmo] rejected non-finite transform output");
        }
    }
    ImGuizmo::PopID();
    return result;
}
} // namespace

EditorTransformGizmoResult EditorTransformGizmo::Draw(GameObject& object, const Camera3D& camera,
                                                      const EditorTransformGizmoState& state,
                                                      const EditorViewportRect& viewportRect) const {
    EditorTransformGizmoResult result{};

    const Transform3D& sourceTransform = object.GetTransform();
    float model[16]{};
    CopyMat4ToFloat16(sourceTransform.GetWorldMatrix(), model);
    auto initial = object.GetParent() && !sourceTransform.useExplicitMatrix &&
                           state.operation != EditorTransformGizmoOperation::Rotate
                       ? DecomposeEditedMatrix(model)
                       : MakeGizmoTransform(sourceTransform);
    auto parentRotation = MATH::Mat4::Identity();
    if (state.operation == EditorTransformGizmoOperation::Rotate && object.GetParent() &&
        !sourceTransform.useExplicitMatrix) {
        // 非一様スケールによるせん断を毎フレーム回転へ混入させない。
        if (!BuildRotationFrame(object.GetParent()->GetTransform(), parentRotation))
            return result;
        MATH::Vec3 unusedPosition, unusedScale;
        if (!MATH::DecomposeTRS(parentRotation * MATH::Mat4::Rotate(initial.rotation), unusedPosition, initial.rotation,
                                unusedScale))
            return result;
        initial.transform.position = {model[12], model[13], model[14]};
        initial.transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(initial.rotation);
    }

    // 同一ビューでも選択対象が変われば、ドラッグを引き継がない。
    const auto targetId = object.GetDocumentId().value;
    ImGuizmo::PushID(targetId ? std::to_string(targetId).c_str() : object.GetName().c_str());
    result = DrawTransformMatrix(camera, state, viewportRect, this, initial, model);
    ImGuizmo::PopID();
    if (result.changed) {
        Transform3D runtimeTransform = sourceTransform;
        if (sourceTransform.useExplicitMatrix && state.operation != EditorTransformGizmoOperation::Translate) {
            runtimeTransform.position = initial.transform.position;
            runtimeTransform.rotation = initial.rotation;
            runtimeTransform.scale = initial.transform.scale;
        }
        auto local = MATH::Mat4::TRS(result.transform.position, result.rotation, result.transform.scale);
        if (state.operation != EditorTransformGizmoOperation::Rotate && object.GetParent() &&
            !sourceTransform.useExplicitMatrix)
            local = MATH::Inverse(object.GetParent()->GetTransform().GetWorldMatrix()) * local;
        if (state.operation == EditorTransformGizmoOperation::Translate) {
            runtimeTransform.position = {local.m[3][0], local.m[3][1], local.m[3][2]};
            if (sourceTransform.useExplicitMatrix) {
                runtimeTransform.explicitMatrix.m[3][0] = runtimeTransform.position.x;
                runtimeTransform.explicitMatrix.m[3][1] = runtimeTransform.position.y;
                runtimeTransform.explicitMatrix.m[3][2] = runtimeTransform.position.z;
            }
        } else {
            const auto rotationMatrix = state.operation == EditorTransformGizmoOperation::Rotate
                                            ? MATH::Inverse(parentRotation) * MATH::Mat4::Rotate(result.rotation)
                                            : local;
            MATH::Vec3 position{}, scale{};
            MATH::Quat rotation{};
            if (!MATH::DecomposeTRS(rotationMatrix, position, rotation, scale) || !MATH::IsFinite(rotation) ||
                !MATH::IsFinite(scale)) {
                result.changed = false;
                return result;
            }
            runtimeTransform.useExplicitMatrix = false;
            if (state.operation == EditorTransformGizmoOperation::Rotate)
                runtimeTransform.rotation = rotation;
            else
                runtimeTransform.scale = {std::copysign(std::abs(scale.x), runtimeTransform.scale.x),
                                          std::copysign(std::abs(scale.y), runtimeTransform.scale.y),
                                          std::copysign(std::abs(scale.z), runtimeTransform.scale.z)};
        }
        result.changed = object.SetLocalTransform(runtimeTransform);
        const auto applied = MakeGizmoTransform(runtimeTransform);
        result.transform = applied.transform;
        result.rotation = applied.rotation;
    }

    return result;
}

EditorTransformGizmoResult EditorTransformGizmo::DrawTransform(const TransformData& transform, const Camera3D& camera,
                                                               const EditorTransformGizmoState& state,
                                                               const EditorViewportRect& viewportRect) const {
    EditorTransformGizmoResult result{};

    const DecomposedGizmoMatrix initial = MakeGizmoTransform(transform);
    float model[16]{};
    CopyMat4ToFloat16(MATH::Mat4::TRS(transform.position, initial.rotation, transform.scale), model);
    result = DrawTransformMatrix(camera, state, viewportRect, this, initial, model);

    return result;
}

} // namespace HIKARI::EDITOR
