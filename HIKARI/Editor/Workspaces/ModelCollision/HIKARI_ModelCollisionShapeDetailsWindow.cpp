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

void ModelCollisionWorkspaceController::DrawShapeDetailsWindow() {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.shape.details.095a79", "ModelCollision/Details").c_str())) {
        ImGui::End();
        return;
    }
    ASSETS::COLLISION::ModelCollisionShape* shape = SelectedShape();
    if (shape == nullptr) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.select.or.add.a.collision.shape.f49c19").c_str());
        ImGui::End();
        return;
    }

    if (selectedShapeIds_.size() > 1u) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.d.shapes.selected.editing.active.shape.25820b").c_str(),
                            static_cast<int>(selectedShapeIds_.size()));
    }
    const bool locked = IsShapeLocked(shape->id);
    if (locked) {
        ImGui::TextColored(ImVec4(0.58f, 0.78f, 1.0f, 1.0f),
                           TEXT::Localize("editor.ui.active.shape.is.locked.26c7ed").c_str());
    }

    ImGui::BeginDisabled(locked);
    char nameBuffer[160]{};
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", shape->name.c_str());
    if (ImGui::InputText(TEXT::LocalizeWindow("ui.input.name", "Name").c_str(), nameBuffer, sizeof(nameBuffer))) {
        shape->name = nameBuffer;
        detailsEditPending_ = true;
    }
    const bool geometryShape = shape->type == ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull ||
                               shape->type == ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh;
    if (geometryShape) {
        ImGui::Text(TEXT::Localize("ui.assetInspector.typeFormat").c_str(),
                    MODEL_COLLISION_PRESENTATION::ShapeTypeLabel(shape->type));
        ImGui::TextDisabled(TEXT::Localize("editor.ui.d.vertices.d.triangles.15f470").c_str(),
                            static_cast<int>(shape->vertices.size()), static_cast<int>(shape->indices.size() / 3u));
    } else {
        int type = static_cast<int>(shape->type);
        const char* types[]{TEXT::LocalizeCStr("ui.sceneCreation.box"), TEXT::LocalizeCStr("ui.sceneCreation.sphere"),
                            TEXT::LocalizeCStr("ui.sceneCreation.capsule")};
        if (ImGui::Combo(TEXT::LocalizeWindow("ui.vfx.text.Type", "Type").c_str(), &type, types, 3)) {
            shape->type = static_cast<ASSETS::COLLISION::CollisionGeometryShapeType>(type);
            shape->radius = (std::max)(shape->radius, 0.001f);
            shape->height = (std::max)(shape->height, shape->radius * 2.0f);
            detailsEditPending_ = true;
        }
    }
    if (ImGui::DragFloat3(TEXT::LocalizeWindow("ui.vfx.effect.param.center", "Center").c_str(), &shape->center.x, 0.02f,
                          0.0f, 0.0f, "%.3f")) {
        detailsEditPending_ = true;
    }
    if (ImGui::DragFloat3(TEXT::LocalizeWindow("ui.inspector.rotation", "Rotation").c_str(),
                          &shape->rotationEulerDegrees.x, 0.25f, 0.0f, 0.0f, "%.2f deg")) {
        detailsEditPending_ = true;
    }
    if (shape->type == ASSETS::COLLISION::CollisionGeometryShapeType::Box) {
        if (ImGui::DragFloat3(TEXT::LocalizeWindow("ui.inspector.worldSprite.size", "Size").c_str(), &shape->size.x,
                              0.02f, 0.001f, 1000000.0f, "%.3f")) {
            shape->size.x = (std::max)(shape->size.x, 0.001f);
            shape->size.y = (std::max)(shape->size.y, 0.001f);
            shape->size.z = (std::max)(shape->size.z, 0.001f);
            detailsEditPending_ = true;
        }
    } else if (!geometryShape) {
        if (ImGui::DragFloat(TEXT::LocalizeWindow("ui.quality.radius", "Radius").c_str(), &shape->radius, 0.01f, 0.001f,
                             1000000.0f, "%.3f")) {
            shape->radius = (std::max)(shape->radius, 0.001f);
            shape->height = (std::max)(shape->height, shape->radius * 2.0f);
            detailsEditPending_ = true;
        }
        if (shape->type == ASSETS::COLLISION::CollisionGeometryShapeType::Capsule &&
            ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.total.height.4552db", "Total Height").c_str(),
                             &shape->height, 0.02f, shape->radius * 2.0f, 1000000.0f, "%.3f")) {
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
        ImGui::TextDisabled(TEXT::Localize("editor.ui.generated.from.d.source.part.s.s.a0ef14").c_str(),
                            static_cast<int>(shape->sourceNodeIndices.size()),
                            shape->generationMethod.empty() ? "Auto" : shape->generationMethod.c_str());
    } else {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.manual.collision.shape.8d029e").c_str());
    }
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.shape.actions.3d79ec", "Shape Actions...").c_str())) {
        ImGui::OpenPopup("ShapeDetailsActions");
    }
    if (ImGui::BeginPopup("ShapeDetailsActions")) {
        if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.materialGraph.duplicate", "Duplicate").c_str(), "Ctrl+D")) {
            DuplicateSelectedShapes();
        }
        if (ImGui::MenuItem(shape->enabled ? TEXT::LocalizeCStr("editor.ui.disable.9a7d4e")
                                           : TEXT::LocalizeCStr("editor.ui.enable.20063a"))) {
            SetSelectedShapesEnabled(!shape->enabled);
        }
        const bool allHidden = AreAllSelectedShapesHidden();
        if (ImGui::MenuItem(allHidden ? TEXT::LocalizeCStr("ai.api.show") : TEXT::LocalizeCStr("ui.vfx.text.Hide"),
                            "H")) {
            SetSelectedShapesHidden(!allHidden);
        }
        const bool allLocked = AreAllSelectedShapesLocked();
        if (ImGui::MenuItem(allLocked ? TEXT::LocalizeCStr("editor.ui.unlock.1526a1")
                                      : TEXT::LocalizeCStr("editor.ui.lock.891ebc"),
                            "L")) {
            SetSelectedShapesLocked(!allLocked);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.common.delete", "Delete").c_str(), "Delete")) {
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
