#include "Editor/Panels/HIKARI_SceneCreationPanel.h"
#include "Core/Text/HIKARI_AsciiCase.h"

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
            "Search object types...",
            searchBuffer_,
            sizeof(searchBuffer_));
        const std::string search = TEXT::ToLowerAsciiCopy(searchBuffer_);
        ImGui::Separator();

        if (Matches("Empty Object", search) &&
            ImGui::MenuItem("Empty Object")) {
            CreateEmptyObject(scene, context, selectionSync);
        }

        const struct PrimitiveEntry {
            const char* label;
            ProceduralMeshKind kind;
        } primitives[] = {
            { "Plane", ProceduralMeshKind::Plane },
            { "Grid Plane", ProceduralMeshKind::GridPlane },
            { "Box", ProceduralMeshKind::Box },
            { "Sphere", ProceduralMeshKind::Sphere },
            { "Cylinder", ProceduralMeshKind::Cylinder },
            { "Capsule", ProceduralMeshKind::Capsule },
        };

        bool anyPrimitive = false;
        for (const PrimitiveEntry& primitive : primitives) {
            anyPrimitive |= Matches(primitive.label, search);
        }
        if (anyPrimitive && ImGui::BeginMenu("Procedural Mesh")) {
            for (const PrimitiveEntry& primitive : primitives) {
                if (!Matches(primitive.label, search)) {
                    continue;
                }
                if (ImGui::MenuItem(primitive.label)) {
                    primitiveCreationDialog_.Open(primitive.kind);
                }
            }
            ImGui::EndMenu();
        }

        const std::vector<std::string> prefabIds = commands.ListPrefabIds();
        if (!prefabIds.empty() &&
            (search.empty() || Matches("Prefab", search)) &&
            ImGui::BeginMenu("Prefab")) {
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

    std::optional<SceneObjectAuthoringHistoryRequest>
        SceneCreationPanel::ConsumeHistoryRequest() {
        std::optional<SceneObjectAuthoringHistoryRequest> request =
            std::move(historyRequest_);
        historyRequest_.reset();
        return request;
    }

} // namespace HIKARI::EDITOR
