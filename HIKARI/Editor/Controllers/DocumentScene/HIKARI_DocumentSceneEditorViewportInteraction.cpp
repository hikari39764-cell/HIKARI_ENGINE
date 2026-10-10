#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Authoring/HIKARI_EditorObjectPlacement.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include <algorithm>
#include <cmath>
#include <json.hpp>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void DocumentSceneEditorController::SelectViewportObjects(DocumentSceneBase& scene,
                                                          const std::vector<SceneObjectId>& objectIds,
                                                          EditorObjectSelectionMode mode) {
#if defined(HIKARI_WITH_EDITOR)
    context_.selection.SelectObjectIds(scene.GetWorld(), objectIds, mode);
    scene.SetSelectedGizmoObjectId(context_.selection.selectedObject != nullptr
                                       ? context_.selection.selectedObject->GetDocumentId()
                                       : SceneObjectId{});
#else
    (void)scene;
    (void)objectIds;
    (void)mode;
#endif
}

void DocumentSceneEditorController::FocusSceneObjects(DocumentSceneBase& scene,
                                                      const std::vector<SceneObjectId>& objectIds) {
    Bounds selectionBounds = BOUNDS::EmptyBounds();
    bool hasSelectionBounds = false;
    for (SceneObjectId objectId : objectIds) {
        const auto bounds = EDITOR::ObserveSceneObjectBounds(scene, objectId);
        if (BOUNDS::IsUsable(bounds)) {
            BOUNDS::Encapsulate(selectionBounds, bounds);
            hasSelectionBounds = true;
            continue;
        }

        const GameObject* object = scene.GetWorld().FindObject(objectId);
        if (object == nullptr) {
            continue;
        }
        const MATH::Mat4& worldMatrix = object->GetTransform().GetWorldMatrix();
        MATH::Vec3 position{worldMatrix.m[3][0], worldMatrix.m[3][1], worldMatrix.m[3][2]};
        MATH::Quat rotation{};
        MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
        (void)MATH::DecomposeTRS(worldMatrix, position, rotation, scale);
        const float radius = (std::max)({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 0.5f});
        const MATH::Vec3 extent{radius, radius, radius};
        BOUNDS::Encapsulate(selectionBounds, position - extent);
        BOUNDS::Encapsulate(selectionBounds, position + extent);
        hasSelectionBounds = true;
    }

    if (!hasSelectionBounds || !BOUNDS::IsUsable(selectionBounds)) {
        return;
    }

    const MATH::Vec3 center = (selectionBounds.min + selectionBounds.max) * 0.5f;
    const MATH::Vec3 extent = (selectionBounds.max - selectionBounds.min) * 0.5f;
    const float radius = (std::max)(std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z), 1.0f);

    DebugCameraController3D& debugCamera = scene.GetDebugCamera();
    const float yaw = debugCamera.GetYaw();
    const float pitch = debugCamera.GetPitch();
    const float cp = std::cos(pitch);
    const MATH::Vec3 forward = MATH::Normalize({std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp});
    const auto& viewCamera = scene.GetCamera();
    const float halfFov = std::atan(std::tan(viewCamera.GetFovYRad() * 0.5f) *
                                    (std::min)(1.0f, (std::max)(0.01f, viewCamera.GetAspect())));
    const float distance =
        (std::max)(radius + viewCamera.GetNearZ(), radius * 1.1f / (std::max)(0.01f, std::sin(halfFov)));
    const MATH::Vec3 position = center - forward * distance;
    debugCamera.SetPosition(position);
    scene.GetCamera().SetLookAt(position, center);
}

void DocumentSceneEditorController::DrawViewportContextMenu(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::BeginPopup("SceneViewportContextMenu")) {
        return;
    }

    const SceneObjectId contextTarget = viewportSelectionService_.GetContextTarget();
    GameObject* target = contextTarget.value != 0u ? scene.GetWorld().FindObject(contextTarget) : nullptr;
    if (target != nullptr) {
        sceneInspectorPanel_.DrawObjectContextMenu(scene, context_, selectionSync_, sceneObjectCommands_, *target);
    } else {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.create.in.scene.26f233").c_str());
        sceneCreationPanel_.DrawCreationMenu(scene, context_, selectionSync_, sceneObjectCommands_);
    }
    ImGui::EndPopup();
#else
    (void)scene;
#endif
}

void DocumentSceneEditorController::HandleGameViewportAssetDrop(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
    EDITOR::DroppedAssetPayload payload{};
    if (!EDITOR::AcceptAssetDrop(scene.GetAssetDatabase(), payload) || !payload.record) {
        return;
    }

    switch (payload.record->type) {
    case AssetType::Model: {
        EDITOR::CreateObjectRequest request{};
        request.name = payload.record->displayName.empty() ? payload.record->sourcePath.stem().string()
                                                           : payload.record->displayName;
        request.position = EDITOR::ComputeObjectPlacementInView(scene.GetCamera(), 1.0f);

        GameObject* object = EDITOR::CreateModelObject(scene, payload.guid, request);
        context_.selection.SelectObject(scene.GetWorld(), object);
        context_.sceneDirty = true;
        scene.SetUnsavedSceneChanges(true);
        selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
        viewportDropMessage_ = object ? "Model object created: " + object->GetName() : "Model drop failed";
        break;
    }
    case AssetType::Scene: RequestSceneOpen(scene, payload.guid); break;
    case AssetType::VfxEffect: viewportDropMessage_ = "VFX drop target not implemented yet"; break;
    case AssetType::Texture:
    case AssetType::Sprite: {
        const bool dirtyBefore = context_.sceneDirty || scene.HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
        EDITOR::CreateObjectRequest request{};
        request.name = payload.record->displayName.empty() ? payload.record->sourcePath.stem().string()
                                                           : payload.record->displayName;
        request.position = EDITOR::ComputeObjectPlacementInView(scene.GetCamera(), 1.0f);
        GameObject* object = EDITOR::CreateWorldSpriteObject(scene, payload.guid, request);
        if (object == nullptr) {
            viewportDropMessage_ = "World Sprite creation failed";
            break;
        }
        context_.selection.SelectObject(scene.GetWorld(), object);
        selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
        CommitSceneObjectAuthoringHistory(
            scene,
            SceneObjectAuthoringHistoryRequest{"Create World Sprite", beforeObjects, scene.GetSceneDocument().objects,
                                               beforeCamera, scene.GetSceneDocument().camera, dirtyBefore});
        viewportDropMessage_ = "World Sprite created: " + object->GetName();
        break;
    }
    case AssetType::Material:
        if (!context_.selection.selectedObject) {
            viewportDropMessage_ = "Select a model object first, then drop Material";
            break;
        }
        if (auto* modelComponent = context_.selection.selectedObject->GetComponent<ModelComponent>()) {
            const ModelAsset* modelAsset = modelComponent->GetModelAsset();
            if (modelAsset == nullptr || modelAsset->materials.empty()) {
                viewportDropMessage_ = "The selected model has no resolved material slots";
                break;
            }
            if (modelAsset->materials.size() != 1u) {
                viewportDropMessage_ = "This model has multiple material slots; assign the "
                                       "material to a selected slot in Model Material workspace";
                break;
            }

            const MaterialAsset& importedMaterial = modelAsset->materials.front();
            modelComponent->SetMaterialOverride(importedMaterial.slotId, payload.guid);
            scene.RebuildMaterialOverrides();
            if (SceneObjectData* documentObject =
                    selectionSync_.FindDocumentObjectByRuntime(scene, context_.selection.selectedObject)) {
                for (SceneComponentData& component : documentObject->components) {
                    if (component.type != "ModelComponent") {
                        continue;
                    }

                    nlohmann::json serializedComponent{};
                    modelComponent->Serialize(serializedComponent);
                    component.properties["materialOverrides"] = serializedComponent["materialOverrides"];
                    break;
                }
            }
            context_.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
            viewportDropMessage_ = "Material assigned: " + payload.record->displayName;
        } else {
            viewportDropMessage_ = "Selected object has no ModelComponent";
        }
        break;
    default: viewportDropMessage_ = "This asset type cannot be dropped into Game View yet"; break;
    }
#else
    (void)scene;
#endif
}

} // namespace HIKARI
