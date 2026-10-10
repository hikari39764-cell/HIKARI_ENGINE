#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Core/Text/HIKARI_Localization.h"
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
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.collision.shapes.5aa237", "ModelCollision/Shapes").c_str())) {
        ImGui::End();
        return;
    }
    ImGui::Text(TEXT::Localize("editor.ui.d.shapes.92709f").c_str(), static_cast<int>(setup_.shapes.size()));
    if (!selectedShapeIds_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled(TEXT::Localize("ui.hierarchy.selected").c_str(),
                            static_cast<int>(selectedShapeIds_.size()));
    }
    if (!hiddenShapeIds_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled(TEXT::Localize("editor.ui.d.hidden.d56ffa").c_str(),
                            static_cast<int>(hiddenShapeIds_.size()));
    }
    ImGui::SameLine();
    ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.generated.only.42a4d1", "Generated only").c_str(),
                    &showGeneratedOnly_);
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.shape.bdf0e8", "+ Shape").c_str())) {
        ImGui::OpenPopup("AddCollisionShape");
    }
    if (ImGui::BeginPopup("AddCollisionShape")) {
        if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.sceneCreation.box", "Box").c_str())) {
            AddShape(ASSETS::COLLISION::CollisionGeometryShapeType::Box);
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.sceneCreation.sphere", "Sphere").c_str())) {
            AddShape(ASSETS::COLLISION::CollisionGeometryShapeType::Sphere);
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.sceneCreation.capsule", "Capsule").c_str())) {
            AddShape(ASSETS::COLLISION::CollisionGeometryShapeType::Capsule);
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    const bool hasSelection = !selectedShapeIds_.empty();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.selection.d92c58", "Selection...").c_str())) {
        ImGui::OpenPopup("CollisionSelectionActions");
    }
    if (ImGui::BeginPopup("CollisionSelectionActions")) {
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.select.visible.9f78ea", "Select Visible").c_str())) {
            selectedShapeIds_.clear();
            selectedShapeId_ = 0u;
            for (const ASSETS::COLLISION::ModelCollisionShape& shape : setup_.shapes) {
                if ((showGeneratedOnly_ && !shape.generated) || !ShouldDrawShape(shape.id)) {
                    continue;
                }
                selectedShapeIds_.insert(shape.id);
                if (selectedShapeId_ == 0u) {
                    selectedShapeId_ = shape.id;
                }
            }
            selectionMode_ = ModelCollisionSelectionMode::CollisionShapes;
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.clear.selection.fae58c", "Clear Selection").c_str(),
                            nullptr, false, hasSelection)) {
            selectedShapeIds_.clear();
            selectedShapeId_ = 0u;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.materialGraph.duplicate", "Duplicate").c_str(), "Ctrl+D", false,
                            hasSelection)) {
            DuplicateSelectedShapes();
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.hide.selected.c01d1a", "Hide Selected").c_str(), "H", false,
                            hasSelection)) {
            SetSelectedShapesHidden(true);
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.hide.unselected.667124", "Hide Unselected").c_str(),
                            nullptr, false, hasSelection)) {
            HideUnselectedShapes();
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.show.all.b3f854", "Show All").c_str(), "Shift+H")) {
            ShowAllShapes();
        }
        if (ImGui::MenuItem(AreAllSelectedShapesLocked() ? TEXT::Localize("editor.ui.unlock.1526a1").c_str()
                                                         : TEXT::Localize("editor.ui.lock.891ebc").c_str(),
                            "L", false, hasSelection)) {
            SetSelectedShapesLocked(!AreAllSelectedShapesLocked());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.common.delete", "Delete").c_str(), "Delete", false,
                            hasSelection)) {
            DeleteSelectedShapes();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    // 衝突形状のリストを表示する子ウィンドウを開始します。
    if (ImGui::BeginChild("##CollisionShapeList")) {
        for (ASSETS::COLLISION::ModelCollisionShape& shape : setup_.shapes) {
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
                ImGui::SetTooltip(hidden ? TEXT::Localize("editor.ui.show.collision.shape.21508c").c_str()
                                         : TEXT::Localize("editor.ui.hide.collision.shape.b8773e").c_str());
            }
            ImGui::SameLine();
            bool enabled = shape.enabled;
            if (ImGui::Checkbox("##Enabled", &enabled)) {
                shape.enabled = enabled;
                CommitEdit(enabled ? "Enable Collision Shape" : "Disable Collision Shape");
            }
            ImGui::SameLine();
            const std::string label = shape.name + "  [" + MODEL_COLLISION_PRESENTATION::ShapeTypeLabel(shape.type) +
                                      "]" + (shape.generated ? "  Auto" : "") + (hidden ? "  Hidden" : "") +
                                      (locked ? "  Locked" : "");
            if (hidden) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            if (ImGui::Selectable(label.c_str(), IsShapeSelected(shape.id), ImGuiSelectableFlags_AllowDoubleClick)) {
                const ImGuiIO& io = ImGui::GetIO();
                selectionMode_ = ModelCollisionSelectionMode::CollisionShapes;
                SelectShape(shape.id, io.KeyCtrl || io.KeyShift);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    FocusSelection();
                }
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsShapeSelected(shape.id)) {
                SelectShape(shape.id, false);
            }
            bool shapeListChanged = false;
            if (ImGui::BeginPopupContextItem("ShapeContext")) {
                if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.materialGraph.duplicate", "Duplicate").c_str(),
                                    "Ctrl+D")) {
                    DuplicateSelectedShapes();
                    shapeListChanged = true;
                }
                if (ImGui::MenuItem(hidden ? TEXT::Localize("ui.vfx.text.Show").c_str()
                                           : TEXT::Localize("ui.vfx.text.Hide").c_str(),
                                    "H")) {
                    SetSelectedShapesHidden(!hidden);
                }
                if (ImGui::MenuItem(locked ? TEXT::Localize("editor.ui.unlock.1526a1").c_str()
                                           : TEXT::Localize("editor.ui.lock.891ebc").c_str(),
                                    "L")) {
                    SetSelectedShapesLocked(!locked);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.common.delete", "Delete").c_str(), "Delete")) {
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
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete, focused))
        DeleteSelectedShapes();
    if (ImGui::GetIO().KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_D, focused))
        DuplicateSelectedShapes();
    if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_F, focused))
        FocusSelection();
    ImGui::End();
#endif
}

} // namespace HIKARI::EDITOR
