#include "Editor/Authoring/HIKARI_SceneComponentAuthoringSection.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
#if defined(HIKARI_WITH_EDITOR)
    namespace {
        void SyncRuntimeTransformToDocument(
            SceneObjectData& target,
            const EditorContext& context) {
            if (!context.selection.selectedObject ||
                context.selection.selectedObject->GetDocumentId() !=
                    target.id) {
                return;
            }
            const Transform3D& transform =
                context.selection.selectedObject->GetTransform();
            target.transform.position = transform.position;
            target.transform.scale = transform.scale;
            target.transform.rotationEulerDeg =
                MATH::EulerXYZDegreesFromQuat(transform.rotation);
        }

        bool ApplyDocumentComponentToRuntime(
            const SceneComponentData& componentData,
            size_t componentIndex,
            const EditorContext& context) {
            if (!context.selection.selectedObject) {
                return false;
            }
            const auto& runtimeComponents =
                context.selection.selectedObject->GetComponents();
            if (componentIndex >= runtimeComponents.size() ||
                !runtimeComponents[componentIndex]) {
                return false;
            }
            IComponent* runtimeComponent =
                runtimeComponents[componentIndex].get();
            if (std::string(runtimeComponent->GetTypeName()) !=
                componentData.type) {
                return false;
            }
            runtimeComponent->Deserialize(componentData.properties);
            return true;
        }

        bool HasDocumentComponent(
            const SceneObjectData& object,
            std::string_view typeName) {
            return std::any_of(
                object.components.begin(),
                object.components.end(),
                [typeName](const SceneComponentData& component) {
                    return component.type == typeName;
                });
        }
    }
#endif

    std::optional<SceneObjectAuthoringHistoryRequest>
        SceneComponentAuthoringSection::Draw(
            DocumentSceneBase& scene,
            EditorContext& context,
            const SelectionSyncService& selectionSync,
            SceneObjectData& target) {
#if defined(HIKARI_WITH_EDITOR)
        std::optional<SceneObjectAuthoringHistoryRequest>
            historyRequest{};
        bool needsRebuild = false;
        bool forceImmediateRebuild = false;
        RuntimeObjectHandle runtimeObject{};
        if (context.selection.selectedObject != nullptr &&
            context.selection.selectedObject->GetDocumentId() ==
                target.id) {
            runtimeObject = context.selection.selectedObject
                ->GetRuntimeHandle();
        }
        const InspectorContext inspectorContext{
            &scene.GetAssetRegistry(),
            &scene.GetAssetDatabase(),
            &scene.GetSceneDocument(),
            &scene.GetWorld().Services(),
            runtimeObject.IsValid() ? &runtimeObject : nullptr
        };

        ImGui::SeparatorText("Document Components");
        for (size_t componentIndex = 0;
            componentIndex < target.components.size();
            ++componentIndex) {
            SceneComponentData& component =
                target.components[componentIndex];
            const ComponentTypeInfo* typeInfo =
                scene.GetComponentRegistry().Find(component.type);
            const std::string componentDisplayName =
                typeInfo != nullptr &&
                !typeInfo->presentation.displayName.empty()
                    ? typeInfo->presentation.displayName
                    : component.type;
            const size_t componentTypeCount =
                static_cast<size_t>(std::count_if(
                    target.components.begin(),
                    target.components.end(),
                    [&component](const SceneComponentData& candidate) {
                        return candidate.type == component.type;
                    }));
            const size_t componentTypeIndex =
                static_cast<size_t>(std::count_if(
                    target.components.begin(),
                    target.components.begin() +
                        static_cast<std::ptrdiff_t>(componentIndex),
                    [&component](const SceneComponentData& candidate) {
                        return candidate.type == component.type;
                    })) + 1u;
            const std::string componentTreeLabel =
                componentTypeCount > 1u
                    ? componentDisplayName + " " +
                        std::to_string(componentTypeIndex)
                    : componentDisplayName;

            ImGui::PushID(static_cast<int>(componentIndex));
            const bool allowDuplicate =
                typeInfo != nullptr && typeInfo->allowMultiple;
            const bool componentOpen = ImGui::TreeNodeEx(
                componentTreeLabel.c_str(),
                ImGuiTreeNodeFlags_SpanAvailWidth);
            bool duplicateRequested = false;
            bool removeRequested = false;
            if (ImGui::BeginPopupContextItem("ComponentContext")) {
                if (allowDuplicate) {
                    duplicateRequested = ImGui::MenuItem("Duplicate");
                }
                if (allowDuplicate) {
                    ImGui::Separator();
                }
                removeRequested = ImGui::MenuItem("Remove Component");
                ImGui::EndPopup();
            }

            bool componentStructureChanged = false;
            if (duplicateRequested) {
                const bool dirtyBefore =
                    context.sceneDirty ||
                    scene.HasUnsavedSceneChanges();
                const std::vector<SceneObjectData> beforeObjects =
                    scene.GetSceneDocument().objects;
                const SceneCameraSettings beforeCamera =
                    scene.GetSceneDocument().camera;
                const ComponentEditResult result =
                    componentAuthoringService_.DuplicateComponent(
                        scene.GetComponentRegistry(),
                        target,
                        componentIndex);
                context.componentAddStatusMessage = result.message;
                context.componentAddStatusIsError = !result.success;
                if (result.documentChanged) {
                    context.sceneDirty = true;
                    needsRebuild = true;
                    forceImmediateRebuild = true;
                    componentStructureChanged = true;
                    historyRequest =
                        SceneObjectAuthoringHistoryRequest{
                            "Duplicate " + componentDisplayName,
                            beforeObjects,
                            scene.GetSceneDocument().objects,
                            beforeCamera,
                            scene.GetSceneDocument().camera,
                            dirtyBefore
                        };
                }
            }
            if (!componentStructureChanged &&
                removeRequested) {
                const bool dirtyBefore =
                    context.sceneDirty ||
                    scene.HasUnsavedSceneChanges();
                const std::vector<SceneObjectData> beforeObjects =
                    scene.GetSceneDocument().objects;
                const SceneCameraSettings beforeCamera =
                    scene.GetSceneDocument().camera;
                const std::string removedType = component.type;
                const ComponentEditResult result =
                    componentAuthoringService_.RemoveComponent(
                        scene.GetComponentRegistry(),
                        target,
                        componentIndex);
                context.componentAddStatusMessage = result.message;
                context.componentAddStatusIsError = !result.success;
                if (result.documentChanged) {
                    if (removedType == "CameraComponent" &&
                        !HasDocumentComponent(
                            target,
                            "CameraComponent")) {
                        if (scene.GetSceneDocument().camera
                                .defaultCameraObjectId == target.id) {
                            scene.ClearGameDefaultCamera();
                        }
                        if (scene.IsEditorCameraPreviewActive() &&
                            scene.GetEditorCameraPreviewObjectId() ==
                                target.id) {
                            scene.EndEditorCameraPreview();
                        }
                    }
                    context.sceneDirty = true;
                    needsRebuild = true;
                    forceImmediateRebuild = true;
                    componentStructureChanged = true;
                    historyRequest =
                        SceneObjectAuthoringHistoryRequest{
                            "Remove " + componentDisplayName,
                            beforeObjects,
                            scene.GetSceneDocument().objects,
                            beforeCamera,
                            scene.GetSceneDocument().camera,
                            dirtyBefore
                        };
                }
            }
            if (componentStructureChanged) {
                deferredRuntimeComponentApply_.reset();
                pendingComponentHistory_.reset();
                if (componentOpen) {
                    ImGui::TreePop();
                }
                ImGui::PopID();
                break;
            }

            if (!componentOpen) {
                ImGui::PopID();
                continue;
            }

            const SceneComponentData beforeComponent = component;
            const bool dirtyBefore =
                context.sceneDirty ||
                scene.HasUnsavedSceneChanges();
            if (componentDocumentEditor_.DrawComponent(
                    scene.GetComponentRegistry(),
                    component,
                    componentInspectorBuilder_,
                    inspectorContext)) {
                if (!pendingComponentHistory_) {
                    std::vector<SceneObjectData> beforeObjects =
                        scene.GetSceneDocument().objects;
                    auto beforeObject = std::find_if(
                        beforeObjects.begin(),
                        beforeObjects.end(),
                        [&target](const SceneObjectData& object) {
                            return object.id == target.id;
                        });
                    if (beforeObject != beforeObjects.end() &&
                        componentIndex <
                            beforeObject->components.size()) {
                        beforeObject->components[componentIndex] =
                            beforeComponent;
                        pendingComponentHistory_ =
                            PendingComponentHistory{
                                "Edit " + componentDisplayName,
                                std::move(beforeObjects),
                                scene.GetSceneDocument().camera,
                                dirtyBefore
                            };
                    }
                }
                context.sceneDirty = true;
                const bool applyOnCommit =
                    typeInfo != nullptr &&
                    typeInfo->runtimeApplyPolicy ==
                        ComponentRuntimeApplyPolicy::OnEditCommit;
                if (applyOnCommit && ImGui::IsAnyItemActive()) {
                    deferredRuntimeComponentApply_ =
                        DeferredRuntimeComponentApply{
                            target.id,
                            componentIndex
                        };
                } else {
                    const bool appliedToRuntime =
                        ApplyDocumentComponentToRuntime(
                            component,
                            componentIndex,
                            context);
                    if (!appliedToRuntime) {
                        needsRebuild = true;
                    } else if (component.type ==
                        "CameraComponent") {
                        (void)scene.ApplyCameraRuntimeChanges();
                    }
                }
            }

            ImGui::TreePop();
            ImGui::PopID();
        }

        const bool editingComponentParameter =
            ImGui::IsAnyItemActive();
        if (deferredRuntimeComponentApply_ &&
            !editingComponentParameter) {
            const DeferredRuntimeComponentApply deferred =
                *deferredRuntimeComponentApply_;
            deferredRuntimeComponentApply_.reset();
            if (deferred.objectId == target.id &&
                deferred.componentIndex < target.components.size()) {
                const SceneComponentData& component =
                    target.components[deferred.componentIndex];
                if (!ApplyDocumentComponentToRuntime(
                        component,
                        deferred.componentIndex,
                        context)) {
                    needsRebuild = true;
                } else if (component.type == "CameraComponent") {
                    (void)scene.ApplyCameraRuntimeChanges();
                }
            } else {
                needsRebuild = true;
            }
        }
        if (needsRebuild) {
            if (editingComponentParameter &&
                !forceImmediateRebuild) {
                deferredComponentRebuild_ = true;
            } else {
                SyncRuntimeTransformToDocument(target, context);
                selectionSync.RebuildRuntimeWorldWithSelectionSync(
                    scene,
                    context.selection,
                    context.nextSceneObjectId);
                deferredComponentRebuild_ = false;
            }
        } else if (deferredComponentRebuild_ &&
            !editingComponentParameter) {
            SyncRuntimeTransformToDocument(target, context);
            selectionSync.RebuildRuntimeWorldWithSelectionSync(
                scene,
                context.selection,
                context.nextSceneObjectId);
            deferredComponentRebuild_ = false;
        }

        if (pendingComponentHistory_ &&
            !editingComponentParameter) {
            PendingComponentHistory pending =
                std::move(*pendingComponentHistory_);
            pendingComponentHistory_.reset();
            historyRequest = SceneObjectAuthoringHistoryRequest{
                std::move(pending.label),
                std::move(pending.beforeObjects),
                scene.GetSceneDocument().objects,
                std::move(pending.beforeCamera),
                scene.GetSceneDocument().camera,
                pending.dirtyBefore
            };
        }
        return historyRequest;
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)target;
        return std::nullopt;
#endif
    }

} // namespace HIKARI
