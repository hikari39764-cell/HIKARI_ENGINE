#include "Editor/Viewport/HIKARI_ViewportAuthoringToolbar.h"

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    ViewportAuthoringToolbarResult DrawViewportAuthoringToolbar(
        EditorContext& context,
        bool scaleDisabled) {
        ViewportAuthoringToolbarResult result{};
#if defined(HIKARI_WITH_EDITOR)
        const ImVec2 buttonSize{ 26.0f, 26.0f };
        const auto sameLine = [] {
            ImGui::SameLine(0.0f, 4.0f);
        };

        if (IconToggleButton(
                EditorGlyph::Transform,
                "ViewportTransformEnabled",
                context.transformGizmo.enabled,
                buttonSize,
                "Enable transform gizmo")) {
            context.transformGizmo.enabled = !context.transformGizmo.enabled;
        }
        sameLine();
        if (IconToggleButton(
                EditorGlyph::Translate,
                "ViewportTransformTranslate",
                context.transformGizmo.operation ==
                    EditorTransformGizmoOperation::Translate,
                buttonSize,
                "Translate (W)")) {
            context.transformGizmo.operation =
                EditorTransformGizmoOperation::Translate;
            context.transformGizmo.enabled = true;
        }
        sameLine();
        if (IconToggleButton(
                EditorGlyph::Rotate,
                "ViewportTransformRotate",
                context.transformGizmo.operation ==
                    EditorTransformGizmoOperation::Rotate,
                buttonSize,
                "Rotate (E)")) {
            context.transformGizmo.operation =
                EditorTransformGizmoOperation::Rotate;
            context.transformGizmo.enabled = true;
        }
        sameLine();
        if (scaleDisabled) {
            ImGui::BeginDisabled();
        }
        const bool scaleClicked = IconToggleButton(
            EditorGlyph::Scale,
            "ViewportTransformScale",
            context.transformGizmo.operation ==
                EditorTransformGizmoOperation::Scale,
            buttonSize,
            scaleDisabled
                ? "Camera scale does not affect its lens"
                : "Scale (R)");
        if (scaleDisabled) {
            ImGui::EndDisabled();
        }
        if (scaleClicked) {
            context.transformGizmo.operation =
                EditorTransformGizmoOperation::Scale;
            context.transformGizmo.enabled = true;
        }

        sameLine();
        const bool localSpace = context.transformGizmo.mode ==
            EditorTransformGizmoMode::Local;
        if (IconToggleButton(
                localSpace
                    ? EditorGlyph::LocalSpace
                    : EditorGlyph::WorldSpace,
                "ViewportTransformSpace",
                localSpace,
                buttonSize,
                localSpace
                    ? "Local transform space (Q)"
                    : "World transform space (Q)")) {
            context.transformGizmo.mode = localSpace
                ? EditorTransformGizmoMode::World
                : EditorTransformGizmoMode::Local;
        }
        sameLine();
        if (IconToggleButton(
                EditorGlyph::Snap,
                "ViewportSnap",
                context.transformGizmo.snapEnabled,
                buttonSize,
                "Transform snapping")) {
            context.transformGizmo.snapEnabled =
                !context.transformGizmo.snapEnabled;
        }
        sameLine();
        if (IconToggleButton(
                EditorGlyph::Grid,
                "ViewportGrid",
                context.overlays.showGrid,
                buttonSize,
                "Viewport grid")) {
            context.overlays.showGrid = !context.overlays.showGrid;
        }
        sameLine();
        if (IconToggleButton(
                EditorGlyph::Light,
                "ViewportLights",
                context.overlays.showLights,
                buttonSize,
                "Light icons")) {
            context.overlays.showLights = !context.overlays.showLights;
        }
        sameLine();
        if (IconToggleButton(
                EditorGlyph::Gizmo,
                "ViewportComponentGizmos",
                context.gizmos.showComponentGizmos,
                buttonSize,
                "Component gizmos")) {
            context.gizmos.showComponentGizmos =
                !context.gizmos.showComponentGizmos;
        }
        sameLine();
        result.settingsRequested = IconButton(
            EditorGlyph::Settings,
            "ViewportToolSettings",
            EditorButtonTone::Quiet,
            buttonSize,
            "Viewport options");
#else
        (void)context;
        (void)scaleDisabled;
#endif
        return result;
    }

} // namespace HIKARI::EDITOR
