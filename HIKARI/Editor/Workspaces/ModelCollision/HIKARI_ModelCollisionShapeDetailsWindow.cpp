#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"

#include <algorithm>
#include <cstdio>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPresentation.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelCollisionWorkspaceController::DrawShapeDetailsWindow() {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Shape Details###ModelCollision/Details")) {
    ImGui::End();
    return;
  }
  ASSETS::COLLISION::ModelCollisionShape *shape = SelectedShape();
  if (shape == nullptr) {
    ImGui::TextDisabled("Select or add a collision shape.");
    ImGui::End();
    return;
  }

  if (selectedShapeIds_.size() > 1u) {
    ImGui::TextDisabled("%d shapes selected; editing active shape",
                        static_cast<int>(selectedShapeIds_.size()));
  }
  const bool locked = IsShapeLocked(shape->id);
  if (locked) {
    ImGui::TextColored(ImVec4(0.58f, 0.78f, 1.0f, 1.0f),
                       "Active shape is locked");
  }

  ImGui::BeginDisabled(locked);
  char nameBuffer[160]{};
  std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", shape->name.c_str());
  if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
    shape->name = nameBuffer;
    detailsEditPending_ = true;
  }
  const bool geometryShape =
      shape->type ==
          ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull ||
      shape->type ==
          ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh;
  if (geometryShape) {
    ImGui::Text("Type: %s",
                MODEL_COLLISION_PRESENTATION::ShapeTypeLabel(shape->type));
    ImGui::TextDisabled("%d vertices | %d triangles",
                        static_cast<int>(shape->vertices.size()),
                        static_cast<int>(shape->indices.size() / 3u));
  } else {
    int type = static_cast<int>(shape->type);
    const char *types[]{"Box", "Sphere", "Capsule"};
    if (ImGui::Combo("Type", &type, types, 3)) {
      shape->type =
          static_cast<ASSETS::COLLISION::CollisionGeometryShapeType>(type);
      shape->radius = (std::max)(shape->radius, 0.001f);
      shape->height = (std::max)(shape->height, shape->radius * 2.0f);
      detailsEditPending_ = true;
    }
  }
  if (ImGui::DragFloat3("Center", &shape->center.x, 0.02f, 0.0f, 0.0f,
                        "%.3f")) {
    detailsEditPending_ = true;
  }
  if (ImGui::DragFloat3("Rotation", &shape->rotationEulerDegrees.x, 0.25f, 0.0f,
                        0.0f, "%.2f deg")) {
    detailsEditPending_ = true;
  }
  if (shape->type == ASSETS::COLLISION::CollisionGeometryShapeType::Box) {
    if (ImGui::DragFloat3("Size", &shape->size.x, 0.02f, 0.001f, 1000000.0f,
                          "%.3f")) {
      shape->size.x = (std::max)(shape->size.x, 0.001f);
      shape->size.y = (std::max)(shape->size.y, 0.001f);
      shape->size.z = (std::max)(shape->size.z, 0.001f);
      detailsEditPending_ = true;
    }
  } else if (!geometryShape) {
    if (ImGui::DragFloat("Radius", &shape->radius, 0.01f, 0.001f, 1000000.0f,
                         "%.3f")) {
      shape->radius = (std::max)(shape->radius, 0.001f);
      shape->height = (std::max)(shape->height, shape->radius * 2.0f);
      detailsEditPending_ = true;
    }
    if (shape->type == ASSETS::COLLISION::CollisionGeometryShapeType::Capsule &&
        ImGui::DragFloat("Total Height", &shape->height, 0.02f,
                         shape->radius * 2.0f, 1000000.0f, "%.3f")) {
      shape->height = (std::max)(shape->height, shape->radius * 2.0f);
      detailsEditPending_ = true;
    }
  }
  if (detailsEditPending_) {
    shape->generated = false;
    shape->sourceNodeIndices.clear();
    shape->generationMethod.clear();
  }
  ImGui::EndDisabled();

  ImGui::Separator();
  if (shape->generated) {
    ImGui::TextDisabled("Generated from %d source part(s) | %s",
                        static_cast<int>(shape->sourceNodeIndices.size()),
                        shape->generationMethod.empty()
                            ? "Auto"
                            : shape->generationMethod.c_str());
  } else {
    ImGui::TextDisabled("Manual collision shape");
  }
  if (ImGui::Button("Shape Actions...")) {
    ImGui::OpenPopup("ShapeDetailsActions");
  }
  if (ImGui::BeginPopup("ShapeDetailsActions")) {
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
      DuplicateSelectedShapes();
    }
    if (ImGui::MenuItem(shape->enabled ? "Disable" : "Enable")) {
      SetSelectedShapesEnabled(!shape->enabled);
    }
    const bool allHidden = AreAllSelectedShapesHidden();
    if (ImGui::MenuItem(allHidden ? "Show" : "Hide", "H")) {
      SetSelectedShapesHidden(!allHidden);
    }
    const bool allLocked = AreAllSelectedShapesLocked();
    if (ImGui::MenuItem(allLocked ? "Unlock" : "Lock", "L")) {
      SetSelectedShapesLocked(!allLocked);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Delete")) {
      DeleteSelectedShapes();
    }
    ImGui::EndPopup();
  }

  if (detailsEditPending_ && !ImGui::IsAnyItemActive()) {
    CommitEdit("Edit Collision Shape");
    detailsEditPending_ = false;
  }
  ImGui::End();
#endif
}

} // namespace HIKARI::EDITOR
