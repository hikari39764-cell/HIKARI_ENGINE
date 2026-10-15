#include "Editor/Viewport/HIKARI_ViewportAuthoringToolbar.h"

#include "Editor/HIKARI_EditorContext.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

ViewportAuthoringToolbarResult DrawViewportAuthoringToolbar(EditorContext& context, bool scaleDisabled) {
    ViewportAuthoringToolbarResult result{};
    const ImVec2 buttonSize{26.0f, 26.0f};
    const auto sameLine = [] { ImGui::SameLine(0.0f, 4.0f); };

    if (IconToggleButton(EditorGlyph::Transform, "ViewportTransformEnabled", context.transformGizmo.enabled, buttonSize,
                         TEXT::Localize("ui.viewport.tooltipTransform").c_str())) {
        context.transformGizmo.enabled = !context.transformGizmo.enabled;
    }
    sameLine();
    if (IconToggleButton(EditorGlyph::Translate, "ViewportTransformTranslate",
                         context.transformGizmo.operation == EditorTransformGizmoOperation::Translate, buttonSize,
                         TEXT::Localize("ui.viewport.tooltipTranslate").c_str())) {
        context.transformGizmo.operation = EditorTransformGizmoOperation::Translate;
        context.transformGizmo.enabled = true;
    }
    sameLine();
    if (IconToggleButton(EditorGlyph::Rotate, "ViewportTransformRotate",
                         context.transformGizmo.operation == EditorTransformGizmoOperation::Rotate, buttonSize,
                         TEXT::Localize("ui.viewport.tooltipRotate").c_str())) {
        context.transformGizmo.operation = EditorTransformGizmoOperation::Rotate;
        context.transformGizmo.enabled = true;
    }
    sameLine();
    if (scaleDisabled) {
        ImGui::BeginDisabled();
    }
    const bool scaleClicked =
        IconToggleButton(EditorGlyph::Scale, "ViewportTransformScale",
                         context.transformGizmo.operation == EditorTransformGizmoOperation::Scale, buttonSize,
                         scaleDisabled ? TEXT::Localize("ui.viewport.tooltipScaleDisabled").c_str()
                                       : TEXT::Localize("ui.viewport.tooltipScale").c_str());
    if (scaleDisabled) {
        ImGui::EndDisabled();
    }
    if (scaleClicked) {
        context.transformGizmo.operation = EditorTransformGizmoOperation::Scale;
        context.transformGizmo.enabled = true;
    }

    sameLine();
    const bool localSpace = context.transformGizmo.mode == EditorTransformGizmoMode::Local;
    if (IconToggleButton(localSpace ? EditorGlyph::LocalSpace : EditorGlyph::WorldSpace, "ViewportTransformSpace",
                         localSpace, buttonSize,
                         localSpace ? TEXT::Localize("ui.viewport.tooltipLocalSpace").c_str()
                                    : TEXT::Localize("ui.viewport.tooltipWorldSpace").c_str())) {
        context.transformGizmo.mode = localSpace ? EditorTransformGizmoMode::World : EditorTransformGizmoMode::Local;
    }
    sameLine();
    if (IconToggleButton(EditorGlyph::Snap, "ViewportSnap", context.transformGizmo.snapEnabled, buttonSize,
                         TEXT::Localize("ui.viewport.tooltipSnapping").c_str())) {
        context.transformGizmo.snapEnabled = !context.transformGizmo.snapEnabled;
    }
    sameLine();
    if (IconToggleButton(EditorGlyph::Grid, "ViewportGrid", context.overlays.showGridFloor, buttonSize,
                         TEXT::Localize("ui.viewport.tooltipGridFloor").c_str())) {
        context.overlays.showGridFloor = !context.overlays.showGridFloor;
    }
    sameLine();
    if (IconToggleButton(EditorGlyph::Light, "ViewportLights", context.overlays.showLights, buttonSize,
                         TEXT::Localize("ui.viewport.tooltipLights").c_str())) {
        context.overlays.showLights = !context.overlays.showLights;
    }
    sameLine();
    if (IconToggleButton(EditorGlyph::Gizmo, "ViewportComponentGizmos", context.gizmos.showComponentGizmos, buttonSize,
                         TEXT::Localize("ui.viewport.tooltipComponentGizmos").c_str())) {
        context.gizmos.showComponentGizmos = !context.gizmos.showComponentGizmos;
    }
    sameLine();
    result.settingsRequested = IconButton(EditorGlyph::Settings, "ViewportToolSettings", EditorButtonTone::Quiet,
                                          buttonSize, TEXT::Localize("ui.viewport.tooltipOptions").c_str());
    return result;
}

} // namespace HIKARI::EDITOR
