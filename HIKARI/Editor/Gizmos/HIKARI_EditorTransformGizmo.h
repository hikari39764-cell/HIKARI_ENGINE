#pragma once

#include "Editor/HIKARI_EditorContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    class GameObject;

    namespace EDITOR {

        struct EditorViewportRect {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
        };

        struct EditorTransformGizmoResult {
            bool changed = false;
            bool interacting = false;
            bool manipulating = false;
            TransformData transform{};
            MATH::Quat rotation = MATH::Quat::Identity();
        };

        class EditorTransformGizmo {
        public:
            EditorTransformGizmoResult Draw(
                GameObject& object,
                const Camera3D& camera,
                const EditorTransformGizmoState& state,
                const EditorViewportRect& viewportRect) const;

            EditorTransformGizmoResult DrawTransform(
                const TransformData& transform,
                const Camera3D& camera,
                const EditorTransformGizmoState& state,
                const EditorViewportRect& viewportRect) const;
        };

    } // namespace EDITOR

} // namespace HIKARI
