#include "Editor/Panels/HIKARI_SceneCreationPanel.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"

#include <algorithm>
#include <cctype>
#include <functional>
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
bool SceneCreationPanel::MatchesSearch(std::string_view label, std::string_view search) {
    return search.empty() || TEXT::ToLowerAsciiCopy(label).find(TEXT::ToLowerAsciiCopy(search)) != std::string::npos;
}

void SceneCreationPanel::DrawCreationMenu(DocumentSceneBase& scene, EditorContext& context,
                                          const SelectionSyncService& selectionSync,
                                          SceneObjectCommandService& commands) {
#if defined(HIKARI_WITH_EDITOR)
    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputTextWithHint("##CreateSearch", TEXT::Localize("ui.sceneCreation.searchHint").c_str(), searchBuffer_,
                             sizeof(searchBuffer_));
    const std::string search = TEXT::ToLowerAsciiCopy(searchBuffer_);
    ImGui::Separator();

    // 視点の前に置く物体。make は配置済みの要求から物体を作る。
    const auto createInView = [&](std::string historyLabel, const auto& make) {
        CreateWithHistory(scene, context, selectionSync, std::move(historyLabel), [&] {
            CreateObjectRequest request{};
            request.position = ComputeObjectPlacementInView(scene.GetCamera());
            return make(request);
        });
    };
    const auto menuItem = [&](const char* key) {
        const std::string label = TEXT::Localize(key);
        return MatchesSearch(label.c_str(), search) && ImGui::MenuItem(label.c_str());
    };

    if (menuItem("ui.sceneCreation.emptyObject")) {
        createInView("Create Empty Object",
                     [&](const CreateObjectRequest& request) { return EDITOR::CreateEmptyObject(scene, request); });
    }
    if (menuItem("ui.sceneCreation.worldSprite")) {
        createInView("Create World Sprite", [&](CreateObjectRequest request) {
            request.name = TEXT::Localize("ui.sceneCreation.worldSprite");
            return EDITOR::CreateWorldSpriteObject(scene, {}, request);
        });
    }
    if (menuItem("ui.sceneCreation.worldText")) {
        createInView("Create World Text", [&](const CreateObjectRequest& request) {
            CreateWorldTextRequest text{};
            text.object = request;
            text.object.name = TEXT::Localize("ui.sceneCreation.worldText");
            return EDITOR::CreateWorldTextObject(scene, text);
        });
    }
    if (menuItem("ui.sceneCreation.decal")) {
        createInView("Create Decal",
                     [&](const CreateObjectRequest& request) { return EDITOR::CreateDecalObject(scene, request); });
    }

    DrawUiCreationMenu(scene, context, selectionSync, search);

    const struct PrimitiveEntry {
        std::string label;
        ProceduralMeshKind kind;
    } primitives[] = {
        {TEXT::Localize("ui.sceneCreation.plane"), ProceduralMeshKind::Plane},
        {TEXT::Localize("ui.sceneCreation.gridPlane"), ProceduralMeshKind::GridPlane},
        {TEXT::Localize("ui.sceneCreation.box"), ProceduralMeshKind::Box},
        {TEXT::Localize("ui.sceneCreation.sphere"), ProceduralMeshKind::Sphere},
        {TEXT::Localize("ui.sceneCreation.cylinder"), ProceduralMeshKind::Cylinder},
        {TEXT::Localize("ui.sceneCreation.capsule"), ProceduralMeshKind::Capsule},
    };

    bool anyPrimitive = false;
    for (const PrimitiveEntry& primitive : primitives) {
        anyPrimitive |= MatchesSearch(primitive.label.c_str(), search);
    }
    if (anyPrimitive &&
        ImGui::BeginMenu(TEXT::LocalizeWindow("ui.sceneCreation.proceduralMesh", "Procedural Mesh").c_str())) {
        for (const PrimitiveEntry& primitive : primitives) {
            if (!MatchesSearch(primitive.label.c_str(), search)) {
                continue;
            }
            if (ImGui::MenuItem(primitive.label.c_str())) {
                primitiveCreationDialog_.Open(primitive.kind);
            }
        }
        ImGui::EndMenu();
    }

    if (menuItem("ui.sceneCreation.waterSurface")) {
        createInView("Create Water Surface", [&](const CreateObjectRequest& request) {
            return EDITOR::CreateWaterSurfaceObject(scene, request);
        });
    }
    if (menuItem("ui.sceneCreation.terrain")) {
        createInView("Create Terrain", [&](const CreateObjectRequest& request) {
            return EDITOR::CreateTerrainSurfaceObject(scene, request);
        });
    }
    if (menuItem("ui.sceneCreation.dialogueTrigger")) {
        createInView("Create Dialogue Trigger", [&](const CreateObjectRequest& request) {
            return EDITOR::CreateDialogueTriggerObject(scene, request);
        });
    }

    const bool showReflectionProbe = MatchesSearch(TEXT::Localize("ui.sceneCreation.reflectionProbe").c_str(), search);
    const bool showIrradianceVolume =
        MatchesSearch(TEXT::Localize("ui.sceneCreation.irradianceVolume").c_str(), search);
    const bool showPointLight = MatchesSearch(TEXT::Localize("ui.sceneCreation.pointLight").c_str(), search);
    const bool showSpotLight = MatchesSearch(TEXT::Localize("ui.sceneCreation.spotLight").c_str(), search);
    const bool showTubeLight = MatchesSearch(TEXT::Localize("ui.sceneCreation.tubeLight").c_str(), search);
    const bool showRectLight = MatchesSearch(TEXT::Localize("ui.sceneCreation.rectLight").c_str(), search);
    const bool showLight = showPointLight || showSpotLight || showTubeLight || showRectLight;
    if ((showReflectionProbe || showIrradianceVolume || showLight) &&
        ImGui::BeginMenu(TEXT::Localize("ui.sceneCreation.lighting").c_str())) {
        if (showPointLight && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.pointLight").c_str())) {
            createInView("Create Point Light", [&](const CreateObjectRequest& request) {
                return EDITOR::CreateLightObject(scene, request, RENDER3D::LIGHTING::SceneLightType::Point,
                                                 LightIntensityUnit::Lumens);
            });
        }
        if (showSpotLight && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.spotLight").c_str())) {
            createInView("Create Spot Light", [&](const CreateObjectRequest& request) {
                return EDITOR::CreateLightObject(scene, request, RENDER3D::LIGHTING::SceneLightType::Spot,
                                                 LightIntensityUnit::Lumens);
            });
        }
        if (showTubeLight && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.tubeLight").c_str())) {
            createInView("Create Tube Light", [&](const CreateObjectRequest& request) {
                return EDITOR::CreateLightObject(scene, request, RENDER3D::LIGHTING::SceneLightType::Tube,
                                                 LightIntensityUnit::Lumens);
            });
        }
        if (showRectLight && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.rectLight").c_str())) {
            createInView("Create Rect Light", [&](const CreateObjectRequest& request) {
                return EDITOR::CreateLightObject(scene, request, RENDER3D::LIGHTING::SceneLightType::Rect,
                                                 LightIntensityUnit::Lumens);
            });
        }
        if (showLight && (showReflectionProbe || showIrradianceVolume)) {
            ImGui::Separator();
        }
        if (showReflectionProbe && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.reflectionProbe").c_str())) {
            createInView("Create Reflection Probe", [&](const CreateObjectRequest& request) {
                return EDITOR::CreateReflectionProbeObject(scene, request);
            });
        }
        if (showIrradianceVolume && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.irradianceVolume").c_str())) {
            createInView("Create Irradiance Volume", [&](const CreateObjectRequest& request) {
                return EDITOR::CreateIrradianceVolumeObject(scene, request);
            });
        }
        ImGui::EndMenu();
    }

    const std::vector<PrefabAssetEntry> prefabs = commands.ListPrefabs(scene.GetAssetDatabase());
    if (!prefabs.empty() &&
        (search.empty() || MatchesSearch(TEXT::Localize("ui.sceneCreation.prefab").c_str(), search)) &&
        ImGui::BeginMenu(TEXT::Localize("ui.sceneCreation.prefab").c_str())) {
        for (const PrefabAssetEntry& prefab : prefabs) {
            if (search.empty() || MatchesSearch(prefab.name, search)) {
                if (ImGui::MenuItem(prefab.name.c_str())) {
                    commands.Execute(SceneObjectCommandId::InstantiatePrefab, scene, context, selectionSync,
                                     prefab.guid.value);
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

void SceneCreationPanel::DrawDeferredDialogs(DocumentSceneBase& scene, EditorContext& context,
                                             const SelectionSyncService& selectionSync) {
#if defined(HIKARI_WITH_EDITOR)
    CreatePrimitiveRequest primitiveRequest{};
    if (!primitiveCreationDialog_.Draw(primitiveRequest)) {
        return;
    }

    CreateWithHistory(scene, context, selectionSync, std::string("Create ") + ToString(primitiveRequest.mesh.kind),
                      [&] {
                          primitiveRequest.object.position =
                              ComputePrimitivePlacementInView(scene.GetCamera(), primitiveRequest.mesh);
                          return CreatePrimitiveObject(scene, primitiveRequest);
                      });
#else
    (void)scene;
    (void)context;
    (void)selectionSync;
#endif
}

void SceneCreationPanel::CreateWithHistory(DocumentSceneBase& scene, EditorContext& context,
                                           const SelectionSyncService& selectionSync, std::string historyLabel,
                                           const std::function<GameObject*()>& create) {
    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    GameObject* object = create();
    if (scene.GetSceneDocument().objects.size() == beforeObjects.size()) {
        return;
    }
    if (object != nullptr) {
        context.selection.SelectObject(scene.GetWorld(), object);
    }
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    historyRequest_ = SceneObjectAuthoringHistoryRequest{std::move(historyLabel),          std::move(beforeObjects),
                                                         scene.GetSceneDocument().objects, beforeCamera,
                                                         scene.GetSceneDocument().camera,  dirtyBefore};
}

std::optional<SceneObjectAuthoringHistoryRequest> SceneCreationPanel::ConsumeHistoryRequest() {
    std::optional<SceneObjectAuthoringHistoryRequest> request = std::move(historyRequest_);
    historyRequest_.reset();
    return request;
}

} // namespace HIKARI::EDITOR
