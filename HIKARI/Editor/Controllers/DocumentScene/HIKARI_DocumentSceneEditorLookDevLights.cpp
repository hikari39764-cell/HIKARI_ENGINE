#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/History/HIKARI_SceneSettingsHistoryCommand.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Editor/Widgets/HIKARI_NumberField.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevDocument.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevLightTools.h"
#include "Render3D/Shadow/HIKARI_LocalLightShadowRenderer.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Environment/HIKARI_CelestialModel.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

#include "imgui.h"

namespace HIKARI {

namespace {

constexpr uint64_t kLookDevMainLightMergeGroup = 0x4C4F4F4B53554Eull;
constexpr float kDegreesToRadians = 0.0174532925f;
constexpr float kHandleRadius = 5.5f;
constexpr float kHandlePickRadius = 9.0f;

struct LightRow {
    SceneObjectId id{};
    std::string name;
    LightSettings settings{};
};

std::vector<LightRow> CollectLights(const SceneDocument& document) {
    std::vector<LightRow> lights;
    for (const SceneObjectData& object : document.objects) {
        for (const SceneComponentData& component : object.components) {
            if (component.type == "LightComponent") {
                lights.push_back({object.id, object.name, ReadLightSettings(component.properties)});
                break;
            }
        }
    }
    return lights;
}

void DrawHandleDot(ImDrawList& drawList, const MATH::Vec2& screen, bool hot) {
    drawList.AddCircleFilled({screen.x, screen.y}, hot ? kHandleRadius + 1.5f : kHandleRadius,
                             hot ? IM_COL32(255, 244, 200, 255) : IM_COL32(255, 205, 96, 235));
    drawList.AddCircle({screen.x, screen.y}, hot ? kHandleRadius + 1.5f : kHandleRadius, IM_COL32(30, 22, 8, 230), 0,
                       1.5f);
}

} // namespace

void DocumentSceneEditorController::ContinueLookDevObjectDrag(DocumentSceneBase& scene, SceneObjectId objectId,
                                                              std::string_view label,
                                                              const std::function<bool(SceneObjectData&)>& edit) {
    if (lookDevDrag_ && lookDevDrag_->objectId != objectId) {
        FinishLookDevObjectDrag(scene);
    }
    SceneDocument& document = scene.GetSceneDocument();
    SceneObjectData* object = FindSceneObjectData(document, objectId);
    if (object == nullptr) {
        return;
    }
    if (!lookDevDrag_) {
        lookDevDrag_ = LookDevObjectDrag{objectId, std::string(label), document.objects, document.camera,
                                         context_.sceneDirty || scene.HasUnsavedSceneChanges()};
    }
    if (!edit(*object)) {
        return;
    }
    lookDevDrag_->changed = true;
    EDITOR::SyncLookDevRuntimeObject(scene, *object);
}

void DocumentSceneEditorController::FinishLookDevObjectDrag(DocumentSceneBase& scene) {
    if (!lookDevDrag_) {
        return;
    }
    LookDevObjectDrag drag = std::move(*lookDevDrag_);
    lookDevDrag_.reset();
    if (!drag.changed) {
        return;
    }
    CommitSceneObjectAuthoringHistory(scene, SceneObjectAuthoringHistoryRequest{
                                                 std::move(drag.label),
                                                 std::move(drag.beforeObjects),
                                                 scene.GetSceneDocument().objects,
                                                 drag.beforeCamera,
                                                 scene.GetSceneDocument().camera,
                                                 drag.dirtyBefore,
                                             });
}

void DocumentSceneEditorController::EditLookDevLight(DocumentSceneBase& scene, SceneObjectId objectId,
                                                     const LightSettings& settings) {
    ContinueLookDevObjectDrag(scene, objectId, "Edit Light", [&](SceneObjectData& object) {
        SceneComponentData* light = object.FindComponent("LightComponent");
        if (light == nullptr) {
            return false;
        }
        light->properties = WriteLightSettings(NormalizeLightSettings(settings));
        return true;
    });
}

void DocumentSceneEditorController::DrawLookDevLightsWindow(DocumentSceneBase& scene) {
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.lookDev.lightsWindow", "LookDev/Lights").c_str())) {
        ImGui::End();
        return;
    }
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.lightFromView").c_str(), "##lightFromView",
                             EDITOR::EditorButtonTone::Neutral, ImVec2(half, 0.0f),
                             TEXT::Localize("ui.lookDev.lightFromViewHint").c_str())) {
        PlaceLightAtView(scene);
    }
    ImGui::SameLine();
    const SceneObjectId subject = context_.selection.GetActiveObjectId();
    const bool hasSubject = subject.value != 0u;
    ImGui::BeginDisabled(!hasSubject);
    if (EDITOR::ActionButton(
            TEXT::Localize("ui.lookDev.threePoint").c_str(), "##threePoint", EDITOR::EditorButtonTone::Neutral,
            ImVec2(half, 0.0f),
            TEXT::Localize(hasSubject ? "ui.lookDev.threePointHint" : "ui.lookDev.threePointNeedsSelection").c_str())) {
        SelectLookDevOperationResult(scene, RunLookDevOperation("lighting.three_point", {{"objectId", subject.value}}));
    }
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.mainLightDragHint").c_str());

    const SHADOW::LocalLightShadowStats& shadows = SHADOW::GetLocalLightShadowStats();
    if (shadows.budgetFaces == 0u) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.shadowBudgetOff").c_str());
    } else {
        char text[160]{};
        std::snprintf(text, sizeof(text), TEXT::Localize("ui.lookDev.shadowBudgetFormat").c_str(), shadows.usedFaces,
                      shadows.budgetFaces);
        ImGui::TextDisabled("%s", text);
        if (!shadows.budgetRejectedLightIds.empty()) {
            std::snprintf(text, sizeof(text), TEXT::Localize("ui.lookDev.shadowRejectedFormat").c_str(),
                          static_cast<unsigned>(shadows.budgetRejectedLightIds.size()));
            EDITOR::StatusText(text, EDITOR::EditorStatusTone::Warning);
        }
    }
    ImGui::PopTextWrapPos();

    const std::vector<LightRow> lights = CollectLights(scene.GetSceneDocument());
    if (lights.empty()) {
        ImGui::TextWrapped("%s", TEXT::Localize("ui.lookDev.noLights").c_str());
        ImGui::End();
        return;
    }
    const auto contains = [](const std::vector<std::string>& ids, const std::string& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    const float iconSize = ImGui::GetFrameHeight();
    if (ImGui::BeginTable("##lookDevLights", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, iconSize);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("solo", ImGuiTableColumnFlags_WidthFixed, iconSize);
        ImGui::TableSetupColumn("brightness", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn("color", ImGuiTableColumnFlags_WidthFixed, iconSize);
        ImGui::TableSetupColumn("shadow", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        for (const LightRow& row : lights) {
            ImGui::PushID(static_cast<int>(row.id.value));
            ImGui::TableNextRow();
            LightSettings settings = row.settings;
            bool changed = false;
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##on", &settings.enabled);
            ImGui::SetItemTooltip("%s", TEXT::Localize("ui.lookDev.lightOn").c_str());
            ImGui::TableNextColumn();
            if (ImGui::Selectable(row.name.c_str(), context_.selection.IsObjectSelected(row.id),
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                SelectViewportObjects(scene, {row.id}, EditorObjectSelectionMode::Replace);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    FocusSceneObjects(scene, {row.id});
                }
            }
            ImGui::TableNextColumn();
            const bool solo = context_.overlays.soloLightObjectId == row.id.value;
            if (EDITOR::IconToggleButton(EDITOR::EditorGlyph::Light, "##solo", solo, ImVec2(iconSize, iconSize),
                                         TEXT::Localize("ui.lookDev.solo").c_str())) {
                RunLookDevOperation("view.light_solo", {{"objectId", solo ? 0u : row.id.value}});
            }
            ImGui::TableNextColumn();
            float brightness = GetLightBrightness(settings);
            const bool relative = settings.intensityUnit == LightIntensityUnit::SceneRelative;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("lm").x -
                                    ImGui::GetStyle().ItemSpacing.x);
            if (EDITOR::DragNumber(
                    "##brightness", brightness,
                    {.minimum = 0.0, .maximum = relative ? kMaximumSceneRelativeIntensity : kMaximumLightLumens})) {
                SetLightBrightness(settings, brightness);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", relative ? "x" : "lm");
            ImGui::TableNextColumn();
            changed |= ImGui::ColorEdit3("##color", &settings.color.x,
                                         ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::TableNextColumn();
            const std::string id = LIGHTING_AUTHORING::LightId(row.id);
            if (!settings.castsShadows) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.shadow.off").c_str());
            } else if (contains(shadows.shadowedLightIds, id)) {
                EDITOR::StatusText(TEXT::Localize("ui.lookDev.shadow.on").c_str(), EDITOR::EditorStatusTone::Ready);
            } else if (contains(shadows.budgetRejectedLightIds, id)) {
                EDITOR::StatusText(TEXT::Localize("ui.lookDev.shadow.over").c_str(), EDITOR::EditorStatusTone::Warning);
            } else {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.shadow.offscreen").c_str());
            }
            if (changed) {
                EditLookDevLight(scene, row.id, settings);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DocumentSceneEditorController::PlaceLightAtView(DocumentSceneBase& scene) {
    const Camera3D& view = scene.GetCamera();
    const MATH::Vec3 forward = view.GetTarget() - view.GetPosition();
    if (MATH::Length(forward) <= 1.0e-5f) {
        return;
    }
    // 画面の中心に見えている面まで届き、そこを環境の照度と同じ明るさで照らす。
    float distance = 6.0f;
    if (const auto hit = EDITOR::FindLookDevSurface(view, 0.5f, 0.5f)) {
        distance = std::clamp(MATH::Length(hit->position - view.GetPosition()), 1.0f, 60.0f);
    }
    const MATH::Vec3 position = view.GetPosition();
    const MATH::Vec3 aim = position + MATH::Normalize(forward) * distance;
    SelectLookDevOperationResult(
        scene, RunLookDevOperation(
                   "scene.create_light",
                   {{"type", "Spot"},
                    {"name", TEXT::Localize("ui.lookDev.viewLightName")},
                    {"position", {position.x, position.y, position.z}},
                    {"aimAt", {aim.x, aim.y, aim.z}},
                    {"settings", {{"range", distance * 1.5f}, {"sceneRelativeIntensity", distance * distance}}}}));
}

EDITOR::EditorOperationResult DocumentSceneEditorController::CreateLookDevLight(const nlohmann::json& p) {
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr || scene->IsRuntimePlayActive()) {
        return EDITOR::OperationFailure("EDIT_MODE_REQUIRED", "Create lights outside Play mode.");
    }
    const std::optional<RENDER3D::LIGHTING::SceneLightType> type =
        TEXT::TryParseEnum(RENDER3D::LIGHTING::kSceneLightTypeNames, p.at("type").get<std::string>());
    if (!type) {
        return EDITOR::OperationFailure("INVALID_TYPE", "type is Point, Spot, Tube or Rect.");
    }
    // 既定は場面に対する相対の明るさ（環境の照度が変わっても釣り合う）。settings は LightSettings の部分 JSON。
    nlohmann::json properties =
        WriteLightSettings(EDITOR::DefaultLightSettings(*type, LightIntensityUnit::SceneRelative));
    if (p.contains("settings")) {
        const nlohmann::json& patch = p.at("settings");
        if (!patch.is_object()) {
            return EDITOR::OperationFailure("INVALID_SETTINGS", "settings must be an object of LightSettings fields.");
        }
        if (const std::string unknown = EDITOR::FindUnknownPatchKey(patch, properties); !unknown.empty()) {
            return EDITOR::OperationFailure("UNKNOWN_SETTING", "Unknown light setting: " + unknown);
        }
        properties.merge_patch(patch);
    }
    LightSettings settings = ReadLightSettings(properties);
    settings.type = *type;
    settings = NormalizeLightSettings(settings);
    const MATH::Vec3 position = JSONREAD::Vec3MemberOr(p, "position", {});
    // 向き：aimAt（世界の点）か rotation（Euler 度）。どちらも無ければ真下を照らす。
    MATH::Vec3 rotation = EDITOR::LookDevFacingRotation({0.0f, -1.0f, 0.0f});
    if (p.contains("aimAt")) {
        const MATH::Vec3 direction = JSONREAD::Vec3MemberOr(p, "aimAt", position) - position;
        if (!(MATH::Length(direction) > 1.0e-4f)) {
            return EDITOR::OperationFailure("INVALID_AIM", "aimAt must differ from position.");
        }
        rotation = EDITOR::LookDevFacingRotation(direction);
    } else if (p.contains("rotation")) {
        rotation = JSONREAD::Vec3MemberOr(p, "rotation", rotation);
    }
    if (!MATH::IsFinite(position) || !MATH::IsFinite(rotation)) {
        return EDITOR::OperationFailure("INVALID_TRANSFORM", "position, aimAt and rotation must be finite.");
    }
    // 名前の既定は摆放の灯と同じ。
    std::string name = p.value("name", std::string{});
    for (const auto& entry : EDITOR::kLookDevPlaceItemNames) {
        if (name.empty() && EDITOR::IsLookDevLight(entry.value) && EDITOR::LookDevLightType(entry.value) == *type) {
            name = TEXT::Localize(std::string("ui.lookDev.item.") + entry.name);
        }
    }
    const std::vector<SceneObjectId> created = CreateLookDevObjects(*scene, "Create Light", [&] {
        EDITOR::CreateObjectRequest request{};
        request.name = name;
        request.position = position;
        request.rotation = rotation;
        return std::vector<GameObject*>{EDITOR::CreateLightObject(*scene, request, settings)};
    });
    if (created.empty()) {
        return EDITOR::OperationFailure("CREATE_FAILED", "The light could not be created.");
    }
    return EDITOR::OperationOk("Light created.", {{"objectId", created.front().value}});
}

EDITOR::EditorOperationResult DocumentSceneEditorController::CreateThreePointLights(const nlohmann::json& p) {
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr || scene->IsRuntimePlayActive()) {
        return EDITOR::OperationFailure("EDIT_MODE_REQUIRED", "Create lights outside Play mode.");
    }
    const SceneObjectId subject{p.at("objectId").get<uint64_t>()};
    if (FindSceneObjectData(scene->GetSceneDocument(), subject) == nullptr) {
        return EDITOR::OperationFailure("OBJECT_NOT_FOUND", "The subject object does not exist.");
    }
    const Bounds bounds =
        CollectLookDevArrangeItems(*scene, std::span<const SceneObjectId>(&subject, 1u)).front().bounds;
    // 見る方向：viewAzimuth（被写体から見る人への水平の向き、度。0 が +Z、90 が +X）、無ければ編集の視点。
    MATH::Vec3 viewer = scene->GetCamera().GetPosition();
    if (p.contains("viewAzimuth")) {
        const float radians = p.at("viewAzimuth").get<float>() * kDegreesToRadians;
        const MATH::Vec3 center = (bounds.min + bounds.max) * 0.5f;
        viewer = center + MATH::Vec3{std::sin(radians), 0.0f, std::cos(radians)} * 10.0f;
        if (!MATH::IsFinite(viewer)) {
            return EDITOR::OperationFailure("INVALID_AZIMUTH", "viewAzimuth must be finite.");
        }
    }
    const auto rig = EDITOR::BuildThreePointLights(bounds, viewer);
    const std::vector<SceneObjectId> lights = CreateLookDevObjects(*scene, "Three-Point Lighting", [&] {
        std::vector<GameObject*> created;
        for (const EDITOR::LookDevRigLight& light : rig) {
            EDITOR::CreateObjectRequest request{};
            request.name = TEXT::Localize(std::string(light.nameKey));
            request.position = light.position;
            request.rotation = light.rotationDegrees;
            created.push_back(EDITOR::CreateLightObject(*scene, request, light.settings));
        }
        return created;
    });
    if (lights.size() != rig.size()) {
        return EDITOR::OperationFailure("CREATE_FAILED", "The lights could not be created.");
    }
    return EDITOR::OperationOk("Key, fill and rim lights created.",
                               {{"keyId", lights[0].value}, {"fillId", lights[1].value}, {"rimId", lights[2].value}});
}

void DocumentSceneEditorController::DragLookDevMainLight(DocumentSceneBase& scene, float azimuthDegrees,
                                                         float elevationDegrees, bool dragging) {
    const ENVIRONMENT::SceneEnvironmentBinding before = scene.GetSceneDocument().environment;
    ENVIRONMENT::SceneEnvironmentBinding edited = before;
    const EnvironmentPanelResult result =
        environmentPanel_.DragMainLight(edited, scene, azimuthDegrees, elevationDegrees, dragging);
    if (!result.bindingChanged || edited == before) {
        return;
    }
    const bool externalDirtyBefore = IsDirtyOutsideHistory(scene);
    scene.GetSceneDocument().environment = edited;
    CommitAppliedDocumentCommand(
        scene, EDITOR::MakeEnvironmentHistoryCommand("Move Main Light", before, std::move(edited)),
        dragging ? kLookDevMainLightMergeGroup : 0u, externalDirtyBefore, EDITOR::EditorDocumentImpact::Environment);
}

bool DocumentSceneEditorController::UpdateLookDevHandles(DocumentSceneBase& scene,
                                                         const EDITOR::SceneViewportRect& viewport, bool hovered,
                                                         bool focused, bool gizmoCapture) {
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const Camera3D& camera = scene.GetCamera();
    const EDITOR::LookDevView view{viewport.x, viewport.y, viewport.width, viewport.height};

    // A/B 分屏の境目。左は開いた時の画（A）、右は今の画（B）。境目はドラッグで動かす。
    if (lookDevWorkspace_.SplitCompare() && POST::PostSystem::GetEditorSnapshotSrv().ptr != 0) {
        float& split = lookDevWorkspace_.SplitPosition();
        bool& dragging = lookDevWorkspace_.SplitDragging();
        const float x = viewport.x + viewport.width * split;
        const float middle = viewport.y + viewport.height * 0.5f;
        const bool nearDivider = hovered && std::abs(io.MousePos.x - x) < 7.0f;
        drawList.AddLine({x, viewport.y}, {x, viewport.y + viewport.height}, IM_COL32(240, 244, 248, 230),
                         nearDivider || dragging ? 3.0f : 2.0f);
        drawList.AddCircleFilled({x, middle}, 9.0f, IM_COL32(240, 244, 248, 235));
        drawList.AddText({x - 4.0f, middle - 7.0f}, IM_COL32(20, 26, 34, 255), "|");
        const std::string a = TEXT::Localize("ui.lookDev.splitA");
        const std::string b = TEXT::Localize("ui.lookDev.splitB");
        const auto label = [&](float left, const std::string& text) {
            const ImVec2 size = ImGui::CalcTextSize(text.c_str());
            const ImVec2 origin{left, viewport.y + 44.0f};
            drawList.AddRectFilled(origin, {origin.x + size.x + 12.0f, origin.y + size.y + 8.0f},
                                   IM_COL32(8, 12, 17, 200), 4.0f);
            drawList.AddText({origin.x + 6.0f, origin.y + 4.0f}, IM_COL32(240, 244, 248, 245), text.c_str());
        };
        label(viewport.x + 10.0f, a);
        label(x + 10.0f, b);
        // 視点が A を撮った時から動いたら、左右が合わないことを知らせる。
        const MATH::Mat4 current = camera.GetViewProj();
        const MATH::Mat4& captured = lookDevWorkspace_.SplitCamera();
        bool moved = false;
        for (int row = 0; row < 4 && !moved; ++row) {
            for (int column = 0; column < 4 && !moved; ++column) {
                moved = std::abs(current.m[row][column] - captured.m[row][column]) > 1.0e-4f;
            }
        }
        if (moved) {
            const std::string warning = TEXT::Localize("ui.lookDev.splitMoved");
            const ImVec2 size = ImGui::CalcTextSize(warning.c_str());
            const ImVec2 origin{viewport.x + (viewport.width - size.x) * 0.5f - 8.0f,
                                viewport.y + viewport.height - size.y - 24.0f};
            drawList.AddRectFilled(origin, {origin.x + size.x + 16.0f, origin.y + size.y + 10.0f},
                                   IM_COL32(72, 46, 10, 230), 4.0f);
            drawList.AddText({origin.x + 8.0f, origin.y + 5.0f}, IM_COL32(255, 220, 150, 250), warning.c_str());
        }
        if (nearDivider && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dragging = true;
        }
        if (dragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                split = std::clamp((io.MousePos.x - viewport.x) / viewport.width, 0.05f, 0.95f);
            } else {
                dragging = false;
            }
        }
        if (dragging || nearDivider) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            return true;
        }
    }

    // 主光：L を押している間は太陽の向きを示し、そのままドラッグで動かす（選択には使わない）。
    const bool mainLightKey = focused && !io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_L);
    if (mainLightKey || lookDevMainLightDrag_) {
        const SceneEnvironment& environment = scene.GetSceneEnvironment();
        const MATH::Vec3 toLight = environment.frame.sun.toLight;
        MATH::Vec2 marker{};
        if (EDITOR::ProjectLookDevPoint(camera, view, camera.GetPosition() + toLight * 1000.0f, marker) &&
            marker.x >= view.x && marker.y >= view.y && marker.x <= view.x + view.width &&
            marker.y <= view.y + view.height) {
            drawList.AddCircleFilled({marker.x, marker.y}, 9.0f, IM_COL32(255, 228, 140, 240));
            for (int ray = 0; ray < 8; ++ray) {
                const float angle = 0.785398f * static_cast<float>(ray);
                const ImVec2 direction{std::cos(angle), std::sin(angle)};
                drawList.AddLine({marker.x + direction.x * 13.0f, marker.y + direction.y * 13.0f},
                                 {marker.x + direction.x * 19.0f, marker.y + direction.y * 19.0f},
                                 IM_COL32(255, 228, 140, 220), 2.0f);
            }
        }
        float azimuth = 0.0f;
        float elevation = 0.0f;
        ENVIRONMENT::WorldDirectionToHorizon(
            toLight, scene.GetEnvironmentRuntime().Simulation().GetSnapshot().location.northYawDegrees, azimuth,
            elevation);
        char text[160]{};
        std::snprintf(text, sizeof(text), TEXT::Localize("ui.lookDev.mainLightFormat").c_str(), azimuth, elevation);
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 origin{viewport.x + 10.0f, viewport.y + viewport.height - size.y - 18.0f};
        drawList.AddRectFilled(origin, {origin.x + size.x + 12.0f, origin.y + size.y + 8.0f}, IM_COL32(8, 12, 17, 205),
                               4.0f);
        drawList.AddText({origin.x + 6.0f, origin.y + 4.0f}, IM_COL32(255, 228, 160, 245), text);
        if (!lookDevMainLightDrag_ && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            lookDevMainLightDrag_ = true;
        }
        if (lookDevMainLightDrag_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
                    DragLookDevMainLight(scene, io.MouseDelta.x * 0.3f, -io.MouseDelta.y * 0.3f, true);
                }
            } else {
                DragLookDevMainLight(scene, 0.0f, 0.0f, false);
                lookDevMainLightDrag_ = false;
            }
        }
        return true;
    }

    // 選んだ灯の形と調整点。
    const SceneObjectId selected = context_.selection.GetActiveObjectId();
    GameObject* object = context_.selection.GetSelectedObjectCount() == 1u && selected.value != 0u
                             ? scene.GetWorld().FindObject(selected)
                             : nullptr;
    const LightComponent* light = object != nullptr ? object->GetComponent<LightComponent>() : nullptr;
    if (light == nullptr) {
        lookDevLightHandle_ = EDITOR::LookDevLightHandle::None;
        return UpdateLookDevShapeHandles(scene, viewport, hovered, gizmoCapture);
    }
    const MATH::Mat4 world = object->GetTransform().GetWorldMatrix();
    EDITOR::DrawLookDevLightShape(drawList, camera, viewport.x, viewport.y, viewport.width, viewport.height,
                                  light->GetSettings(), world);
    EDITOR::LookDevLightHandle hot = lookDevLightHandle_;
    if (hot == EDITOR::LookDevLightHandle::None && hovered && !gizmoCapture) {
        float nearest = kHandlePickRadius;
        for (const EDITOR::LookDevLightHandlePoint& handle :
             EDITOR::BuildLookDevLightHandles(light->GetSettings(), world, camera)) {
            MATH::Vec2 screen{};
            if (!EDITOR::ProjectLookDevPoint(camera, view, handle.position, screen)) {
                continue;
            }
            const float distance = std::hypot(screen.x - io.MousePos.x, screen.y - io.MousePos.y);
            if (distance < nearest) {
                nearest = distance;
                hot = handle.handle;
            }
        }
    }
    for (const EDITOR::LookDevLightHandlePoint& handle :
         EDITOR::BuildLookDevLightHandles(light->GetSettings(), world, camera)) {
        MATH::Vec2 screen{};
        if (EDITOR::ProjectLookDevPoint(camera, view, handle.position, screen)) {
            DrawHandleDot(drawList, screen, handle.handle == hot);
        }
    }
    if (lookDevLightHandle_ == EDITOR::LookDevLightHandle::None && hot != EDITOR::LookDevLightHandle::None &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        lookDevLightHandle_ = hot;
    }
    if (lookDevLightHandle_ == EDITOR::LookDevLightHandle::None) {
        return hot != EDITOR::LookDevLightHandle::None;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        lookDevLightHandle_ = EDITOR::LookDevLightHandle::None;
        FinishLookDevObjectDrag(scene);
        return true;
    }
    EDITOR::EditorPointerRay ray{};
    LightSettings next{};
    if (EDITOR::BuildEditorPointerRay(camera, (io.MousePos.x - viewport.x) / viewport.width,
                                      (io.MousePos.y - viewport.y) / viewport.height, ray) &&
        EDITOR::ApplyLookDevLightHandle(lookDevLightHandle_, light->GetSettings(), world, ray, camera, next)) {
        EditLookDevLight(scene, selected, next);
    }
    return true;
}

} // namespace HIKARI
