#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_SceneComponentAuthoringSection.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
#if defined(HIKARI_WITH_EDITOR)
namespace {
void SyncRuntimeTransformToDocument(SceneObjectData& target, const EditorContext& context) {
    if (!context.selection.selectedObject || context.selection.selectedObject->GetDocumentId() != target.id) {
        return;
    }
    const Transform3D& transform = context.selection.selectedObject->GetTransform();
    target.transform.position = transform.position;
    target.transform.scale = transform.scale;
    target.transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(transform.rotation);
}

bool ApplyDocumentComponentToRuntime(DocumentSceneBase& scene, const SceneObjectData& documentObject,
                                     const SceneComponentData& componentData, size_t componentIndex,
                                     const EditorContext& context) {
    if (!context.selection.selectedObject) {
        return false;
    }
    if (componentIndex >= documentObject.components.size() ||
        documentObject.components[componentIndex].type != componentData.type) {
        return false;
    }

    // Runtime construction may skip an unavailable document
    // component, so the raw document index is not a stable runtime
    // address. Until runtime components carry their document ID,
    // resolve by type plus the same-type ordinal.
    size_t typeOrdinal = 0u;
    for (size_t index = 0u; index < componentIndex; ++index) {
        if (documentObject.components[index].type == componentData.type) {
            ++typeOrdinal;
        }
    }
    const auto& runtimeComponents = context.selection.selectedObject->GetComponents();
    for (const auto& runtimeEntry : runtimeComponents) {
        if (!runtimeEntry || std::string(runtimeEntry->GetTypeName()) != componentData.type) {
            continue;
        }
        if (typeOrdinal > 0u) {
            --typeOrdinal;
            continue;
        }
        runtimeEntry->Deserialize(componentData.properties);
        std::string diagnostic{};
        return SceneRuntimeBuilder{}.InitializeRuntimeComponentAssets(*context.selection.selectedObject, *runtimeEntry,
                                                                      scene.GetAssetRegistry(), scene.GetModelManager(),
                                                                      diagnostic);
    }
    return false;
}

bool HasDocumentComponent(const SceneObjectData& object, std::string_view typeName) {
    return std::any_of(object.components.begin(), object.components.end(),
                       [typeName](const SceneComponentData& component) { return component.type == typeName; });
}

} // namespace
#endif

std::optional<SceneObjectAuthoringHistoryRequest> SceneComponentAuthoringSection::Draw(
    DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync,
    SceneObjectData& target, std::span<const std::string_view> filter) {
#if defined(HIKARI_WITH_EDITOR)
    std::optional<SceneObjectAuthoringHistoryRequest> historyRequest{};
    NormalizeSceneComponentIds(target);
    bool needsRebuild = false;
    bool forceImmediateRebuild = false;
    RuntimeObjectHandle runtimeObject{};
    if (context.selection.selectedObject != nullptr && context.selection.selectedObject->GetDocumentId() == target.id) {
        runtimeObject = context.selection.selectedObject->GetRuntimeHandle();
    }
    ProceduralMeshSettings proceduralSettings{};
    const ProceduralMeshSettings* proceduralSettingsPtr = nullptr;
    for (const SceneComponentData& candidate : target.components) {
        if (candidate.type != "ProceduralMeshComponent") {
            continue;
        }
        ProceduralMeshComponent probe{};
        probe.Deserialize(candidate.properties);
        proceduralSettings = probe.GetSettings();
        proceduralSettingsPtr = &proceduralSettings;
        break;
    }
    const InspectorContext inspectorContext{&scene.GetAssetRegistry(),
                                            &scene.GetAssetDatabase(),
                                            &scene.GetSceneDocument(),
                                            &scene.GetWorld().Services(),
                                            runtimeObject.IsValid() ? &runtimeObject : nullptr,
                                            &modelMaterialSlotCatalog_,
                                            proceduralSettingsPtr};

    ImGui::SeparatorText(TEXT::Localize("editor.ui.document.components.c124e2").c_str());
    for (size_t componentIndex = 0; componentIndex < target.components.size(); ++componentIndex) {
        SceneComponentData& component = target.components[componentIndex];
        if (!filter.empty() && std::find(filter.begin(), filter.end(), component.type) == filter.end())
            continue;
        const ComponentTypeInfo* typeInfo = scene.GetComponentRegistry().Find(component.type);
        const std::string componentDisplayName = typeInfo != nullptr && !typeInfo->presentation.displayName.empty()
                                                     ? TEXT::Localize(typeInfo->presentation.displayName)
                                                     : component.type;
        const size_t componentTypeCount = static_cast<size_t>(std::count_if(
            target.components.begin(), target.components.end(),
            [&component](const SceneComponentData& candidate) { return candidate.type == component.type; }));
        const size_t componentTypeIndex =
            static_cast<size_t>(std::count_if(
                target.components.begin(), target.components.begin() + static_cast<std::ptrdiff_t>(componentIndex),
                [&component](const SceneComponentData& candidate) { return candidate.type == component.type; })) +
            1u;
        const std::string componentTreeLabel = componentTypeCount > 1u
                                                   ? componentDisplayName + " " + std::to_string(componentTypeIndex)
                                                   : componentDisplayName;

        const std::string componentUiId = "SceneComponent_" + std::to_string(component.id.value);
        ImGui::PushID(componentUiId.c_str());
        const bool allowDuplicate = typeInfo != nullptr && typeInfo->allowMultiple;
        const bool componentOpen =
            ImGui::TreeNodeEx(componentTreeLabel.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth |
                                                              (filter.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen));
        bool duplicateRequested = false;
        bool removeRequested = false;
        if (ImGui::BeginPopupContextItem("ComponentContext")) {
            if (allowDuplicate) {
                duplicateRequested =
                    ImGui::MenuItem(TEXT::LocalizeWindow("ui.materialGraph.duplicate", "Duplicate").c_str());
            }
            if (allowDuplicate) {
                ImGui::Separator();
            }
            removeRequested =
                ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.remove.component.b0b011", "Remove Component").c_str());
            ImGui::EndPopup();
        }

        bool componentStructureChanged = false;
        if (duplicateRequested) {
            const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
            const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
            const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
            const ComponentEditResult result =
                componentAuthoringService_.DuplicateComponent(scene.GetComponentRegistry(), target, componentIndex);
            context.componentAddStatusMessage = result.message;
            context.componentAddStatusIsError = !result.success;
            if (result.documentChanged) {
                context.sceneDirty = true;
                needsRebuild = true;
                forceImmediateRebuild = true;
                componentStructureChanged = true;
                historyRequest = SceneObjectAuthoringHistoryRequest{"Duplicate " + componentDisplayName, beforeObjects,
                                                                    scene.GetSceneDocument().objects,    beforeCamera,
                                                                    scene.GetSceneDocument().camera,     dirtyBefore};
            }
        }
        if (!componentStructureChanged && removeRequested) {
            const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
            const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
            const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
            const std::string removedType = component.type;
            const ComponentEditResult result = componentAuthoringService_.RemoveComponent(
                scene.GetComponentRegistry(), scene.GetSceneDocument(), target, componentIndex);
            context.componentAddStatusMessage = result.message;
            context.componentAddStatusIsError = !result.success;
            if (result.documentChanged) {
                if (removedType == "CameraComponent" && !HasDocumentComponent(target, "CameraComponent")) {
                    if (scene.GetSceneDocument().camera.defaultCameraObjectId == target.id) {
                        scene.ClearGameDefaultCamera();
                    }
                    if (scene.IsEditorCameraPreviewActive() && scene.GetEditorCameraPreviewObjectId() == target.id) {
                        scene.EndEditorCameraPreview();
                    }
                }
                context.sceneDirty = true;
                needsRebuild = true;
                forceImmediateRebuild = true;
                componentStructureChanged = true;
                historyRequest = SceneObjectAuthoringHistoryRequest{"Remove " + componentDisplayName, beforeObjects,
                                                                    scene.GetSceneDocument().objects, beforeCamera,
                                                                    scene.GetSceneDocument().camera,  dirtyBefore};
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
        const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
        if (componentDocumentEditor_.DrawComponent(scene.GetComponentRegistry(), component, componentInspectorBuilder_,
                                                   inspectorContext)) {
            if (!pendingComponentHistory_) {
                std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
                auto beforeObject =
                    std::find_if(beforeObjects.begin(), beforeObjects.end(),
                                 [&target](const SceneObjectData& object) { return object.id == target.id; });
                if (beforeObject != beforeObjects.end() && componentIndex < beforeObject->components.size()) {
                    beforeObject->components[componentIndex] = beforeComponent;
                    pendingComponentHistory_ =
                        PendingComponentHistory{"Edit " + componentDisplayName, std::move(beforeObjects),
                                                scene.GetSceneDocument().camera, dirtyBefore};
                }
            }
            context.sceneDirty = true;
            const bool applyOnCommit =
                typeInfo != nullptr && typeInfo->runtimeApplyPolicy == ComponentRuntimeApplyPolicy::OnEditCommit;
            if (applyOnCommit && ImGui::IsAnyItemActive()) {
                deferredRuntimeComponentApply_ = DeferredRuntimeComponentApply{target.id, component.id};
            } else {
                const bool appliedToRuntime =
                    ApplyDocumentComponentToRuntime(scene, target, component, componentIndex, context);
                if (!appliedToRuntime) {
                    needsRebuild = true;
                } else if (component.type == "CameraComponent") {
                    (void)scene.ApplyCameraRuntimeChanges();
                } else if (LIGHTING_AUTHORING::IsLightingComponentType(component.type)) {
                    (void)scene.RefreshLightingRuntime();
                } else if (component.type == "ProceduralMeshComponent") {
                    (void)scene.RebuildMaterialOverrides();
                } else if (component.type == "ModelComponent") {
                    (void)scene.RebuildMaterialOverrides();
                }
            }
        }

        ImGui::TreePop();
        ImGui::PopID();
    }

    const bool editingComponentParameter = ImGui::IsAnyItemActive();
    if (deferredRuntimeComponentApply_ && !editingComponentParameter) {
        const DeferredRuntimeComponentApply deferred = *deferredRuntimeComponentApply_;
        deferredRuntimeComponentApply_.reset();
        const std::optional<size_t> deferredIndex =
            deferred.objectId == target.id ? FindSceneComponentIndex(target, deferred.componentId) : std::nullopt;
        if (deferredIndex.has_value()) {
            const SceneComponentData& component = target.components[*deferredIndex];
            if (!ApplyDocumentComponentToRuntime(scene, target, component, *deferredIndex, context)) {
                needsRebuild = true;
            } else if (component.type == "CameraComponent") {
                (void)scene.ApplyCameraRuntimeChanges();
            } else if (LIGHTING_AUTHORING::IsLightingComponentType(component.type)) {
                (void)scene.RefreshLightingRuntime();
            } else if (component.type == "ProceduralMeshComponent") {
                (void)scene.RebuildMaterialOverrides();
            } else if (component.type == "ModelComponent") {
                (void)scene.RebuildMaterialOverrides();
            }
        } else {
            needsRebuild = true;
        }
    }
    if (needsRebuild) {
        if (editingComponentParameter && !forceImmediateRebuild) {
            deferredComponentRebuild_ = true;
        } else {
            SyncRuntimeTransformToDocument(target, context);
            selectionSync.ApplyRuntimeObjectChangesWithSelectionSync(
                scene, context.selection, context.nextSceneObjectId, {.changed = {target.id}});
            deferredComponentRebuild_ = false;
        }
    } else if (deferredComponentRebuild_ && !editingComponentParameter) {
        SyncRuntimeTransformToDocument(target, context);
        selectionSync.ApplyRuntimeObjectChangesWithSelectionSync(scene, context.selection, context.nextSceneObjectId,
                                                                 {.changed = {target.id}});
        deferredComponentRebuild_ = false;
    }

    if (pendingComponentHistory_ && !editingComponentParameter) {
        PendingComponentHistory pending = std::move(*pendingComponentHistory_);
        pendingComponentHistory_.reset();
        historyRequest = SceneObjectAuthoringHistoryRequest{
            std::move(pending.label),        std::move(pending.beforeObjects), scene.GetSceneDocument().objects,
            std::move(pending.beforeCamera), scene.GetSceneDocument().camera,  pending.dirtyBefore};
    }
    return historyRequest;
#else
    (void)scene;
    (void)context;
    (void)selectionSync;
    (void)target;
    (void)filter;
    return std::nullopt;
#endif
}

} // namespace HIKARI
