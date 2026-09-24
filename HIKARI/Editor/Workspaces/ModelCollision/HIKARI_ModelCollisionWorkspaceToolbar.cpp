#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"

#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelCollisionWorkspaceController::DrawPreviewToolbar(DocumentSceneBase& scene,
                                                           ModelCollisionWorkspaceResult& result,
                                                           EditorCommandRouter& commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
    (void)scene;
    constexpr ImVec2 kIconSize{28.0f, 28.0f};
    const std::string title = IsEditingModel() ? modelDisplayName_ : std::string("No model open");

    ToolbarLabel(title.c_str());
    if (IsEditingModel()) {
        NextToolbarItem();
        StatusBadge(history_.IsDirty() ? TEXT::LocalizeCStr("ui.vfx.text.Unsaved")
                                       : TEXT::LocalizeCStr("ui.projectFeatures.saved"),
                    history_.IsDirty() ? EditorStatusTone::Warning : EditorStatusTone::Ready);
    }

    NextToolbarItem();
    ImGui::BeginDisabled(!history_.IsDirty() || !commandRouter.CanExecute(EditorCommandId::SaveDocument));
    if (IconButton(EditorGlyph::Save, "CollisionSave",
                   history_.IsDirty() ? EditorButtonTone::Primary : EditorButtonTone::Quiet, kIconSize,
                   TEXT::Localize("editor.ui.save.collision.setup.ctrl.s.cc3f84").c_str())) {
        (void)commandRouter.Execute(EditorCommandId::SaveDocument);
    }
    ImGui::EndDisabled();

    NextToolbarItem();
    ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Undo));
    if (IconButton(EditorGlyph::Undo, "CollisionUndo", EditorButtonTone::Quiet, kIconSize,
                   TEXT::Localize("ui.asm.menu.undoTooltip").c_str())) {
        (void)commandRouter.Execute(EditorCommandId::Undo);
    }
    ImGui::EndDisabled();

    NextToolbarItem();
    ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Redo));
    if (IconButton(EditorGlyph::Redo, "CollisionRedo", EditorButtonTone::Quiet, kIconSize,
                   TEXT::Localize("ui.asm.menu.redoTooltip").c_str())) {
        (void)commandRouter.Execute(EditorCommandId::Redo);
    }
    ImGui::EndDisabled();

    NextToolbarItem();
    ToolbarDivider();
    NextToolbarItem();
    ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::FocusSelection));
    if (IconButton(EditorGlyph::Focus, "CollisionFrameSelection", EditorButtonTone::Quiet, kIconSize,
                   TEXT::Localize("editor.ui.frame.selected.collision.or.source.part.f.d9963e").c_str())) {
        (void)commandRouter.Execute(EditorCommandId::FocusSelection);
    }
    NextToolbarItem();
    if (IconButton(EditorGlyph::Model, "CollisionFrameModel", EditorButtonTone::Quiet, kIconSize,
                   TEXT::Localize("editor.ui.frame.the.complete.model.6ea1a3").c_str())) {
        FitPreviewCamera();
    }
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Object, "CollisionShowModel", showModel_, kIconSize,
                         showModel_ ? TEXT::LocalizeCStr("editor.ui.hide.source.model.f8d90c")
                                    : TEXT::LocalizeCStr("editor.ui.show.source.model.d5c357"))) {
        showModel_ = !showModel_;
    }
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Reveal, "CollisionVisibility",
                         collisionVisibility_ != ModelCollisionPreviewVisibility::Hidden, kIconSize,
                         TEXT::Localize("editor.ui.collision.visibility.options.68406d").c_str())) {
        ImGui::OpenPopup("CollisionVisibilityMenu");
    }
    if (ImGui::BeginPopup("CollisionVisibilityMenu")) {
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.show.all.b3f854", "Show All").c_str(), nullptr,
                            collisionVisibility_ == ModelCollisionPreviewVisibility::All)) {
            collisionVisibility_ = ModelCollisionPreviewVisibility::All;
        }
        if (ImGui::MenuItem(
                TEXT::LocalizeWindow("editor.ui.selected.dim.context.1d83e6", "Selected + Dim Context").c_str(),
                nullptr, collisionVisibility_ == ModelCollisionPreviewVisibility::SelectedWithContext)) {
            collisionVisibility_ = ModelCollisionPreviewVisibility::SelectedWithContext;
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.selected.only.8bab77", "Selected Only").c_str(), nullptr,
                            collisionVisibility_ == ModelCollisionPreviewVisibility::SelectedOnly)) {
            collisionVisibility_ = ModelCollisionPreviewVisibility::SelectedOnly;
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.hide.all.556020", "Hide All").c_str(), nullptr,
                            collisionVisibility_ == ModelCollisionPreviewVisibility::Hidden)) {
            collisionVisibility_ = ModelCollisionPreviewVisibility::Hidden;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.show.individually.hidden.shapes.f12009",
                                                 "Show Individually Hidden Shapes")
                                .c_str(),
                            nullptr, false, !hiddenShapeIds_.empty())) {
            hiddenShapeIds_.clear();
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();

    NextToolbarItem();
    ToolbarDivider();
    NextToolbarItem();
    if (IconButton(EditorGlyph::Exit, "CollisionExit", EditorButtonTone::Quiet, kIconSize,
                   TEXT::Localize("editor.ui.exit.collision.setup.and.return.to.the.scene.6221c2").c_str())) {
        if (history_.IsDirty()) {
            closeRequested_ = true;
        } else {
            result.exitToSceneRequested = true;
        }
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!IsEditingModel());
    if (IconToggleButton(EditorGlyph::Gizmo, "CollisionSelectShapes",
                         selectionMode_ == ModelCollisionSelectionMode::CollisionShapes, kIconSize,
                         TEXT::Localize("editor.ui.edit.collision.shapes.e5a971").c_str())) {
        selectionMode_ = ModelCollisionSelectionMode::CollisionShapes;
    }
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Model, "CollisionSelectSource",
                         selectionMode_ == ModelCollisionSelectionMode::SourceNodes, kIconSize,
                         TEXT::Localize("editor.ui.pick.source.model.parts.d2d8e0").c_str())) {
        selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
    }

    NextToolbarItem();
    ToolbarDivider();
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Translate, "CollisionTranslate",
                         gizmoState_.operation == EditorTransformGizmoOperation::Translate, kIconSize,
                         TEXT::Localize("editor.ui.move.selected.shapes.w.4136a0").c_str())) {
        gizmoState_.operation = EditorTransformGizmoOperation::Translate;
    }
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Rotate, "CollisionRotate",
                         gizmoState_.operation == EditorTransformGizmoOperation::Rotate, kIconSize,
                         TEXT::Localize("editor.ui.rotate.selected.shapes.e.3c6820").c_str())) {
        gizmoState_.operation = EditorTransformGizmoOperation::Rotate;
    }
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Scale, "CollisionScale",
                         gizmoState_.operation == EditorTransformGizmoOperation::Scale, kIconSize,
                         TEXT::Localize("editor.ui.scale.selected.shapes.r.a90ae9").c_str())) {
        gizmoState_.operation = EditorTransformGizmoOperation::Scale;
    }
    NextToolbarItem();
    const bool localSpace = gizmoState_.mode == EditorTransformGizmoMode::Local;
    if (IconButton(localSpace ? EditorGlyph::LocalSpace : EditorGlyph::WorldSpace, "CollisionTransformSpace",
                   EditorButtonTone::Quiet, kIconSize,
                   localSpace ? TEXT::LocalizeCStr("editor.ui.use.world.space.8f2d9a")
                              : TEXT::LocalizeCStr("editor.ui.use.local.space.22d273"))) {
        gizmoState_.mode = localSpace ? EditorTransformGizmoMode::World : EditorTransformGizmoMode::Local;
    }
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Snap, "CollisionSnap", gizmoState_.snapEnabled, kIconSize,
                         gizmoState_.snapEnabled ? TEXT::LocalizeCStr("editor.ui.disable.snapping.576053")
                                                 : TEXT::LocalizeCStr("editor.ui.enable.snapping.8002ed"))) {
        gizmoState_.snapEnabled = !gizmoState_.snapEnabled;
    }

    NextToolbarItem();
    if (IconButton(EditorGlyph::More, "CollisionControls", EditorButtonTone::Quiet, kIconSize,
                   TEXT::Localize("editor.ui.preview.controls.dbc3af").c_str())) {
        ImGui::OpenPopup("CollisionControlsPopup");
    }
    if (ImGui::BeginPopup("CollisionControlsPopup")) {
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.preview.controls.dbc3af").c_str());
        ImGui::Separator();
        ImGui::TextDisabled(TEXT::Localize("editor.ui.rmb.wasdqe.fly.301750").c_str());
        ImGui::TextDisabled(TEXT::Localize("editor.ui.alt.lmb.orbit.51038c").c_str());
        ImGui::TextDisabled(TEXT::Localize("editor.ui.mmb.pan.3cc4b0").c_str());
        ImGui::TextDisabled(TEXT::Localize("editor.ui.mouse.wheel.zoom.19f42b").c_str());
        ImGui::TextDisabled(TEXT::Localize("editor.ui.f.frame.selection.2813df").c_str());
        ImGui::TextDisabled(TEXT::Localize("editor.ui.ctrl.shift.add.to.selection.e90dac").c_str());
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::Separator();
#else
    (void)scene;
    (void)result;
    (void)commandRouter;
#endif
}

} // namespace HIKARI::EDITOR
