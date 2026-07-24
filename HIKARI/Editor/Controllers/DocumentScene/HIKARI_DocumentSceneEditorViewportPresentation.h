#pragma once

#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

struct ImVec2;

namespace HIKARI {

    class DocumentSceneBase;

    namespace EDITOR {
        namespace DOCUMENT_SCENE {

            void DrawViewportDebugOptions(
                ViewportOverlayState& overlays,
                ViewportPerformanceState& performance);
            bool DrawRenderDebugViewCombo(
                const char* label,
                ViewportDebugViewState& debugView,
                float width);
            void DrawReflectionProbeLabels(
                DocumentSceneBase& scene,
                const ViewportOverlayState& overlays,
                const ImVec2& viewportOrigin,
                const ImVec2& viewportSize);
            void DrawLightOverlayIcons(
                DocumentSceneBase& scene,
                const ViewportOverlayState& overlays,
                const ImVec2& viewportOrigin,
                const ImVec2& viewportSize);
            bool CanEditReflectionProbeTarget(
                const ReflectionProbeSettings& probe,
                ReflectionProbeEditTarget target);
            TransformData BuildReflectionProbeEditTransform(
                const ReflectionProbeSettings& probe,
                ReflectionProbeEditTarget target);
            EditorTransformGizmoState BuildReflectionProbeGizmoState(
                const EditorTransformGizmoState& source,
                ReflectionProbeEditTarget target);
            void ApplyReflectionProbeEditTransform(
                ReflectionProbeSettings& probe,
                ReflectionProbeEditTarget target,
                const EditorTransformGizmoResult& result,
                EditorTransformGizmoOperation operation);

        } // namespace DOCUMENT_SCENE
    } // namespace EDITOR

} // namespace HIKARI
