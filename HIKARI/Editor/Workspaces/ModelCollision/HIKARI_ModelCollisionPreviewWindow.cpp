#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"

#include "Core/HIKARI_TimeService.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPresentation.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionViewportOverlay.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceInteraction.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

void ModelCollisionWorkspaceController::DrawPreviewWindow(
    DocumentSceneBase &scene, EditorWorkspaceHost &workspaceHost,
    ModelCollisionWorkspaceResult &result, EditorCommandRouter &commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
  constexpr ImGuiWindowFlags previewWindowFlags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoCollapse;
  if (!ImGui::Begin("Model Preview###ModelCollision/Preview", nullptr,
                    previewWindowFlags)) {
    ImGui::End();
    return;
  }

  DrawPreviewToolbar(scene, result, commandRouter);

  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = (std::max)(canvasSize.x,
                            MODEL_COLLISION_PRESENTATION::kMinimumPreviewSize);
  canvasSize.y = (std::max)(canvasSize.y,
                            MODEL_COLLISION_PRESENTATION::kMinimumPreviewSize);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(origin,
                          {origin.x + canvasSize.x, origin.y + canvasSize.y},
                          IM_COL32(15, 19, 25, 255));

  EditorViewInstance &view = workspaceHost.GetModelCollisionPreviewView();
  view.extent.width = static_cast<uint32_t>(canvasSize.x);
  view.extent.height = static_cast<uint32_t>(canvasSize.y);
  const float aspect = (std::max)(0.05f, canvasSize.x / canvasSize.y);

  const RENDER3D::EDITORVIEW::EditorInteractiveViewOutput output =
      RENDER3D::EDITORVIEW::GetOutput(view.renderViewId);
  const bool outputReady = showModel_ && output.ready &&
                           output.colorSrv.ptr != 0 &&
                           output.sceneRevision == previewScene_.GetRevision();
  ImGui::SetCursorScreenPos(origin);
  if (outputReady) {
    ImGui::Image(reinterpret_cast<ImTextureID>(
                     static_cast<uintptr_t>(output.colorSrv.ptr)),
                 canvasSize);
  } else {
    ImGui::InvisibleButton("##ModelCollisionPreviewCanvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    if (showModel_ && IsEditingModel()) {
      drawList->AddText({origin.x + 14.0f, origin.y + 14.0f},
                        IM_COL32(180, 194, 207, 255),
                        "Preparing model preview...");
    }
  }

  const bool canvasVisible = ImGui::IsItemVisible();
  const bool hovered = ImGui::IsItemHovered();
  if (hovered) {
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY, ImGuiInputFlags_LockThisFrame);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX, ImGuiInputFlags_LockThisFrame);
  }
  const bool focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const ImGuiIO &io = ImGui::GetIO();
  const EditorViewInputBlockState block = QueryEditorViewInputBlockState();
  EditorViewInputSubmission submission{};
  submission.rect = {origin.x, origin.y, canvasSize.x, canvasSize.y};
  submission.visible = canvasVisible;
  submission.focused = focused;
  submission.hovered = hovered;
  submission.rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  submission.rightMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
  submission.middleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  submission.middleMouseClicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
  submission.leftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  submission.leftMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  submission.altDown = io.KeyAlt;
  submission.pointerBlocked = block.pointer;
  submission.keyboardBlocked = block.keyboard;
  inputRouter_.SetGizmoCapture(view.renderViewId, false);
  const EditorViewInputState &input =
      inputRouter_.Submit(view.renderViewId, submission);

  EditorDirectorCameraInput cameraInput{};
  cameraInput.lookActive = input.rightMouseCaptured;
  cameraInput.orbitActive = input.orbitMouseCaptured;
  cameraInput.panActive = input.middleMouseCaptured;
  cameraInput.mouseDeltaX = io.MouseDelta.x;
  cameraInput.mouseDeltaY = io.MouseDelta.y;
  cameraInput.wheelDelta =
      input.AcceptsWheel() ? io.MouseWheel * (io.KeyShift ? 4.0f : 1.0f) : 0.0f;
  cameraInput.fast = io.KeyShift;
  if (input.rightMouseCaptured) {
    cameraInput.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
    cameraInput.moveBackward = ImGui::IsKeyDown(ImGuiKey_S);
    cameraInput.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
    cameraInput.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
    cameraInput.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
    cameraInput.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);
  }
  bool cameraChanged = cameraController_.Update(
      cameraInput, TIME::GetFrameContext().unscaledDt, aspect);
  if (input.AcceptsKeyboard() && hovered && !io.WantTextInput) {
    (void)commandRouter.ProcessViewportShortcuts(true);
    if (ImGui::IsKeyPressed(ImGuiKey_W, false) && !input.rightMouseCaptured) {
      gizmoState_.operation = EditorTransformGizmoOperation::Translate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E, false) && !input.rightMouseCaptured) {
      gizmoState_.operation = EditorTransformGizmoOperation::Rotate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !input.rightMouseCaptured) {
      gizmoState_.operation = EditorTransformGizmoOperation::Scale;
    }
    if (selectionMode_ == ModelCollisionSelectionMode::CollisionShapes &&
        !io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
      ShowAllShapes();
    } else if (selectionMode_ == ModelCollisionSelectionMode::CollisionShapes &&
               !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
      SetSelectedShapesHidden(true);
    }
    if (selectionMode_ == ModelCollisionSelectionMode::CollisionShapes &&
        !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
      SetSelectedShapesLocked(!AreAllSelectedShapesLocked());
    }
  }
  if (cameraChanged) {
    ++cameraRevision_;
  }
  const Camera3D &camera = cameraController_.GetCamera();
  const bool previewingGenerationDraft = generationDraft_.has_value();
  const ASSETS::COLLISION::ModelCollisionSetup &viewportSetup =
      previewingGenerationDraft ? generationDraft_->setup : setup_;

  ModelCollisionPointerRay pointerRay{};
  const bool pointerRayValid =
      hovered && !block.pointer && canvasSize.x > 0.0f && canvasSize.y > 0.0f &&
      BuildModelCollisionPointerRay(
          camera, (io.MousePos.x - origin.x) / canvasSize.x,
          (io.MousePos.y - origin.y) / canvasSize.y, pointerRay);
  hoveredShapeId_ = 0u;
  hoveredSourceNodeIndex_ = -1;
  if (pointerRayValid && !input.rightMouseCaptured &&
      !input.middleMouseCaptured && !input.orbitMouseCaptured) {
    if (selectionMode_ == ModelCollisionSelectionMode::CollisionShapes &&
        collisionVisibility_ != ModelCollisionPreviewVisibility::Hidden &&
        !previewingGenerationDraft) {
      hoveredShapeId_ = PickModelCollisionShape(
          pointerRay, viewportSetup, hiddenShapeIds_, showGeneratedOnly_);
      if (!ShouldDrawShape(hoveredShapeId_)) {
        hoveredShapeId_ = 0u;
      }
    } else if (selectionMode_ == ModelCollisionSelectionMode::SourceNodes &&
               showModel_ && previewScene_.GetModel() != nullptr) {
      hoveredSourceNodeIndex_ = PickModelCollisionSourceNode(
          pointerRay, previewScene_.GetSourceNodes(),
          *previewScene_.GetModel());
    }
  }

  if (collisionVisibility_ != ModelCollisionPreviewVisibility::Hidden) {
    for (const ASSETS::COLLISION::ModelCollisionShape &shape :
         viewportSetup.shapes) {
      if (showGeneratedOnly_ && !shape.generated) {
        continue;
      }
      if (!ShouldDrawShape(shape.id)) {
        continue;
      }
      DrawModelCollisionShapeOverlay(
          drawList, camera, origin.x, origin.y, canvasSize.x, canvasSize.y,
          shape, !previewingGenerationDraft && IsShapeSelected(shape.id),
          shape.id == hoveredShapeId_, IsShapeLocked(shape.id),
          ShapeOverlayOpacity(shape.id));
    }
  }
  if (previewingGenerationDraft) {
    const char *label =
        "Generation Draft Preview - Apply or Discard in Auto Generate";
    const ImVec2 labelPosition{origin.x + 12.0f, origin.y + 12.0f};
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    drawList->AddRectFilled({labelPosition.x - 7.0f, labelPosition.y - 5.0f},
                            {labelPosition.x + labelSize.x + 7.0f,
                             labelPosition.y + labelSize.y + 5.0f},
                            IM_COL32(29, 24, 42, 220), 4.0f);
    drawList->AddText(labelPosition, IM_COL32(205, 164, 255, 255), label);
  }

  if (showModel_) {
    const ModelAsset *previewModel = previewScene_.GetModel();
    size_t highlightedNodeCount = 0u;
    for (const ModelCollisionPreviewNode &node :
         previewScene_.GetSourceNodes()) {
      const bool selectedNode = selectedSourceNodes_.contains(node.nodeIndex);
      const bool hoveredNode = node.nodeIndex == hoveredSourceNodeIndex_;
      if (!selectedNode && !hoveredNode) {
        continue;
      }
      const uint32_t highlightColor = hoveredNode ? IM_COL32(255, 210, 92, 255)
                                                  : IM_COL32(76, 205, 255, 245);
      const float highlightThickness = hoveredNode ? 2.4f : 1.9f;
      const bool drewGeometryOutline =
          previewModel != nullptr &&
          sourceOutlineCache_.Draw(drawList, camera, origin.x, origin.y,
                                   canvasSize.x, canvasSize.y, *previewModel,
                                   node, previewScene_.GetRevision(),
                                   highlightColor, highlightThickness);
      DrawModelCollisionBoundsOverlay(
          drawList, camera, origin.x, origin.y, canvasSize.x, canvasSize.y,
          node.bounds,
          drewGeometryOutline ? (hoveredNode ? IM_COL32(255, 210, 92, 105)
                                             : IM_COL32(76, 205, 255, 85))
                              : highlightColor,
          drewGeometryOutline ? 1.0f : highlightThickness);
      ++highlightedNodeCount;
      if (highlightedNodeCount >= 1024u) {
        break;
      }
    }
    if (hoveredSourceNodeIndex_ >= 0) {
      const ModelCollisionPreviewNode *hoveredNode =
          previewScene_.FindSourceNode(hoveredSourceNodeIndex_);
      if (hoveredNode != nullptr) {
        drawList->AddText({io.MousePos.x + 14.0f, io.MousePos.y + 12.0f},
                          IM_COL32(255, 225, 145, 255),
                          hoveredNode->name.c_str());
      }
    }
  }

  ASSETS::COLLISION::ModelCollisionShape *selected = SelectedShape();
  EditorTransformGizmoResult gizmo{};
  if (selectionMode_ == ModelCollisionSelectionMode::CollisionShapes &&
      !previewingGenerationDraft && selected != nullptr && selected->enabled &&
      ShouldDrawShape(selected->id) && !IsShapeLocked(selected->id) &&
      !block.pointer) {
    gizmo = transformGizmo_.DrawTransform(
        BuildModelCollisionShapeTransform(*selected), camera, gizmoState_,
        {origin.x, origin.y, canvasSize.x, canvasSize.y});
    if (gizmo.changed) {
      if (!gizmoEditActive_) {
        gizmoEditActive_ = true;
        gizmoEditChanged_ = false;
      }
      selected->center = gizmo.transform.position;
      selected->rotationEulerDegrees = gizmo.transform.rotationEulerDeg;
      const MATH::Vec3 scale = gizmo.transform.scale;
      if (selected->type ==
          ASSETS::COLLISION::CollisionGeometryShapeType::Box) {
        selected->size = scale;
      } else if (selected->type ==
                 ASSETS::COLLISION::CollisionGeometryShapeType::Sphere) {
        selected->radius =
            (std::max)(0.001f, (scale.x + scale.y + scale.z) / 6.0f);
      } else if (selected->type ==
                 ASSETS::COLLISION::CollisionGeometryShapeType::Capsule) {
        selected->radius = (std::max)(0.001f, (scale.x + scale.z) * 0.25f);
        selected->height = (std::max)(scale.y, selected->radius * 2.0f);
      }
      selected->generated = false;
      selected->sourceNodeIndices.clear();
      selected->generationMethod.clear();
      gizmoEditChanged_ = true;
    }
  }
  inputRouter_.SetGizmoCapture(view.renderViewId, gizmo.interacting);
  if (gizmoEditActive_ && !gizmo.interacting) {
    if (gizmoEditChanged_) {
      CommitEdit("Transform Collision Shape");
    }
    gizmoEditActive_ = false;
    gizmoEditChanged_ = false;
  }

  if (pointerRayValid && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      !io.KeyAlt && !gizmo.interacting) {
    const bool additive = io.KeyCtrl || io.KeyShift;
    if (selectionMode_ == ModelCollisionSelectionMode::CollisionShapes) {
      SelectShape(hoveredShapeId_, additive);
    } else {
      SelectSourceNode(hoveredSourceNodeIndex_, additive);
      if (hoveredSourceNodeIndex_ >= 0) {
        sourceSearch_[0] = '\0';
      }
    }
  }

  if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
      !block.pointer) {
    const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
    if (drag.x * drag.x + drag.y * drag.y <= 16.0f) {
      ImGui::OpenPopup("CollisionViewportContext");
    }
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
  }
  if (ImGui::BeginPopup("CollisionViewportContext")) {
    if (ImGui::BeginMenu("Collision Visibility")) {
      if (ImGui::MenuItem("Show All", "Shift+H",
                          collisionVisibility_ ==
                              ModelCollisionPreviewVisibility::All)) {
        ShowAllShapes();
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
      ImGui::EndMenu();
    }
    if (selectionMode_ == ModelCollisionSelectionMode::CollisionShapes) {
      ImGui::Separator();
      const bool hasSelection = !selectedShapeIds_.empty();
      if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false,
                          hasSelection)) {
        DuplicateSelectedShapes();
      }
      if (ImGui::MenuItem("Hide Selected", "H", false, hasSelection)) {
        SetSelectedShapesHidden(true);
      }
      if (ImGui::MenuItem("Hide Unselected", nullptr, false, hasSelection)) {
        HideUnselectedShapes();
      }
      if (ImGui::MenuItem("Isolate Selected", nullptr, false, hasSelection)) {
        collisionVisibility_ = ModelCollisionPreviewVisibility::SelectedOnly;
      }
      if (ImGui::MenuItem(AreAllSelectedShapesLocked() ? "Unlock Selected"
                                                       : "Lock Selected",
                          "L", false, hasSelection)) {
        SetSelectedShapesLocked(!AreAllSelectedShapesLocked());
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Delete Selected", "Delete", false, hasSelection)) {
        DeleteSelectedShapes();
      }
    }
    ImGui::EndPopup();
  }

  if (showModel_ && previewScene_.IsReady()) {
    RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
    request.viewId = view.renderViewId;
    request.cameraFrame.camera = camera;
    request.cameraFrame.revision = cameraRevision_;
    request.cameraFrame.cameraCut = cameraCutPending_;
    request.cameraFrame.valid = true;
    request.width = view.extent.width;
    request.height = view.extent.height;
    request.sceneRevision = previewScene_.GetRevision();
    request.visible = canvasVisible;
    request.drawDebug = false;
    request.shadingMode =
        RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Neutral;
    request.displayExposure = 1.0f;
    request.sceneSourceOverride = previewScene_.GetSceneSource();
    RENDER3D::EDITORVIEW::SubmitRequest(request);
  } else {
    RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
  }
  cameraCutPending_ = false;

  ImGui::SetCursorScreenPos({origin.x, origin.y + canvasSize.y});
  ImGui::Dummy(ImVec2(1.0f, 1.0f));
  if (!statusMessage_.empty()) {
    ImGui::TextWrapped("%s", statusMessage_.c_str());
  }
  ImGui::End();
#else
  (void)scene;
  (void)workspaceHost;
  (void)result;
#endif
}

} // namespace HIKARI::EDITOR
