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
	// 描画する衝突形状のリストウィンドウを描画します。
void ModelCollisionWorkspaceController::DrawShapeListWindow() {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Collision Shapes###ModelCollision/Shapes")) {
    ImGui::End();
    return;
  }
  ImGui::Text("%d shapes", static_cast<int>(setup_.shapes.size()));
  if (!selectedShapeIds_.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("| %d selected",
                        static_cast<int>(selectedShapeIds_.size()));
  }
  if (!hiddenShapeIds_.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("| %d hidden",
                        static_cast<int>(hiddenShapeIds_.size()));
  }
  ImGui::SameLine();
  ImGui::Checkbox("Generated only", &showGeneratedOnly_);
  if (ImGui::Button("+ Shape")) {
    ImGui::OpenPopup("AddCollisionShape");
  }
  if (ImGui::BeginPopup("AddCollisionShape")) {
    if (ImGui::MenuItem("Box")) {
      AddShape(ASSETS::COLLISION::CollisionGeometryShapeType::Box);
    }
    if (ImGui::MenuItem("Sphere")) {
      AddShape(ASSETS::COLLISION::CollisionGeometryShapeType::Sphere);
    }
    if (ImGui::MenuItem("Capsule")) {
      AddShape(ASSETS::COLLISION::CollisionGeometryShapeType::Capsule);
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  const bool hasSelection = !selectedShapeIds_.empty();
  if (ImGui::Button("Selection...")) {
    ImGui::OpenPopup("CollisionSelectionActions");
  }
  if (ImGui::BeginPopup("CollisionSelectionActions")) {
    if (ImGui::MenuItem("Select Visible")) {
      selectedShapeIds_.clear();
      selectedShapeId_ = 0u;
      for (const ASSETS::COLLISION::ModelCollisionShape &shape :
           setup_.shapes) {
        if ((showGeneratedOnly_ && !shape.generated) ||
            !ShouldDrawShape(shape.id)) {
          continue;
        }
        selectedShapeIds_.insert(shape.id);
        if (selectedShapeId_ == 0u) {
          selectedShapeId_ = shape.id;
        }
      }
      selectionMode_ = ModelCollisionSelectionMode::CollisionShapes;
    }
    if (ImGui::MenuItem("Clear Selection", nullptr, false, hasSelection)) {
      selectedShapeIds_.clear();
      selectedShapeId_ = 0u;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) {
      DuplicateSelectedShapes();
    }
    if (ImGui::MenuItem("Hide Selected", "H", false, hasSelection)) {
      SetSelectedShapesHidden(true);
    }
    if (ImGui::MenuItem("Hide Unselected", nullptr, false, hasSelection)) {
      HideUnselectedShapes();
    }
    if (ImGui::MenuItem("Show All", "Shift+H")) {
      ShowAllShapes();
    }
    if (ImGui::MenuItem(AreAllSelectedShapesLocked() ? "Unlock" : "Lock", "L",
                        false, hasSelection)) {
      SetSelectedShapesLocked(!AreAllSelectedShapesLocked());
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Delete", false, hasSelection)) {
      DeleteSelectedShapes();
    }
    ImGui::EndPopup();
  }
  ImGui::Separator();
 
  // 衝突形状のリストを表示する子ウィンドウを開始します。
  if (ImGui::BeginChild("##CollisionShapeList")) {
    for (ASSETS::COLLISION::ModelCollisionShape &shape : setup_.shapes) {
      if (showGeneratedOnly_ && !shape.generated) {
        continue;
      }
      ImGui::PushID(static_cast<int>(shape.id));
      const bool hidden = IsShapeHidden(shape.id);
      const bool locked = IsShapeLocked(shape.id);
      if (ImGui::SmallButton(hidden ? "-" : "o")) {
        if (hidden) {
          hiddenShapeIds_.erase(shape.id);
        } else {
          hiddenShapeIds_.insert(shape.id);
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(hidden ? "Show collision shape"
                                 : "Hide collision shape");
      }
      ImGui::SameLine();
      bool enabled = shape.enabled;
      if (ImGui::Checkbox("##Enabled", &enabled)) {
        shape.enabled = enabled;
        CommitEdit(enabled ? "Enable Collision Shape"
                           : "Disable Collision Shape");
      }
      ImGui::SameLine();
      const std::string label =
          shape.name + "  [" +
          MODEL_COLLISION_PRESENTATION::ShapeTypeLabel(shape.type) + "]" +
          (shape.generated ? "  Auto" : "") + (hidden ? "  Hidden" : "") +
          (locked ? "  Locked" : "");
      if (hidden) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      }
      if (ImGui::Selectable(label.c_str(), IsShapeSelected(shape.id),
                            ImGuiSelectableFlags_AllowDoubleClick)) {
        const ImGuiIO &io = ImGui::GetIO();
        selectionMode_ = ModelCollisionSelectionMode::CollisionShapes;
        SelectShape(shape.id, io.KeyCtrl || io.KeyShift);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          FocusSelection();
        }
      }
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right) &&
          !IsShapeSelected(shape.id)) {
        SelectShape(shape.id, false);
      }
      bool shapeListChanged = false;
      if (ImGui::BeginPopupContextItem("ShapeContext")) {
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
          DuplicateSelectedShapes();
          shapeListChanged = true;
        }
        if (ImGui::MenuItem(hidden ? "Show" : "Hide", "H")) {
          SetSelectedShapesHidden(!hidden);
        }
        if (ImGui::MenuItem(locked ? "Unlock" : "Lock", "L")) {
          SetSelectedShapesLocked(!locked);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Delete")) {
          DeleteSelectedShapes();
          shapeListChanged = true;
        }
        ImGui::EndPopup();
      }
      if (hidden) {
        ImGui::PopStyleColor();
      }
      ImGui::PopID();
      if (shapeListChanged) {
        break;
      }
    }
  }
  ImGui::EndChild();
  ImGui::End();
#endif
}

} // namespace HIKARI::EDITOR
