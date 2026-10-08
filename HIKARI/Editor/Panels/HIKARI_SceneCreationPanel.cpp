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

    const std::string emptyObjectLabel = TEXT::Localize("ui.sceneCreation.emptyObject");
    if (MatchesSearch(emptyObjectLabel.c_str(), search) && ImGui::MenuItem(emptyObjectLabel.c_str())) {
        CreateEmptyObject(scene, context, selectionSync);
    }

    const std::string worldSpriteLabel = TEXT::Localize("ui.sceneCreation.worldSprite");
    if (MatchesSearch(worldSpriteLabel.c_str(), search) && ImGui::MenuItem(worldSpriteLabel.c_str())) {
        CreateWorldSpriteObject(scene, context, selectionSync);
    }

    const std::string worldTextLabel = TEXT::Localize("ui.sceneCreation.worldText");
    if (MatchesSearch(worldTextLabel.c_str(), search) && ImGui::MenuItem(worldTextLabel.c_str())) {
        CreateWorldTextObject(scene, context, selectionSync);
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

    const std::string waterSurfaceLabel = TEXT::Localize("ui.sceneCreation.waterSurface");
    if (MatchesSearch(waterSurfaceLabel.c_str(), search) && ImGui::MenuItem(waterSurfaceLabel.c_str())) {
        CreateWaterSurfaceObject(scene, context, selectionSync);
    }

    const std::string terrainSurfaceLabel = TEXT::Localize("ui.sceneCreation.terrain");
    if (MatchesSearch(terrainSurfaceLabel.c_str(), search) && ImGui::MenuItem(terrainSurfaceLabel.c_str())) {
        CreateTerrainSurfaceObject(scene, context, selectionSync);
    }

    const std::string dialogueTriggerLabel = TEXT::Localize("ui.sceneCreation.dialogueTrigger");
    if (MatchesSearch(dialogueTriggerLabel.c_str(), search) && ImGui::MenuItem(dialogueTriggerLabel.c_str())) {
        CreateDialogueTriggerObject(scene, context, selectionSync);
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
            CreateLightObject(scene, context, selectionSync, RENDER3D::LIGHTING::SceneLightType::Point);
        }
        if (showSpotLight && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.spotLight").c_str())) {
            CreateLightObject(scene, context, selectionSync, RENDER3D::LIGHTING::SceneLightType::Spot);
        }
        if (showTubeLight && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.tubeLight").c_str())) {
            CreateLightObject(scene, context, selectionSync, RENDER3D::LIGHTING::SceneLightType::Tube);
        }
        if (showRectLight && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.rectLight").c_str())) {
            CreateLightObject(scene, context, selectionSync, RENDER3D::LIGHTING::SceneLightType::Rect);
        }
        if (showLight && (showReflectionProbe || showIrradianceVolume)) {
            ImGui::Separator();
        }
        if (showReflectionProbe && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.reflectionProbe").c_str())) {
            CreateLightingObject(scene, context, selectionSync, true);
        }
        if (showIrradianceVolume && ImGui::MenuItem(TEXT::Localize("ui.sceneCreation.irradianceVolume").c_str())) {
            CreateLightingObject(scene, context, selectionSync, false);
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

    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    primitiveRequest.object.position = ComputePrimitivePlacementInView(scene.GetCamera(), primitiveRequest.mesh);
    GameObject* object = CreatePrimitiveObject(scene, primitiveRequest);
    context.selection.SelectObject(scene.GetWorld(), object);
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    historyRequest_ = SceneObjectAuthoringHistoryRequest{std::string("Create ") + ToString(primitiveRequest.mesh.kind),
                                                         beforeObjects,
                                                         scene.GetSceneDocument().objects,
                                                         beforeCamera,
                                                         scene.GetSceneDocument().camera,
                                                         dirtyBefore};
#else
    (void)scene;
    (void)context;
    (void)selectionSync;
#endif
}

void SceneCreationPanel::CreateEmptyObject(DocumentSceneBase& scene, EditorContext& context,
                                           const SelectionSyncService& selectionSync) {
    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    CreateObjectRequest request{};
    request.position = ComputeObjectPlacementInView(scene.GetCamera());
    GameObject* object = EDITOR::CreateEmptyObject(scene, request);
    context.selection.SelectObject(scene.GetWorld(), object);
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    historyRequest_ = SceneObjectAuthoringHistoryRequest{
        "Create Empty Object",           beforeObjects, scene.GetSceneDocument().objects, beforeCamera,
        scene.GetSceneDocument().camera, dirtyBefore};
}

void SceneCreationPanel::CreateWorldSpriteObject(DocumentSceneBase& scene, EditorContext& context,
                                                 const SelectionSyncService& selectionSync) {
    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    CreateObjectRequest request{};
    request.name = TEXT::Localize("ui.sceneCreation.worldSprite");
    request.position = ComputeObjectPlacementInView(scene.GetCamera());
    GameObject* object = EDITOR::CreateWorldSpriteObject(scene, {}, request);
    context.selection.SelectObject(scene.GetWorld(), object);
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    historyRequest_ = SceneObjectAuthoringHistoryRequest{
        "Create World Sprite",           beforeObjects, scene.GetSceneDocument().objects, beforeCamera,
        scene.GetSceneDocument().camera, dirtyBefore};
}

void SceneCreationPanel::CreateWorldTextObject(DocumentSceneBase& scene, EditorContext& context,
                                               const SelectionSyncService& selectionSync) {
    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    CreateWorldTextRequest request{};
    request.object.name = TEXT::Localize("ui.sceneCreation.worldText");
    request.object.position = ComputeObjectPlacementInView(scene.GetCamera());
    GameObject* object = EDITOR::CreateWorldTextObject(scene, request);
    context.selection.SelectObject(scene.GetWorld(), object);
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    historyRequest_ = SceneObjectAuthoringHistoryRequest{"Create World Text",
                                                         beforeObjects,
                                                         scene.GetSceneDocument().objects,
                                                         beforeCamera,
                                                         scene.GetSceneDocument().camera,
                                                         dirtyBefore};
}

void SceneCreationPanel::CreateLightingObject(DocumentSceneBase& scene, EditorContext& context,
                                              const SelectionSyncService& selectionSync, bool reflectionProbe) {
    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    CreateObjectRequest request{};
    request.position = ComputeObjectPlacementInView(scene.GetCamera());
    GameObject* object = reflectionProbe ? EDITOR::CreateReflectionProbeObject(scene, request)
                                         : EDITOR::CreateIrradianceVolumeObject(scene, request);
    context.selection.SelectObject(scene.GetWorld(), object);
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    historyRequest_ =
        SceneObjectAuthoringHistoryRequest{reflectionProbe ? "Create Reflection Probe" : "Create Irradiance Volume",
                                           beforeObjects,
                                           scene.GetSceneDocument().objects,
                                           beforeCamera,
                                           scene.GetSceneDocument().camera,
                                           dirtyBefore};
}

void SceneCreationPanel::CreateWaterSurfaceObject(DocumentSceneBase& scene, EditorContext& context,
                                                  const SelectionSyncService& selectionSync) {
    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    CreateObjectRequest request{};
    request.position = ComputeObjectPlacementInView(scene.GetCamera());
    GameObject* object = EDITOR::CreateWaterSurfaceObject(scene, request);
    context.selection.SelectObject(scene.GetWorld(), object);
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    historyRequest_ = SceneObjectAuthoringHistoryRequest{"Create Water Surface",           beforeObjects,
                                                         scene.GetSceneDocument().objects, beforeCamera,
                                                         scene.GetSceneDocument().camera,  dirtyBefore};
}

void SceneCreationPanel::CreateLightObject(DocumentSceneBase& scene, EditorContext& context,
                                           const SelectionSyncService& selectionSync,
                                           RENDER3D::LIGHTING::SceneLightType lightType) {
    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    CreateObjectRequest request{};
    request.position = ComputeObjectPlacementInView(scene.GetCamera());
    GameObject* object = EDITOR::CreateLightObject(scene, request, lightType);
    context.selection.SelectObject(scene.GetWorld(), object);
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    const std::string label =
        std::string(TEXT::EnumToName(RENDER3D::LIGHTING::kSceneLightTypeNames, lightType)) + " Light";
    historyRequest_ = SceneObjectAuthoringHistoryRequest{"Create " + label,
                                                         beforeObjects,
                                                         scene.GetSceneDocument().objects,
                                                         beforeCamera,
                                                         scene.GetSceneDocument().camera,
                                                         dirtyBefore};
}

void SceneCreationPanel::CreateTerrainSurfaceObject(DocumentSceneBase& scene, EditorContext& context,
                                                    const SelectionSyncService& selectionSync) {
    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    CreateObjectRequest request{};
    request.position = ComputeObjectPlacementInView(scene.GetCamera());
    GameObject* object = EDITOR::CreateTerrainSurfaceObject(scene, request);
    context.selection.SelectObject(scene.GetWorld(), object);
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    historyRequest_ = SceneObjectAuthoringHistoryRequest{"Create Terrain",
                                                         beforeObjects,
                                                         scene.GetSceneDocument().objects,
                                                         beforeCamera,
                                                         scene.GetSceneDocument().camera,
                                                         dirtyBefore};
}

void SceneCreationPanel::CreateDialogueTriggerObject(DocumentSceneBase& scene, EditorContext& context,
                                                     const SelectionSyncService& selectionSync) {
    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    CreateObjectRequest request{};
    request.position = ComputeObjectPlacementInView(scene.GetCamera());
    GameObject* object = EDITOR::CreateDialogueTriggerObject(scene, request);
    context.selection.SelectObject(scene.GetWorld(), object);
    context.sceneDirty = true;
    selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    historyRequest_ = SceneObjectAuthoringHistoryRequest{"Create Dialogue Trigger",        beforeObjects,
                                                         scene.GetSceneDocument().objects, beforeCamera,
                                                         scene.GetSceneDocument().camera,  dirtyBefore};
}

std::optional<SceneObjectAuthoringHistoryRequest> SceneCreationPanel::ConsumeHistoryRequest() {
    std::optional<SceneObjectAuthoringHistoryRequest> request = std::move(historyRequest_);
    historyRequest_.reset();
    return request;
}

} // namespace HIKARI::EDITOR
