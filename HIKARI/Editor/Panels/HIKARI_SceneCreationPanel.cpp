#include "Editor/Panels/HIKARI_SceneCreationPanel.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Authoring/HIKARI_EditorObjectPlacement.h"
#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {

        bool Matches(std::string_view label, std::string_view search) {
            return search.empty() ||
                TEXT::ToLowerAsciiCopy(label).find(TEXT::ToLowerAsciiCopy(search)) !=
                    std::string::npos;
        }
    }

    void SceneCreationPanel::DrawCreationMenu(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        SceneObjectCommandService& commands) {
#if defined(HIKARI_WITH_EDITOR)
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputTextWithHint(
            "##CreateSearch",
            TEXT::Localize("ui.sceneCreation.searchHint").c_str(),
            searchBuffer_,
            sizeof(searchBuffer_));
        const std::string search = TEXT::ToLowerAsciiCopy(searchBuffer_);
        ImGui::Separator();

        const std::string emptyObjectLabel =
            TEXT::Localize("ui.sceneCreation.emptyObject");
        if (Matches(emptyObjectLabel.c_str(), search) &&
            ImGui::MenuItem(emptyObjectLabel.c_str())) {
            CreateEmptyObject(scene, context, selectionSync);
        }

        const struct PrimitiveEntry {
            std::string label;
            ProceduralMeshKind kind;
        } primitives[] = {
            { TEXT::Localize("ui.sceneCreation.plane"),
              ProceduralMeshKind::Plane },
            { TEXT::Localize("ui.sceneCreation.gridPlane"),
              ProceduralMeshKind::GridPlane },
            { TEXT::Localize("ui.sceneCreation.box"),
              ProceduralMeshKind::Box },
            { TEXT::Localize("ui.sceneCreation.sphere"),
              ProceduralMeshKind::Sphere },
            { TEXT::Localize("ui.sceneCreation.cylinder"),
              ProceduralMeshKind::Cylinder },
            { TEXT::Localize("ui.sceneCreation.capsule"),
              ProceduralMeshKind::Capsule },
        };

        bool anyPrimitive = false;
        for (const PrimitiveEntry& primitive : primitives) {
            anyPrimitive |= Matches(primitive.label.c_str(), search);
        }
        if (anyPrimitive && ImGui::BeginMenu("Procedural Mesh")) {
            for (const PrimitiveEntry& primitive : primitives) {
                if (!Matches(primitive.label.c_str(), search)) {
                    continue;
                }
                if (ImGui::MenuItem(primitive.label.c_str())) {
                    primitiveCreationDialog_.Open(primitive.kind);
                }
            }
            ImGui::EndMenu();
        }

        const bool showReflectionProbe =
            Matches(
                TEXT::Localize(
                    "ui.sceneCreation.reflectionProbe").c_str(),
                search);
        const bool showIrradianceVolume =
            Matches(
                TEXT::Localize(
                    "ui.sceneCreation.irradianceVolume").c_str(),
                search);
        const bool showDirectionalLight =
            Matches(
                TEXT::Localize(
                    "ui.sceneCreation.directionalLight").c_str(),
                search);
        const bool showPointLight =
            Matches(
                TEXT::Localize("ui.sceneCreation.pointLight").c_str(),
                search);
        const bool showSpotLight =
            Matches(
                TEXT::Localize("ui.sceneCreation.spotLight").c_str(),
                search);
        if ((showReflectionProbe || showIrradianceVolume ||
             showDirectionalLight || showPointLight || showSpotLight) &&
            ImGui::BeginMenu(
                TEXT::Localize("ui.sceneCreation.lighting").c_str())) {
            if (showDirectionalLight &&
                ImGui::MenuItem(
                    TEXT::Localize(
                        "ui.sceneCreation.directionalLight").c_str())) {
                CreateLightObject(
                    scene,
                    context,
                    selectionSync,
                    RENDER3D::LIGHTING::SceneLightType::Directional);
            }
            if (showPointLight &&
                ImGui::MenuItem(
                    TEXT::Localize("ui.sceneCreation.pointLight").c_str())) {
                CreateLightObject(
                    scene,
                    context,
                    selectionSync,
                    RENDER3D::LIGHTING::SceneLightType::Point);
            }
            if (showSpotLight &&
                ImGui::MenuItem(
                    TEXT::Localize("ui.sceneCreation.spotLight").c_str())) {
                CreateLightObject(
                    scene,
                    context,
                    selectionSync,
                    RENDER3D::LIGHTING::SceneLightType::Spot);
            }
            if ((showDirectionalLight || showPointLight ||
                 showSpotLight) &&
                (showReflectionProbe || showIrradianceVolume)) {
                ImGui::Separator();
            }
            if (showReflectionProbe &&
                ImGui::MenuItem(
                    TEXT::Localize(
                        "ui.sceneCreation.reflectionProbe").c_str())) {
                CreateLightingObject(
                    scene,
                    context,
                    selectionSync,
                    true);
            }
            if (showIrradianceVolume &&
                ImGui::MenuItem(
                    TEXT::Localize(
                        "ui.sceneCreation.irradianceVolume").c_str())) {
                CreateLightingObject(
                    scene,
                    context,
                    selectionSync,
                    false);
            }
            ImGui::EndMenu();
        }

        const std::vector<std::string> prefabIds = commands.ListPrefabIds();
        if (!prefabIds.empty() &&
            (search.empty() ||
             Matches(
                 TEXT::Localize("ui.sceneCreation.prefab").c_str(),
                 search)) &&
            ImGui::BeginMenu(
                TEXT::Localize("ui.sceneCreation.prefab").c_str())) {
            for (const std::string& prefabId : prefabIds) {
                if (search.empty() || Matches(prefabId, search)) {
                    if (ImGui::MenuItem(prefabId.c_str())) {
                        commands.Execute(
                            SceneObjectCommandId::InstantiatePrefab,
                            scene,
                            context,
                            selectionSync,
                            prefabId);
                    }
                }
            }
            ImGui::EndMenu();
        }
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)commands;
#endif
    }

    void SceneCreationPanel::DrawDeferredDialogs(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync) {
#if defined(HIKARI_WITH_EDITOR)
        CreatePrimitiveRequest primitiveRequest{};
        if (!primitiveCreationDialog_.Draw(primitiveRequest)) {
            return;
        }

        const bool dirtyBefore =
            context.sceneDirty || scene.HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects =
            scene.GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera =
            scene.GetSceneDocument().camera;
        primitiveRequest.object.position =
            ComputePrimitivePlacementInView(
                scene.GetCamera(),
                primitiveRequest.mesh);
        GameObject* object = CreatePrimitiveObject(scene, primitiveRequest);
        context.selection.SelectObject(scene.GetWorld(), object);
        context.sceneDirty = true;
        selectionSync.SyncNextSceneObjectId(
            scene,
            context.nextSceneObjectId);
        historyRequest_ = SceneObjectAuthoringHistoryRequest{
            std::string("Create ") + ToString(primitiveRequest.mesh.kind),
            beforeObjects,
            scene.GetSceneDocument().objects,
            beforeCamera,
            scene.GetSceneDocument().camera,
            dirtyBefore
        };
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
#endif
    }

    void SceneCreationPanel::CreateEmptyObject(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync) {

        const bool dirtyBefore =
            context.sceneDirty || scene.HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects =
            scene.GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera =
            scene.GetSceneDocument().camera;
        CreateObjectRequest request{};
        request.position = ComputeObjectPlacementInView(scene.GetCamera());
        GameObject* object = EDITOR::CreateEmptyObject(scene, request);
        context.selection.SelectObject(scene.GetWorld(), object);
        context.sceneDirty = true;
        selectionSync.SyncNextSceneObjectId(
            scene,
            context.nextSceneObjectId);
        historyRequest_ = SceneObjectAuthoringHistoryRequest{
            "Create Empty Object",
            beforeObjects,
            scene.GetSceneDocument().objects,
            beforeCamera,
            scene.GetSceneDocument().camera,
            dirtyBefore
        };
    }

    void SceneCreationPanel::CreateLightingObject(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        bool reflectionProbe) {

        const bool dirtyBefore =
            context.sceneDirty || scene.HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects =
            scene.GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera =
            scene.GetSceneDocument().camera;
        CreateObjectRequest request{};
        request.position = ComputeObjectPlacementInView(scene.GetCamera());
        GameObject* object = reflectionProbe
            ? EDITOR::CreateReflectionProbeObject(scene, request)
            : EDITOR::CreateIrradianceVolumeObject(scene, request);
        context.selection.SelectObject(scene.GetWorld(), object);
        context.sceneDirty = true;
        selectionSync.SyncNextSceneObjectId(
            scene,
            context.nextSceneObjectId);
        historyRequest_ = SceneObjectAuthoringHistoryRequest{
            reflectionProbe
                ? "Create Reflection Probe"
                : "Create Irradiance Volume",
            beforeObjects,
            scene.GetSceneDocument().objects,
            beforeCamera,
            scene.GetSceneDocument().camera,
            dirtyBefore
        };
    }

    void SceneCreationPanel::CreateLightObject(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        RENDER3D::LIGHTING::SceneLightType lightType) {

        const bool dirtyBefore =
            context.sceneDirty || scene.HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects =
            scene.GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera =
            scene.GetSceneDocument().camera;
        CreateObjectRequest request{};
        request.position = ComputeObjectPlacementInView(scene.GetCamera());
        GameObject* object = EDITOR::CreateLightObject(
            scene,
            request,
            lightType);
        context.selection.SelectObject(scene.GetWorld(), object);
        context.sceneDirty = true;
        selectionSync.SyncNextSceneObjectId(
            scene,
            context.nextSceneObjectId);
        const char* label = "Point Light";
        if (lightType ==
            RENDER3D::LIGHTING::SceneLightType::Directional) {
            label = "Directional Light";
        } else if (lightType ==
            RENDER3D::LIGHTING::SceneLightType::Spot) {
            label = "Spot Light";
        }
        historyRequest_ = SceneObjectAuthoringHistoryRequest{
            std::string("Create ") + label,
            beforeObjects,
            scene.GetSceneDocument().objects,
            beforeCamera,
            scene.GetSceneDocument().camera,
            dirtyBefore
        };
    }

    std::optional<SceneObjectAuthoringHistoryRequest>
        SceneCreationPanel::ConsumeHistoryRequest() {
        std::optional<SceneObjectAuthoringHistoryRequest> request =
            std::move(historyRequest_);
        historyRequest_.reset();
        return request;
    }

} // namespace HIKARI::EDITOR
