#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"

#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelCollisionWorkspaceController::DrawPreviewToolbar(
    DocumentSceneBase &scene, ModelCollisionWorkspaceResult &result,
    EditorCommandRouter &commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
  constexpr ImVec2 kIconSize{28.0f, 28.0f};
  const std::string title =
      IsEditingModel() ? modelDisplayName_ : std::string("No model open");

  ToolbarLabel(title.c_str());
  if (IsEditingModel()) {
    ImGui::SameLine();
    StatusBadge(history_.IsDirty() ? "Unsaved" : "Saved",
                history_.IsDirty() ? EditorStatusTone::Warning
                                   : EditorStatusTone::Ready);
  }

  ImGui::SameLine(0.0f, 12.0f);
  ImGui::BeginDisabled(
      !history_.IsDirty() ||
      !commandRouter.CanExecute(EditorCommandId::SaveDocument));
  if (IconButton(EditorGlyph::Save, "CollisionSave",
                 history_.IsDirty() ? EditorButtonTone::Primary
                                    : EditorButtonTone::Quiet,
                 kIconSize, "Save collision setup (Ctrl+S)")) {
    (void)commandRouter.Execute(EditorCommandId::SaveDocument);
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Undo));
  if (IconButton(EditorGlyph::Undo, "CollisionUndo", EditorButtonTone::Quiet,
                 kIconSize, "Undo (Ctrl+Z)")) {
    (void)commandRouter.Execute(EditorCommandId::Undo);
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Redo));
  if (IconButton(EditorGlyph::Redo, "CollisionRedo", EditorButtonTone::Quiet,
                 kIconSize, "Redo (Ctrl+Y)")) {
    (void)commandRouter.Execute(EditorCommandId::Redo);
  }
  ImGui::EndDisabled();

  ImGui::SameLine(0.0f, 10.0f);
  ToolbarDivider();
  ImGui::SameLine(0.0f, 10.0f);
  ImGui::BeginDisabled(
      !commandRouter.CanExecute(EditorCommandId::FocusSelection));
  if (IconButton(EditorGlyph::Focus, "CollisionFrameSelection",
                 EditorButtonTone::Quiet, kIconSize,
                 "Frame selected collision or source part (F)")) {
    (void)commandRouter.Execute(EditorCommandId::FocusSelection);
  }
  ImGui::SameLine();
  if (IconButton(EditorGlyph::Model, "CollisionFrameModel",
                 EditorButtonTone::Quiet, kIconSize,
                 "Frame the complete model")) {
    FitPreviewCamera();
  }
  ImGui::SameLine();
  if (IconToggleButton(
          EditorGlyph::Object, "CollisionShowModel", showModel_, kIconSize,
          showModel_ ? "Hide source model" : "Show source model")) {
    showModel_ = !showModel_;
  }
  ImGui::SameLine();
  if (IconToggleButton(EditorGlyph::Reveal, "CollisionVisibility",
                       collisionVisibility_ !=
                           ModelCollisionPreviewVisibility::Hidden,
                       kIconSize, "Collision visibility options")) {
    ImGui::OpenPopup("CollisionVisibilityMenu");
  }
  if (ImGui::BeginPopup("CollisionVisibilityMenu")) {
    if (ImGui::MenuItem("Show All", nullptr,
                        collisionVisibility_ ==
                            ModelCollisionPreviewVisibility::All)) {
      collisionVisibility_ = ModelCollisionPreviewVisibility::All;
    }
    if (ImGui::MenuItem(
            "Selected + Dim Context", nullptr,
            collisionVisibility_ ==
                ModelCollisionPreviewVisibility::SelectedWithContext)) {
      collisionVisibility_ =
          ModelCollisionPreviewVisibility::SelectedWithContext;
    }
    if (ImGui::MenuItem("Selected Only", nullptr,
                        collisionVisibility_ ==
                            ModelCollisionPreviewVisibility::SelectedOnly)) {
      collisionVisibility_ = ModelCollisionPreviewVisibility::SelectedOnly;
    }
    if (ImGui::MenuItem("Hide All", nullptr,
                        collisionVisibility_ ==
                            ModelCollisionPreviewVisibility::Hidden)) {
      collisionVisibility_ = ModelCollisionPreviewVisibility::Hidden;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Show Individually Hidden Shapes", nullptr, false,
                        !hiddenShapeIds_.empty())) {
      hiddenShapeIds_.clear();
    }
    ImGui::EndPopup();
  }
  ImGui::EndDisabled();

  ImGui::SameLine(0.0f, 10.0f);
  ToolbarDivider();
  ImGui::SameLine(0.0f, 10.0f);
  if (IconButton(EditorGlyph::Exit, "CollisionExit", EditorButtonTone::Quiet,
                 kIconSize, "Exit collision setup and return to the scene")) {
    if (history_.IsDirty()) {
      closeRequested_ = true;
    } else {
      result.exitToSceneRequested = true;
    }
  }

  ImGui::Separator();
  ImGui::BeginDisabled(!IsEditingModel());
  if (IconToggleButton(EditorGlyph::Gizmo, "CollisionSelectShapes",
                       selectionMode_ ==
                           ModelCollisionSelectionMode::CollisionShapes,
                       kIconSize, "Edit collision shapes")) {
    selectionMode_ = ModelCollisionSelectionMode::CollisionShapes;
  }
  ImGui::SameLine();
  if (IconToggleButton(EditorGlyph::Model, "CollisionSelectSource",
                       selectionMode_ ==
                           ModelCollisionSelectionMode::SourceNodes,
                       kIconSize, "Pick source model parts")) {
    selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
  }

  ImGui::SameLine(0.0f, 10.0f);
  ToolbarDivider();
  ImGui::SameLine(0.0f, 10.0f);
  if (IconToggleButton(EditorGlyph::Translate, "CollisionTranslate",
                       gizmoState_.operation ==
                           EditorTransformGizmoOperation::Translate,
                       kIconSize, "Move selected shapes (W)")) {
    gizmoState_.operation = EditorTransformGizmoOperation::Translate;
  }
  ImGui::SameLine();
  if (IconToggleButton(EditorGlyph::Rotate, "CollisionRotate",
                       gizmoState_.operation ==
                           EditorTransformGizmoOperation::Rotate,
                       kIconSize, "Rotate selected shapes (E)")) {
    gizmoState_.operation = EditorTransformGizmoOperation::Rotate;
  }
  ImGui::SameLine();
  if (IconToggleButton(EditorGlyph::Scale, "CollisionScale",
                       gizmoState_.operation ==
                           EditorTransformGizmoOperation::Scale,
                       kIconSize, "Scale selected shapes (R)")) {
    gizmoState_.operation = EditorTransformGizmoOperation::Scale;
  }
  ImGui::SameLine();
  const bool localSpace = gizmoState_.mode == EditorTransformGizmoMode::Local;
  if (IconButton(localSpace ? EditorGlyph::LocalSpace : EditorGlyph::WorldSpace,
                 "CollisionTransformSpace", EditorButtonTone::Quiet, kIconSize,
                 localSpace ? "Use world space" : "Use local space")) {
    gizmoState_.mode = localSpace ? EditorTransformGizmoMode::World
                                  : EditorTransformGizmoMode::Local;
  }
  ImGui::SameLine();
  if (IconToggleButton(EditorGlyph::Snap, "CollisionSnap",
                       gizmoState_.snapEnabled, kIconSize,
                       gizmoState_.snapEnabled ? "Disable snapping"
                                               : "Enable snapping")) {
    gizmoState_.snapEnabled = !gizmoState_.snapEnabled;
  }

  ImGui::SameLine(0.0f, 10.0f);
  if (IconButton(EditorGlyph::More, "CollisionControls",
                 EditorButtonTone::Quiet, kIconSize, "Preview controls")) {
    ImGui::OpenPopup("CollisionControlsPopup");
  }
  if (ImGui::BeginPopup("CollisionControlsPopup")) {
    ImGui::TextUnformatted("Preview controls");
    ImGui::Separator();
    ImGui::TextDisabled("RMB + WASDQE   Fly");
    ImGui::TextDisabled("Alt + LMB       Orbit");
    ImGui::TextDisabled("MMB             Pan");
    ImGui::TextDisabled("Mouse Wheel     Zoom");
    ImGui::TextDisabled("F               Frame selection");
    ImGui::TextDisabled("Ctrl/Shift      Add to selection");
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
