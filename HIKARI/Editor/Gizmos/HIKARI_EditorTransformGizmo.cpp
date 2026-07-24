#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "ImGuizmo.h"
#endif

#include "Core/HIKARI_Logger.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/HIKARI_GameObject.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {

#if defined(HIKARI_WITH_EDITOR)
    namespace {
        constexpr float kDegreesToRadians = 0.01745329251994329577f;

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

        MATH::Vec3 NormalizeAxis(float x, float y, float z) {
            const float length = std::sqrt(x * x + y * y + z * z);
            if (length <= 1.0e-6f) {
                return { 0.0f, 0.0f, 0.0f };
            }

            const float invLength = 1.0f / length;
            return { x * invLength, y * invLength, z * invLength };
        }

        bool IsFinite(const DecomposedGizmoMatrix& value) {
            return MATH::IsFinite(value.transform.position) &&
                MATH::IsFinite(value.transform.rotationEulerDeg) &&
                MATH::IsFinite(value.transform.scale) &&
                MATH::IsFinite(value.rotation);
        }

        MATH::Quat ExtractRotationFromMatrix(const float matrix[16]) {
            const MATH::Vec3 right = NormalizeAxis(matrix[0], matrix[1], matrix[2]);
            const MATH::Vec3 up = NormalizeAxis(matrix[4], matrix[5], matrix[6]);
            const MATH::Vec3 dir = NormalizeAxis(matrix[8], matrix[9], matrix[10]);

            const float m00 = right.x;
            const float m01 = up.x;
            const float m02 = dir.x;
            const float m10 = right.y;
            const float m11 = up.y;
            const float m12 = dir.y;
            const float m20 = right.z;
            const float m21 = up.z;
            const float m22 = dir.z;

            MATH::Quat q{};
            const float trace = m00 + m11 + m22;
            if (trace > 0.0f) {
                const float s = std::sqrt(trace + 1.0f) * 2.0f;
                q.w = 0.25f * s;
                q.x = (m21 - m12) / s;
                q.y = (m02 - m20) / s;
                q.z = (m10 - m01) / s;
            } else if (m00 > m11 && m00 > m22) {
                const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
                q.w = (m21 - m12) / s;
                q.x = 0.25f * s;
                q.y = (m01 + m10) / s;
                q.z = (m02 + m20) / s;
            } else if (m11 > m22) {
                const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
                q.w = (m02 - m20) / s;
                q.x = (m01 + m10) / s;
                q.y = 0.25f * s;
                q.z = (m12 + m21) / s;
            } else {
                const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
                q.w = (m10 - m01) / s;
                q.x = (m02 + m20) / s;
                q.y = (m12 + m21) / s;
                q.z = 0.25f * s;
            }

            return MATH::NormalizeQ(q);
        }

        ImGuizmo::OPERATION ToImGuizmoOperation(EditorTransformGizmoOperation operation) {
            switch (operation) {
            case EditorTransformGizmoOperation::Rotate:
                return ImGuizmo::ROTATE;
            case EditorTransformGizmoOperation::Scale:
                return ImGuizmo::SCALE;
            case EditorTransformGizmoOperation::Translate:
            default:
                return ImGuizmo::TRANSLATE;
            }
        }

        ImGuizmo::MODE ToImGuizmoMode(EditorTransformGizmoMode mode) {
            return mode == EditorTransformGizmoMode::Local
                ? ImGuizmo::LOCAL
                : ImGuizmo::WORLD;
        }

        void BuildSnapValues(
            const EditorTransformGizmoState& state,
            float outSnap[3]) {

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
            float translation[3]{};
            float rotationDeg[3]{};
            float scale[3]{};
            ImGuizmo::DecomposeMatrixToComponents(matrix, translation, rotationDeg, scale);

            DecomposedGizmoMatrix decomposed{};
            decomposed.transform.position = { translation[0], translation[1], translation[2] };
            decomposed.transform.scale = {
                (std::max)(0.001f, scale[0]),
                (std::max)(0.001f, scale[1]),
                (std::max)(0.001f, scale[2])
            };
            // Keep the edited orientation in quaternion form to avoid Euler discontinuities.
            decomposed.rotation = ExtractRotationFromMatrix(matrix);
            decomposed.transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(decomposed.rotation);
            return decomposed;
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
            result.transform.rotationEulerDeg =
                MATH::EulerXYZDegreesFromQuat(result.rotation);
            return result;
        }

        DecomposedGizmoMatrix MakeGizmoTransform(const TransformData& transform) {
            DecomposedGizmoMatrix result{};
            result.transform = transform;
            result.rotation = MATH::NormalizeQ(MATH::Quat::FromEulerXYZ(
                transform.rotationEulerDeg.x * kDegreesToRadians,
                transform.rotationEulerDeg.y * kDegreesToRadians,
                transform.rotationEulerDeg.z * kDegreesToRadians));
            result.transform.rotationEulerDeg =
                MATH::EulerXYZDegreesFromQuat(result.rotation);
            return result;
        }

        DecomposedGizmoMatrix SelectEditedComponent(
            const DecomposedGizmoMatrix& initial,
            const DecomposedGizmoMatrix& edited,
            EditorTransformGizmoOperation operation) {

            DecomposedGizmoMatrix result = initial;
            switch (operation) {
            case EditorTransformGizmoOperation::Rotate:
                result.rotation = edited.rotation;
                result.transform.rotationEulerDeg =
                    MATH::EulerXYZDegreesFromQuat(result.rotation);
                break;
            case EditorTransformGizmoOperation::Scale:
                result.transform.scale = edited.transform.scale;
                break;
            case EditorTransformGizmoOperation::Translate:
            default:
                result.transform.position = edited.transform.position;
                break;
            }
            return result;
        }

        EditorTransformGizmoResult DrawTransformMatrix(
            const Camera3D& camera,
            const EditorTransformGizmoState& state,
            const EditorViewportRect& viewportRect,
            const void* stableId,
            const DecomposedGizmoMatrix& initial,
            float model[16]) {

            EditorTransformGizmoResult result{};
            if (!state.enabled || viewportRect.width <= 1.0f || viewportRect.height <= 1.0f) {
                return result;
            }

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
            result.changed = ImGuizmo::Manipulate(
                view,
                projection,
                ToImGuizmoOperation(state.operation),
                ToImGuizmoMode(state.mode),
                model,
                nullptr,
                snapPtr);
            result.manipulating = ImGuizmo::IsUsing();
            result.interacting = ImGuizmo::IsOver() || result.manipulating;

            if (result.changed) {
                const DecomposedGizmoMatrix edited = DecomposeEditedMatrix(model);
                if (IsFinite(edited)) {
                    const DecomposedGizmoMatrix selected = SelectEditedComponent(
                        initial,
                        edited,
                        state.operation);
                    result.transform = selected.transform;
                    result.rotation = selected.rotation;
                } else {
                    result.changed = false;
                    HIKARI_LOG_WARN(
                        "[EditorTransformGizmo] rejected non-finite transform output");
                }
            }
            ImGuizmo::PopID();
            return result;
        }
    }
#endif

    EditorTransformGizmoResult EditorTransformGizmo::Draw(
        GameObject& object,
        const Camera3D& camera,
        const EditorTransformGizmoState& state,
        const EditorViewportRect& viewportRect) const {

        EditorTransformGizmoResult result{};

#if defined(HIKARI_WITH_EDITOR)
        const Transform3D& sourceTransform = object.GetTransform();
        const DecomposedGizmoMatrix initial =
            MakeGizmoTransform(sourceTransform);
        float model[16]{};
        CopyMat4ToFloat16(sourceTransform.GetLocalMatrix(), model);

        result = DrawTransformMatrix(
            camera,
            state,
            viewportRect,
            this,
            initial,
            model);
        if (result.changed) {
            Transform3D runtimeTransform = sourceTransform;
            runtimeTransform.useExplicitMatrix = false;
            runtimeTransform.position = result.transform.position;
            runtimeTransform.scale = result.transform.scale;
            runtimeTransform.rotation = result.rotation;
            (void)object.SetLocalTransform(runtimeTransform);
        }
#else
        (void)object;
        (void)camera;
        (void)state;
        (void)viewportRect;
#endif

        return result;
    }

    EditorTransformGizmoResult EditorTransformGizmo::DrawTransform(
        const TransformData& transform,
        const Camera3D& camera,
        const EditorTransformGizmoState& state,
        const EditorViewportRect& viewportRect) const {

        EditorTransformGizmoResult result{};

#if defined(HIKARI_WITH_EDITOR)
        const DecomposedGizmoMatrix initial = MakeGizmoTransform(transform);
        float model[16]{};
        CopyMat4ToFloat16(
            MATH::Mat4::TRS(
                transform.position,
                initial.rotation,
                transform.scale),
            model);
        result = DrawTransformMatrix(
            camera,
            state,
            viewportRect,
            this,
            initial,
            model);
#else
        (void)transform;
        (void)camera;
        (void)state;
        (void)viewportRect;
#endif

        return result;
    }

} // namespace HIKARI::EDITOR
