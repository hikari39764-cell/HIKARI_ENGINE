#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Authoring/HIKARI_EditorObjectPlacement.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include <algorithm>
#include <cmath>
#include <json.hpp>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    void DocumentSceneEditorController::SelectViewportObjects(
        DocumentSceneBase& scene,
        const std::vector<SceneObjectId>& objectIds,
        EditorObjectSelectionMode mode) {
#if defined(HIKARI_WITH_EDITOR)
        context_.selection.SelectObjectIds(
            scene.GetWorld(),
            objectIds,
            mode);
        scene.SetSelectedGizmoObjectId(
            context_.selection.selectedObject != nullptr
                ? context_.selection.selectedObject->GetDocumentId()
                : SceneObjectId{});
#else
        (void)scene;
        (void)objectIds;
        (void)mode;
#endif
    }

    void DocumentSceneEditorController::FocusSceneObjects(
        DocumentSceneBase& scene,
        const std::vector<SceneObjectId>& objectIds) {

        Bounds selectionBounds = BOUNDS::EmptyBounds();
        bool hasSelectionBounds = false;
        const RENDER3D::RUNTIME::SceneRenderCache& renderCache =
            RenderSubmissionSystem::GetSceneRenderCache();

        for (SceneObjectId objectId : objectIds) {
            const RENDER3D::RUNTIME::SceneRenderObject* renderObject =
                renderCache.Find(
                    RENDER3D::RUNTIME::SceneRenderObjectId{
                        objectId.value
                    });
            if (renderObject != nullptr &&
                BOUNDS::IsUsable(renderObject->desc.worldBounds)) {
                BOUNDS::Encapsulate(
                    selectionBounds,
                    renderObject->desc.worldBounds);
                hasSelectionBounds = true;
                continue;
            }

            const GameObject* object =
                scene.GetWorld().FindObject(objectId);
            if (object == nullptr) {
                continue;
            }
            const MATH::Mat4& worldMatrix =
                object->GetTransform().GetWorldMatrix();
            MATH::Vec3 position{
                worldMatrix.m[3][0],
                worldMatrix.m[3][1],
                worldMatrix.m[3][2]
            };
            MATH::Quat rotation{};
            MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
            (void)MATH::DecomposeTRS(
                worldMatrix,
                position,
                rotation,
                scale);
            const float radius = (std::max)({
                std::abs(scale.x),
                std::abs(scale.y),
                std::abs(scale.z),
                0.5f
            });
            const MATH::Vec3 extent{ radius, radius, radius };
            BOUNDS::Encapsulate(selectionBounds, position - extent);
            BOUNDS::Encapsulate(selectionBounds, position + extent);
            hasSelectionBounds = true;
        }

        if (!hasSelectionBounds ||
            !BOUNDS::IsUsable(selectionBounds)) {
            return;
        }

        const MATH::Vec3 center =
            (selectionBounds.min + selectionBounds.max) * 0.5f;
        const MATH::Vec3 extent =
            (selectionBounds.max - selectionBounds.min) * 0.5f;
        const float radius = (std::max)(
            std::sqrt(
                extent.x * extent.x +
                extent.y * extent.y +
                extent.z * extent.z),
            1.0f);

        DebugCameraController3D& debugCamera =
            scene.GetDebugCamera();
        const float yaw = debugCamera.GetYaw();
        const float pitch = debugCamera.GetPitch();
        const float cp = std::cos(pitch);
        const MATH::Vec3 forward = MATH::Normalize({
            std::sin(yaw) * cp,
            std::sin(pitch),
            std::cos(yaw) * cp
        });
        const float distance = (std::clamp)(
            radius * 2.5f,
            3.0f,
            5000.0f);
        const MATH::Vec3 position =
            center - forward * distance;
        debugCamera.SetPosition(position);
        scene.GetCamera().SetLookAt(position, center);
    }

    void DocumentSceneEditorController::DrawViewportContextMenu(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::BeginPopup("SceneViewportContextMenu")) {
            return;
        }

        const SceneObjectId contextTarget =
            viewportSelectionService_.GetContextTarget();
        GameObject* target = contextTarget.value != 0u
            ? scene.GetWorld().FindObject(contextTarget)
            : nullptr;
        if (target != nullptr) {
            sceneInspectorPanel_.DrawObjectContextMenu(
                scene,
                context_,
                selectionSync_,
                sceneObjectCommands_,
                *target);
        } else {
            ImGui::TextDisabled("Create in Scene");
            sceneCreationPanel_.DrawCreationMenu(
                scene,
                context_,
                selectionSync_,
                sceneObjectCommands_);
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
            request.name = payload.record->displayName.empty()
                ? payload.record->sourcePath.stem().string()
                : payload.record->displayName;
            request.position = EDITOR::ComputeObjectPlacementInView(
                scene.GetCamera(),
                1.0f);

            GameObject* object = EDITOR::CreateModelObject(scene, payload.guid, request);
            context_.selection.SelectObject(scene.GetWorld(), object);
            context_.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
            selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
            viewportDropMessage_ = object
                ? "Model object created: " + object->GetName()
                : "Model drop failed";
            break;
        }
        case AssetType::Scene:
            pendingSceneOpenGuid_ = payload.guid;
            if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
                ImGui::OpenPopup("Unsaved Scene Changes");
            } else {
                OpenSceneAssetFromEditor(scene, payload.guid);
            }
            break;
        case AssetType::VfxEffect:
            viewportDropMessage_ = "VFX drop target not implemented yet";
            break;
        case AssetType::Texture:
            viewportDropMessage_ = "Texture viewport drop is not implemented yet";
            break;
        case AssetType::Material:
            if (!context_.selection.selectedObject) {
                viewportDropMessage_ = "Select a model object first, then drop Material";
                break;
            }
            if (auto* modelComponent = context_.selection.selectedObject->GetComponent<ModelComponent>()) {
                modelComponent->SetMaterialOverride(0, payload.guid);
                scene.RebuildMaterialOverrides();
                if (SceneObjectData* documentObject =
                    selectionSync_.FindDocumentObjectByRuntime(scene, context_.selection.selectedObject)) {
                    for (SceneComponentData& component : documentObject->components) {
                        if (component.type != "ModelComponent") {
                            continue;
                        }
                        component.properties["materialOverrides"] = nlohmann::json::array({
                            {
                                { "slot", 0u },
                                { "materialAssetGuid", payload.guid.value }
                            }
                        });
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
        default:
            viewportDropMessage_ = "This asset type cannot be dropped into Game View yet";
            break;
        }
#else
        (void)scene;
#endif
    }

} // namespace HIKARI
