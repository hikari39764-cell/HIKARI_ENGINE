#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Render3D/Shadow/HIKARI_LocalLightShadowRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

#include "imgui.h"

namespace HIKARI {

namespace {

constexpr float kDegreesPerRadian = 57.2957795f;

struct SceneCameraEntry {
    SceneObjectId id{};
    std::string name;
};

// 場面の機位（摄像机を持つ物体）を文書の順で集める。数字キー 1〜9 と一覧の番号はこの順。
std::vector<SceneCameraEntry> CollectSceneCameras(const SceneDocument& document) {
    std::vector<SceneCameraEntry> cameras;
    for (const SceneObjectData& object : document.objects) {
        for (const SceneComponentData& component : object.components) {
            if (component.type == "CameraComponent") {
                cameras.push_back({object.id, object.name});
                break;
            }
        }
    }
    return cameras;
}

std::string PlaceItemName(EDITOR::LookDevPlaceItem item) {
    return TEXT::Localize(std::string("ui.lookDev.item.") + TEXT::EnumToName(EDITOR::kLookDevPlaceItemNames, item));
}

} // namespace

void DocumentSceneEditorController::DrawLookDevWorkspace(DocumentSceneBase& scene,
                                                         EDITOR::EditorPlaySession& playSession) {
    lookDevWorkspace_.DrawDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::LookDev));
    // 布景は視口が主役なので、場面の工作区で閉じていても出す。
    context_.windows.viewport.showGameView = true;
    DrawGameViewportWindow(scene, playSession, "LookDev/Viewport");
    DrawLookDevPlaceWindow(scene);
    DrawLookDevObjectsWindow(scene);
    DrawLookDevLightsWindow(scene);
    if (ImGui::Begin(TEXT::LocalizeWindow("ui.window.inspector", "LookDev/Inspector").c_str())) {
        sceneInspectorPanel_.DrawContents(scene, context_, selectionSync_, sceneObjectCommands_);
    }
    ImGui::End();
    // 一覧の数値欄や視口の手柄のドラッグが終わったら、一つの取り消しとして積む。
    if (lookDevDrag_ && lookDevLightHandle_ == EDITOR::LookDevLightHandle::None && !ImGui::IsAnyItemActive()) {
        FinishLookDevObjectDrag(scene);
    }
    ProcessSceneAuthoringRequests(scene);
    if (!DrawEnvironmentWindow(scene, true) && !lookDevMainLightDrag_) {
        documentHistory_.SealMerge();
    }
    DrawPendingSceneOpenModal(scene);
    SaveRenderQualityWhenIdle(scene);
}

void DocumentSceneEditorController::DrawLookDevPlaceWindow(DocumentSceneBase& scene) {
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.lookDev.place", "LookDev/Place").c_str())) {
        ImGui::End();
        return;
    }
    lookDevWorkspace_.DrawPalette();
    DrawLookDevCameras(scene);
    ImGui::End();
}

void DocumentSceneEditorController::DrawLookDevObjectsWindow(DocumentSceneBase& scene) {
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.sceneWorkspace.objects", "LookDev/Objects").c_str())) {
        ImGui::End();
        return;
    }
    DrawLookDevArrangeSection(scene);
    hierarchyPanel_.DrawContents(
        scene.GetWorld(), context_.selection,
        [&](GameObject& object) {
            sceneInspectorPanel_.DrawObjectContextMenu(scene, context_, selectionSync_, sceneObjectCommands_, object);
        },
        [&](const GameObject& object) {
            return EDITOR::IsObjectEditorLocked(scene.GetSceneDocument(), object.GetDocumentId());
        },
        &scene.GetSceneDocument(), &operationRegistry_, scene.GetSceneDocumentRevision());
    if (!scene.IsRuntimePlayActive()) {
        (void)commandRouter_.ProcessViewportShortcuts(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    }
    ImGui::End();
}

void DocumentSceneEditorController::DrawLookDevCameras(DocumentSceneBase& scene) {
    ImGui::SeparatorText(TEXT::Localize("ui.lookDev.cameras").c_str());
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.saveView").c_str(), "##saveView",
                             EDITOR::EditorButtonTone::Primary, ImVec2(-1.0f, 0.0f),
                             TEXT::Localize("ui.lookDev.saveViewHint").c_str())) {
        SaveViewAsCamera(scene);
    }
    // 人眼高度：足元の面から 1.6 m の高さへ（構図を人の目線で確かめる）。
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.eyeHeight").c_str(), "##eyeHeight",
                             EDITOR::EditorButtonTone::Neutral, ImVec2(-1.0f, 0.0f),
                             TEXT::Localize("ui.lookDev.eyeHeightHint").c_str())) {
        DebugCameraController3D& camera = scene.GetDebugCamera();
        const MATH::Vec3 position = camera.GetPosition();
        camera.SetPosition({position.x, EDITOR::FindLookDevGroundHeight(position) + 1.6f, position.z});
    }
    const std::vector<SceneCameraEntry> cameras = CollectSceneCameras(scene.GetSceneDocument());
    if (cameras.empty()) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.noCameras").c_str());
        return;
    }
    const SceneObjectId previewing =
        scene.IsEditorCameraPreviewActive() ? scene.GetEditorCameraPreviewObjectId() : SceneObjectId{};
    const float buttonSize = ImGui::GetFrameHeight();
    for (size_t index = 0; index < cameras.size(); ++index) {
        const SceneCameraEntry& camera = cameras[index];
        ImGui::PushID(static_cast<int>(camera.id.value));
        const std::string label = (index < 9u ? "[" + std::to_string(index + 1u) + "] " : std::string{}) + camera.name;
        const float listWidth =
            ImGui::GetContentRegionAvail().x - (buttonSize + ImGui::GetStyle().ItemSpacing.x) * 2.0f;
        if (ImGui::Selectable(label.c_str(), context_.selection.IsObjectSelected(camera.id), 0,
                              ImVec2((std::max)(listWidth, 40.0f), 0.0f))) {
            MoveEditorCameraToObject(scene, camera.id);
            SelectViewportObjects(scene, {camera.id}, EditorObjectSelectionMode::Replace);
        }
        ImGui::SameLine();
        const bool lookingThrough = previewing == camera.id;
        if (EDITOR::IconToggleButton(EDITOR::EditorGlyph::Camera, "##lookThrough", lookingThrough,
                                     ImVec2(buttonSize, buttonSize),
                                     TEXT::Localize("ui.lookDev.lookThrough").c_str())) {
            if (lookingThrough) {
                scene.EndEditorCameraPreview();
            } else {
                (void)scene.BeginEditorCameraPreview(camera.id);
            }
        }
        ImGui::SameLine();
        if (EDITOR::IconButton(EDITOR::EditorGlyph::Refresh, "##updateView", EDITOR::EditorButtonTone::Quiet,
                               ImVec2(buttonSize, buttonSize), TEXT::Localize("ui.lookDev.updateView").c_str())) {
            const bool dirtyBefore = context_.sceneDirty || scene.HasUnsavedSceneChanges();
            std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
            const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
            if (scene.SnapCameraObjectToEditorView(camera.id)) {
                context_.sceneDirty = true;
                CommitSceneObjectAuthoringHistory(scene, SceneObjectAuthoringHistoryRequest{
                                                             "Update Camera View",
                                                             std::move(beforeObjects),
                                                             scene.GetSceneDocument().objects,
                                                             beforeCamera,
                                                             scene.GetSceneDocument().camera,
                                                             dirtyBefore,
                                                         });
            }
        }
        ImGui::PopID();
    }
}

void DocumentSceneEditorController::SaveViewAsCamera(DocumentSceneBase& scene) {
    const Camera3D& view = scene.GetCamera();
    const MATH::Vec3 forward = view.GetTarget() - view.GetPosition();
    const size_t count = CollectSceneCameras(scene.GetSceneDocument()).size();
    sceneCreationPanel_.CreateWithHistory(scene, context_, selectionSync_, "Save Camera View", [&] {
        EDITOR::CreateObjectRequest request{};
        request.name = TEXT::Localize("ui.lookDev.cameraName") + " " + std::to_string(count + 1u);
        request.position = view.GetPosition();
        request.rotation = MATH::Length(forward) > 1.0e-5f ? EDITOR::LookDevFacingRotation(forward) : MATH::Vec3{};
        return EDITOR::CreateCameraObject(scene, request, view.GetFovYRad() * kDegreesPerRadian);
    });
}

void DocumentSceneEditorController::MoveEditorCameraToObject(DocumentSceneBase& scene, SceneObjectId cameraObjectId) {
    const GameObject* object = scene.GetWorld().FindObject(cameraObjectId);
    if (object == nullptr) {
        return;
    }
    if (scene.IsEditorCameraPreviewActive()) {
        scene.EndEditorCameraPreview();
    }
    const MATH::Mat4& world = object->GetTransform().GetWorldMatrix();
    MATH::Vec3 forward{world.m[2][0], world.m[2][1], world.m[2][2]};
    forward = MATH::Length(forward) > 1.0e-5f ? MATH::Normalize(forward) : MATH::Vec3{0.0f, 0.0f, 1.0f};
    DebugCameraController3D& camera = scene.GetDebugCamera();
    camera.SetPosition({world.m[3][0], world.m[3][1], world.m[3][2]});
    camera.SetYaw(std::atan2(forward.x, forward.z));
    camera.SetPitch(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
}

void DocumentSceneEditorController::UpdateLookDevViewport(DocumentSceneBase& scene,
                                                          const EDITOR::SceneViewportRect& viewport, bool hovered,
                                                          bool focused, bool gizmoCapture) {
    const ImGuiIO& io = ImGui::GetIO();
    lookDevWorkspace_.HandlePlacementKeys(focused);
    // 単独表示中は、灯が消えたと誤解しないように視口の上に出す（灯が消されたら解除）。
    if (const uint64_t solo = context_.overlays.soloLightObjectId; solo != 0u) {
        const GameObject* light = scene.GetWorld().FindObject(SceneObjectId{solo});
        if (light == nullptr) {
            context_.overlays.soloLightObjectId = 0u;
        } else {
            char text[192]{};
            std::snprintf(text, sizeof(text), TEXT::Localize("ui.lookDev.soloBanner").c_str(),
                          light->GetName().c_str());
            ImDrawList& banner = *ImGui::GetWindowDrawList();
            const ImVec2 size = ImGui::CalcTextSize(text);
            const ImVec2 origin{viewport.x + (viewport.width - size.x) * 0.5f - 8.0f, viewport.y + 10.0f};
            banner.AddRectFilled(origin, {origin.x + size.x + 16.0f, origin.y + size.y + 10.0f},
                                 IM_COL32(64, 44, 8, 225), 4.0f);
            banner.AddText({origin.x + 8.0f, origin.y + 5.0f}, IM_COL32(255, 222, 150, 250), text);
        }
    }
    // 数字キー 1〜9：その番号の機位へ飛ぶ。
    if (focused && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift) {
        const std::vector<SceneCameraEntry> cameras = CollectSceneCameras(scene.GetSceneDocument());
        for (size_t index = 0; index < cameras.size() && index < 9u; ++index) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(index)), false)) {
                MoveEditorCameraToObject(scene, cameras[index].id);
            }
        }
    }
    const std::optional<EDITOR::LookDevPlaceItem> item = lookDevWorkspace_.ArmedItem();
    if (!item) {
        return;
    }
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const std::string hint = TEXT::Localize("ui.lookDev.placingFormat") + PlaceItemName(*item);
    drawList.AddRectFilled({viewport.x + 10.0f, viewport.y + 10.0f},
                           {viewport.x + 22.0f + ImGui::CalcTextSize(hint.c_str()).x, viewport.y + 34.0f},
                           IM_COL32(8, 12, 17, 205), 4.0f);
    drawList.AddText({viewport.x + 16.0f, viewport.y + 14.0f}, IM_COL32(150, 230, 255, 245), hint.c_str());
    if (!hovered) {
        return;
    }
    const std::optional<EDITOR::LookDevSurfaceHit> hit =
        EDITOR::FindLookDevSurface(scene.GetCamera(), (io.MousePos.x - viewport.x) / viewport.width,
                                   (io.MousePos.y - viewport.y) / viewport.height);
    if (!hit) {
        return;
    }
    const EDITOR::LookDevPlacementPose pose =
        EDITOR::ComputeLookDevPlacement(*item, *hit, lookDevWorkspace_.GridStep(), lookDevWorkspace_.PlacementYaw());
    EDITOR::DrawLookDevPlacementPreview(drawList, scene.GetCamera(),
                                        {viewport.x, viewport.y, viewport.width, viewport.height}, pose, *hit);
    if (!gizmoCapture && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt &&
        !EDITOR::QueryEditorViewInputBlockState().pointer) {
        PlaceLookDevItem(scene, *item, pose);
    }
}

void DocumentSceneEditorController::PlaceLookDevItem(DocumentSceneBase& scene, EDITOR::LookDevPlaceItem item,
                                                     const EDITOR::LookDevPlacementPose& pose) {
    sceneCreationPanel_.CreateWithHistory(
        scene, context_, selectionSync_, std::string("Place ") + TEXT::EnumToName(EDITOR::kLookDevPlaceItemNames, item),
        [&]() -> GameObject* {
            EDITOR::CreateObjectRequest request{};
            request.name = PlaceItemName(item);
            request.position = pose.position;
            request.rotation = pose.rotationDegrees;
            if (EDITOR::IsLookDevLight(item)) {
                return EDITOR::CreateLightObject(
                    scene, request,
                    EDITOR::DefaultLightSettings(EDITOR::LookDevLightType(item), LightIntensityUnit::SceneRelative));
            }
            EDITOR::CreatePrimitiveRequest primitive{};
            primitive.object = request;
            primitive.mesh = EDITOR::LookDevShape(item);
            return EDITOR::CreatePrimitiveObject(scene, primitive);
        });
}

EDITOR::EditorOperationResult DocumentSceneEditorController::RunLookDevOperation(const std::string& id,
                                                                                 const nlohmann::json& parameters) {
    EDITOR::EditorOperationResult result = operationRegistry_.Execute(id, parameters);
    if (!result.ok) {
        viewportDropMessage_ = result.message;
    }
    return result;
}

void DocumentSceneEditorController::SelectLookDevOperationResult(DocumentSceneBase& scene,
                                                                 const EDITOR::EditorOperationResult& result) {
    if (!result.ok) {
        return;
    }
    std::vector<SceneObjectId> ids;
    if (const auto created = result.data.find("createdIds"); created != result.data.end()) {
        for (const nlohmann::json& id : *created) {
            ids.push_back(SceneObjectId{id.get<uint64_t>()});
        }
    }
    for (const char* key : {"groupId", "keyId", "objectId"}) {
        if (ids.empty() && result.data.contains(key)) {
            ids.push_back(SceneObjectId{result.data.at(key).get<uint64_t>()});
        }
    }
    if (!ids.empty()) {
        SelectViewportObjects(scene, ids, EditorObjectSelectionMode::Replace);
    }
}

nlohmann::json DocumentSceneEditorController::DescribeLookDevState() const {
    nlohmann::json placement{{"snapGrid", lookDevWorkspace_.GridStep()},
                             {"yawDegrees", lookDevWorkspace_.PlacementYaw()}};
    if (const std::optional<EDITOR::LookDevPlaceItem> item = lookDevWorkspace_.ArmedItem()) {
        placement["armedItem"] = TEXT::EnumToName(EDITOR::kLookDevPlaceItemNames, *item);
    }
    nlohmann::json view{{"clay", context_.viewportDebug.renderView == RenderDebugView::NeutralClay},
                        {"valueView", context_.overlays.valueView},
                        {"soloLightId", context_.overlays.soloLightObjectId}};
    if (context_.overlays.lockedExposureEv100) {
        view["lockedExposureEv100"] = *context_.overlays.lockedExposureEv100;
    }
    nlohmann::json state{{"placement", std::move(placement)}};
    const DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr) {
        state["view"] = std::move(view);
        return state;
    }
    view["exposureEv100"] = scene->GetCurrentExposureEv100();
    state["view"] = std::move(view);
    const SceneDocument& document = scene->GetSceneDocument();
    const ENVIRONMENT::SceneEnvironmentBinding& environment = document.environment;
    state["environment"] = {{"profileId", environment.profileId},
                            {"weatherId", environment.weatherId},
                            {"renderStyleId", environment.renderStyleId}};
    // 局所光の影の名額：使った面数と上限、名額が足りず影を持てなかった灯（場面の物体 ID）。
    const SHADOW::LocalLightShadowStats& shadows = SHADOW::GetLocalLightShadowStats();
    nlohmann::json rejected = nlohmann::json::array();
    for (const SceneObjectData& object : document.objects) {
        const std::string lightId = LIGHTING_AUTHORING::LightId(object.id);
        if (std::find(shadows.budgetRejectedLightIds.begin(), shadows.budgetRejectedLightIds.end(), lightId) !=
            shadows.budgetRejectedLightIds.end()) {
            rejected.push_back(object.id.value);
        }
    }
    state["localShadows"] = {
        {"usedFaces", shadows.usedFaces}, {"budgetFaces", shadows.budgetFaces}, {"budgetRejectedLightIds", rejected}};
    // 機位の書签（数字キー 1〜9 の順）。
    nlohmann::json cameras = nlohmann::json::array();
    for (const SceneCameraEntry& camera : CollectSceneCameras(document)) {
        cameras.push_back({{"objectId", camera.id.value}, {"name", camera.name}});
    }
    state["cameraBookmarks"] = std::move(cameras);
    return state;
}

} // namespace HIKARI
