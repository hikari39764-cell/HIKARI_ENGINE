// 游戏摄像机の「機位と区域」：機位の一覧と模板からの作成、区域（入ると機位が切り替わる箱）、機位の型ごとの常用項目、
// 視口への区域と軌道の表示、区域の切り替えを試す模拟走动。
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Widgets/HIKARI_NumberField.h"
#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Scene/Camera/HIKARI_CameraVolume.h"
#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace HIKARI::EDITOR {
namespace {

constexpr float kRadiansToDegrees = 57.2957795f;

const char* L(const char* key) { return TEXT::Localize(key).c_str(); }

// 機位の模板。名前と説明は gc.template.<id> / gc.template.<id>.hint。
constexpr std::array<const char*, 9> kCameraTemplates{
    "third_person", "shoulder", "first_person", "lock_on", "top_down", "side_scroller", "fixed", "rail", "arena",
};

std::string TemplateText(const char* id, const char* suffix = "") {
    return TEXT::Localize(std::string("gc.template.") + id + suffix);
}

bool HasComponent(const SceneObjectData& object, std::string_view type) {
    return std::any_of(object.components.begin(), object.components.end(),
                       [type](const SceneComponentData& component) { return component.type == type; });
}

std::string ObjectName(const DocumentSceneBase& scene, SceneObjectId id) {
    const GameObject* object = scene.GetWorld().FindObject(id);
    return object != nullptr ? object->GetName() : std::string(L("gc.none"));
}

// 箱の 12 本の辺を視口へ描く。
void DrawWorldBox(ImDrawList& draw, const Camera3D& camera, const EditorViewportRect& rect, const MATH::Mat4& world,
                  const MATH::Vec3& half, ImU32 color, float thickness) {
    std::array<ImVec2, 8> corners{};
    for (int index = 0; index < 8; ++index) {
        const MATH::Vec4 point = world.TransformPoint(
            {index & 1 ? half.x : -half.x, index & 2 ? half.y : -half.y, index & 4 ? half.z : -half.z, 1.0f});
        if (!DIRECTOR_VIEW::ProjectWorldToViewport(camera, {point.x, point.y, point.z}, {rect.x, rect.y},
                                                   {rect.width, rect.height}, corners[static_cast<size_t>(index)])) {
            return;
        }
    }
    constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                   {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& edge : kEdges) {
        draw.AddLine(corners[static_cast<size_t>(edge[0])], corners[static_cast<size_t>(edge[1])], color, thickness);
    }
}

bool DragVector(const char* key, MATH::Vec3& value) {
    ImGui::TextUnformatted(L(key));
    ImGui::SetNextItemWidth(-1.0f);
    return DragNumbers((std::string("##") + key).c_str(), &value.x, 3, {.step = 0.05, .proportional = false});
}

} // namespace

void GameplayCameraWorkspaceController::DrawTemplateMenu(DocumentSceneBase& scene, EditorOperationRegistry& operations,
                                                         SceneObjectId existingCamera) {
    if (!ImGui::BeginPopup("CameraTemplates")) {
        return;
    }
    ImGui::TextDisabled("%s", L("gc.templateTitle"));
    // 玩家が見つからない場面では、先に追う物を選ぶ。
    if (!target_.value && !FindScenePlayer(scene.GetSceneDocument()).value) {
        ImGui::TextWrapped("%s", L("gc.noPlayer"));
        (void)SelectObject(scene, "gc.target", target_, false);
        ImGui::Separator();
    }
    for (const char* id : kCameraTemplates) {
        ImGui::PushID(id);
        if (ImGui::Selectable(TemplateText(id).c_str())) {
            CreateCameraFromTemplate(scene, operations, id, existingCamera);
        }
        ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
        ImGui::TextDisabled("%s", TemplateText(id, ".hint").c_str());
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

void GameplayCameraWorkspaceController::CreateCameraFromTemplate(DocumentSceneBase& scene,
                                                                 EditorOperationRegistry& operations,
                                                                 std::string_view preset,
                                                                 SceneObjectId existingCamera) {
    const SceneObjectId target = target_.value != 0 ? target_ : FindScenePlayer(scene.GetSceneDocument());
    const GameObject* targetObject = scene.GetWorld().FindObject(target);
    if (targetObject == nullptr) {
        message_ = L("gc.noPlayer");
        return;
    }
    // 開いている方案の未保存の変更は、新しい方案を開く前に保存する。
    if (document_.IsOpen() && document_.IsDirty()) {
        const EditorOperationResult saved = operations.Execute("gameplay_camera.save", {});
        if (!saved.ok) {
            message_ = saved.message;
            return;
        }
    }
    EditorOperationResult result =
        operations.Execute("gameplay_camera.new", {{"preset", std::string(preset)}, {"discard", true}});
    if (!result.ok) {
        message_ = result.message;
        return;
    }
    CAMERA::GameplayCameraProfile profile = document_.Profile();
    const std::string presetId(preset);
    profile.name = TemplateText(presetId.c_str()) + " " +
                   std::to_string(scene.GetAssetDatabase().CollectByType(AssetType::GameplayCamera).size() + 1u);
    // 軌道と竞技场は追う物の周りに既定の線と中心を置く（あとで数値か視口で直す）。
    const MATH::Vec3 anchor = CAMERA::WorldPosition(*targetObject);
    for (CAMERA::GameplayCameraMode& mode : profile.modes) {
        mode.railStart = anchor + MATH::Vec3{-6.0f, 3.0f, -5.0f};
        mode.railEnd = anchor + MATH::Vec3{6.0f, 3.0f, -5.0f};
        mode.arenaCenter = anchor + MATH::Vec3{0.0f, 0.0f, 8.0f};
    }
    (void)document_.Apply(std::move(profile), message_);
    result = operations.Execute("gameplay_camera.save", {});
    if (!result.ok) {
        message_ = result.message;
        return;
    }
    // 既定の游戏摄像机がまだ無い時だけ、新しい機位を既定にする（区域用の固定機位などは既定を奪わない）。
    const auto defaultCamera = scene.GetSceneDocument().camera.defaultCameraObjectId;
    const GameObject* current = defaultCamera ? scene.GetWorld().FindObject(*defaultCamera) : nullptr;
    const bool setDefault = current == nullptr || current->GetComponent<CameraFollowComponent>() == nullptr ||
                            existingCamera == *defaultCamera;
    // 新しい機位は観察の視点に置く（固定機位はその位置から見る）。
    const Camera3D& view = observer_.GetViewCamera();
    MATH::Vec3 forward = view.GetTarget() - view.GetPosition();
    forward = MATH::Length(forward) > 1e-5f ? MATH::Normalize(forward) : MATH::Vec3{0, 0, 1};
    const MATH::Vec3 position = view.GetPosition();
    const MATH::Vec3 rotation{-std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * kRadiansToDegrees,
                              std::atan2(forward.x, forward.z) * kRadiansToDegrees, 0.0f};
    result = operations.Execute("gameplay_camera.bind", {{"assetGuid", document_.Guid().value},
                                                         {"cameraId", existingCamera.value},
                                                         {"targetId", target.value},
                                                         {"mode", document_.Profile().defaultMode},
                                                         {"setDefault", setDefault},
                                                         {"name", document_.Profile().name},
                                                         {"position", {position.x, position.y, position.z}},
                                                         {"rotation", {rotation.x, rotation.y, rotation.z}}});
    message_ = result.message;
    if (result.ok) {
        target_ = target;
        selectedZone_ = {};
        SelectCamera(scene, SceneObjectId{result.data.value("cameraId", uint64_t{0})});
    }
}

void GameplayCameraWorkspaceController::RebindTargets(DocumentSceneBase& scene, EditorOperationRegistry& operations) {
    if (scene.GetWorld().FindObject(target_) == nullptr || !document_.IsOpen()) {
        return;
    }
    if (document_.IsDirty() || !document_.Guid().IsValid()) {
        const EditorOperationResult saved = operations.Execute("gameplay_camera.save", {});
        if (!saved.ok) {
            message_ = saved.message;
            return;
        }
    }
    const auto defaultCamera = scene.GetSceneDocument().camera.defaultCameraObjectId;
    message_ = operations
                   .Execute("gameplay_camera.bind", {{"assetGuid", document_.Guid().value},
                                                     {"cameraId", camera_.value},
                                                     {"targetId", target_.value},
                                                     {"lookTargetId", lookTarget_.value},
                                                     {"mode", mode_},
                                                     {"setDefault", defaultCamera && *defaultCamera == camera_}})
                   .message;
    previewBinding_.clear();
}

void GameplayCameraWorkspaceController::DrawCameraList(DocumentSceneBase& scene, EditorOperationRegistry& operations) {
    ImGui::SeparatorText(L("gc.cameras"));
    if (ActionButton(L("gc.newCamera"), "##newCamera", EditorButtonTone::Primary, ImVec2(-1.0f, 0.0f),
                     L("gc.newCameraHint"))) {
        ImGui::OpenPopup("CameraTemplates");
    }
    DrawTemplateMenu(scene, operations, {});
    const auto defaultCamera = scene.GetSceneDocument().camera.defaultCameraObjectId;
    bool any = false;
    for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
        if (!HasComponent(object, "CameraComponent")) {
            continue;
        }
        any = true;
        const bool follows = HasComponent(object, "CameraFollowComponent");
        const bool isDefault = defaultCamera && *defaultCamera == object.id;
        const std::string label = std::string(isDefault ? "* " : "  ") + object.name + "   " +
                                  L(follows ? "gc.tag.game" : "gc.tag.shot") + "##" + std::to_string(object.id.value);
        if (ImGui::Selectable(label.c_str(), camera_ == object.id && !selectedZone_.value)) {
            selectedZone_ = {};
            SelectCamera(scene, object.id);
        }
        if (isDefault && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("gc.defaultCameraHint"));
        }
    }
    if (!any) {
        ImGui::TextDisabled("%s", L("gc.noCameras"));
    }
}

void GameplayCameraWorkspaceController::DrawZoneList(DocumentSceneBase& scene, EditorOperationRegistry& operations) {
    ImGui::SeparatorText(L("gc.zones"));
    const GameObject* target = scene.GetWorld().FindObject(target_);
    ImGui::BeginDisabled(target == nullptr || !camera_.value);
    if (ActionButton(L("gc.newZone"), "##newZone", EditorButtonTone::Neutral, ImVec2(-1.0f, 0.0f),
                     L("gc.newZoneHint"))) {
        const MATH::Vec3 position = CAMERA::WorldPosition(*target);
        size_t count = 0;
        for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
            count += HasComponent(object, "CameraActivationVolumeComponent") ? 1u : 0u;
        }
        const EditorOperationResult result = operations.Execute(
            "camera_zone.edit", {{"action", "create"},
                                 {"name", std::string(L("gc.zoneName")) + " " + std::to_string(count + 1u)},
                                 {"cameraId", camera_.value},
                                 {"subjectId", target_.value},
                                 {"position", {position.x, position.y, position.z}},
                                 {"halfExtents", {4.0f, 3.0f, 4.0f}}});
        message_ = result.message;
        if (result.ok) {
            selectedZone_ = SceneObjectId{result.data.value("objectId", uint64_t{0})};
        }
    }
    ImGui::EndDisabled();
    if (target == nullptr || !camera_.value) {
        ImGui::TextDisabled("%s", L("gc.zoneNeedsCamera"));
    }
    for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
        if (!HasComponent(object, "CameraActivationVolumeComponent")) {
            continue;
        }
        const GameObject* zoneObject = scene.GetWorld().FindObject(object.id);
        const auto* zone = zoneObject ? zoneObject->GetComponent<CameraActivationVolumeComponent>() : nullptr;
        const std::string camera = zone ? ObjectName(scene, zone->GetCameraObjectId()) : std::string{};
        const bool active = activeZone_ == object.id;
        const std::string label = std::string(active ? "> " : "  ") + object.name + "  -> " + camera + "##zone" +
                                  std::to_string(object.id.value);
        if (ImGui::Selectable(label.c_str(), selectedZone_ == object.id)) {
            selectedZone_ = object.id;
        }
    }
}

void GameplayCameraWorkspaceController::DrawZoneInspector(DocumentSceneBase& scene,
                                                          EditorOperationRegistry& operations) {
    const GameObject* object = scene.GetWorld().FindObject(selectedZone_);
    const auto* zone = object ? object->GetComponent<CameraActivationVolumeComponent>() : nullptr;
    if (zone == nullptr) {
        selectedZone_ = {};
        return;
    }
    PanelTitle(object->GetName().c_str(), L("gc.zoneTitle"));
    ImGui::TextWrapped("%s", L("gc.zoneExplain"));
    nlohmann::json patch = nlohmann::json::object();
    SceneObjectId camera = zone->GetCameraObjectId();
    if (SelectObject(scene, "gc.zoneCamera", camera, true)) {
        patch["cameraId"] = camera.value;
    }
    SceneObjectId subject = zone->GetSubjectObjectId();
    if (SelectObject(scene, "gc.zoneSubject", subject, false)) {
        patch["subjectId"] = subject.value;
    }
    MATH::Vec3 size = zone->GetHalfExtents() * 2.0f;
    ImGui::TextUnformatted(L("gc.zoneSize"));
    if (DragNumbers("##zoneSize", &size.x, 3, {.minimum = 0.1, .maximum = 1000.0, .step = 0.05})) {
        const MATH::Vec3 half = size * 0.5f;
        patch["halfExtents"] = {half.x, half.y, half.z};
    }
    MATH::Vec3 position = CAMERA::WorldPosition(*object);
    ImGui::TextUnformatted(L("gc.zoneCenter"));
    if (DragNumbers("##zoneCenter", &position.x, 3, {.step = 0.05, .proportional = false})) {
        patch["position"] = {position.x, position.y, position.z};
    }
    float blend = zone->GetBlendSeconds();
    constexpr float kDefaultBlend = 0.5f;
    ImGui::TextUnformatted(L("gc.zoneBlend"));
    if (DragNumber("##zoneBlend", blend, {.minimum = 0.0, .maximum = 10.0, .step = 0.01}, &kDefaultBlend)) {
        patch["blendSeconds"] = blend;
    }
    int priority = zone->GetPriority();
    ImGui::TextUnformatted(L("gc.zonePriority"));
    if (DragNumber("##zonePriority", priority, {.minimum = -1000.0, .maximum = 1000.0, .step = 1.0})) {
        patch["priority"] = priority;
    }
    bool controlBasis = zone->GetAffectsControlBasis();
    if (ImGui::Checkbox(L("gc.zoneControlBasis"), &controlBasis)) {
        patch["affectsControlBasis"] = controlBasis;
    }
    if (target_.value) {
        if (ActionButton(L("gc.zoneToTarget"), "##zoneToTarget")) {
            if (const GameObject* target = scene.GetWorld().FindObject(target_)) {
                const MATH::Vec3 center = CAMERA::WorldPosition(*target);
                patch["position"] = {center.x, center.y, center.z};
            }
        }
        ImGui::SameLine();
    }
    if (ActionButton(L("gc.zoneRemove"), "##zoneRemove", EditorButtonTone::Danger)) {
        message_ =
            operations.Execute("camera_zone.edit", {{"action", "remove"}, {"objectId", selectedZone_.value}}).message;
        selectedZone_ = {};
        return;
    }
    if (!patch.empty()) {
        patch["action"] = "update";
        patch["objectId"] = selectedZone_.value;
        patch["mergeKey"] = selectedZone_.value;
        const EditorOperationResult result = operations.Execute("camera_zone.edit", patch);
        if (!result.ok) {
            message_ = result.message;
        }
    }
}

bool GameplayCameraWorkspaceController::DrawMountSettings(DocumentSceneBase& scene, CAMERA::GameplayCameraMode& mode) {
    bool changed = false;
    // 機位の型。押すとその型に切り替える（数値は今のまま）。
    constexpr std::array<CAMERA::CameraMount, 5> kMounts{CAMERA::CameraMount::Orbit, CAMERA::CameraMount::FirstPerson,
                                                         CAMERA::CameraMount::Fixed, CAMERA::CameraMount::Rail,
                                                         CAMERA::CameraMount::Arena};
    ImGui::SeparatorText(L("gc.mountTitle"));
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    for (size_t index = 0; index < kMounts.size(); ++index) {
        if (index % 3u != 0u) {
            ImGui::SameLine(0.0f, spacing);
        }
        const std::string key = std::string("gc.mount.") + TEXT::EnumToName(CAMERA::kCameraMountNames, kMounts[index]);
        if (ToggleButton(TEXT::Localize(key).c_str(), key.c_str(), mode.mount == kMounts[index], ImVec2(width, 0.0f),
                         TEXT::Localize(key + ".hint").c_str()) &&
            mode.mount != kMounts[index]) {
            mode.mount = kMounts[index];
            mode.follow.orbitInputEnabled = mode.mount == CAMERA::CameraMount::Orbit && mode.follow.orbitInputEnabled;
            changed = true;
        }
    }
    auto& follow = mode.follow;
    const CAMERA::GameplayCameraMode defaults{};
    const auto number = [&](const char* key, float& value, NumberRange range, const float* fallback) {
        ImGui::TextUnformatted(L(key));
        ImGui::SetNextItemWidth(-1.0f);
        changed |= DragNumber((std::string("##") + key).c_str(), value, range, fallback);
    };
    ImGui::SeparatorText(L("gc.common"));
    switch (mode.mount) {
    case CAMERA::CameraMount::Orbit:
        changed |= ImGui::Checkbox(L("gc.playerRotates"), &follow.orbitInputEnabled);
        number("gc.distance", follow.initialDistance, {.minimum = 0.1, .maximum = 100.0, .step = 0.05},
               &defaults.follow.initialDistance);
        number("gc.pitch", follow.initialPitchDegrees, {.minimum = -85.0, .maximum = 85.0, .step = 0.5},
               &defaults.follow.initialPitchDegrees);
        if (!follow.orbitInputEnabled) {
            number("gc.yaw", follow.initialYawDegrees, {.minimum = -180.0, .maximum = 180.0, .step = 0.5},
                   &defaults.follow.initialYawDegrees);
        }
        number("gc.shoulder", mode.shoulderOffset, {.minimum = -2.0, .maximum = 2.0, .step = 0.01},
               &defaults.shoulderOffset);
        changed |= ImGui::Checkbox(L("gc.lockTarget"), &mode.trackLookTarget);
        break;
    case CAMERA::CameraMount::FirstPerson: break;
    case CAMERA::CameraMount::Fixed: ImGui::TextWrapped("%s", L("gc.fixedHint")); break;
    case CAMERA::CameraMount::Rail: {
        ImGui::TextWrapped("%s", L("gc.railHint"));
        changed |= DragVector("gc.railStart", mode.railStart);
        changed |= DragVector("gc.railEnd", mode.railEnd);
        const Camera3D& view = observer_.GetViewCamera();
        if (ActionButton(L("gc.railStartFromView"), "##railStartView")) {
            mode.railStart = view.GetPosition();
            changed = true;
        }
        ImGui::SameLine();
        if (ActionButton(L("gc.railEndFromView"), "##railEndView")) {
            mode.railEnd = view.GetPosition();
            changed = true;
        }
        break;
    }
    case CAMERA::CameraMount::Arena:
        ImGui::TextWrapped("%s", L("gc.arenaHint"));
        changed |= DragVector("gc.arenaCenter", mode.arenaCenter);
        if (const GameObject* look = scene.GetWorld().FindObject(lookTarget_)) {
            if (ActionButton(L("gc.arenaCenterFromLook"), "##arenaFromLook")) {
                mode.arenaCenter = CAMERA::WorldPosition(*look);
                changed = true;
            }
        }
        number("gc.distance", follow.initialDistance, {.minimum = 0.5, .maximum = 100.0, .step = 0.05},
               &defaults.follow.initialDistance);
        number("gc.arenaHeight", follow.initialPitchDegrees, {.minimum = -10.0, .maximum = 85.0, .step = 0.5},
               &defaults.follow.initialPitchDegrees);
        break;
    }
    number("gc.height", follow.pivotOffset.y, {.minimum = -10.0, .maximum = 30.0, .step = 0.01, .proportional = false},
           &defaults.follow.pivotOffset.y);
    number("gc.fov", mode.verticalFovDegrees, {.minimum = 10.0, .maximum = 140.0, .step = 0.5},
           &defaults.verticalFovDegrees);
    number("gc.followSmooth", follow.followSmooth, {.minimum = 0.0, .maximum = 40.0, .step = 0.05},
           &defaults.follow.followSmooth);
    return changed;
}

void GameplayCameraWorkspaceController::DrawZoneOverlay(DocumentSceneBase& scene, const EditorViewportRect& rect) {
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    const Camera3D& camera = observer_.GetViewCamera();
    draw.PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
    scene.GetWorld().ForEachObjectWith<CameraActivationVolumeComponent>(
        [&](GameObject& object, CameraActivationVolumeComponent& zone) {
            const SceneObjectId id = object.GetDocumentId();
            const bool active = activeZone_ == id;
            const bool selected = selectedZone_ == id;
            const ImU32 color = active     ? IM_COL32(255, 196, 80, 235)
                                : selected ? IM_COL32(120, 230, 255, 235)
                                           : IM_COL32(150, 170, 190, 150);
            DrawWorldBox(draw, camera, rect, object.GetTransform().GetWorldMatrix(), zone.GetHalfExtents(), color,
                         active || selected ? 2.0f : 1.2f);
            ImVec2 label{};
            if (DIRECTOR_VIEW::ProjectWorldToViewport(camera, CAMERA::WorldPosition(object), {rect.x, rect.y},
                                                      {rect.width, rect.height}, label)) {
                draw.AddText(label, color, object.GetName().c_str());
            }
        });
    // 選んだ機位の軌道の線と竞技场の中心。
    if (const CAMERA::GameplayCameraMode* mode = document_.Profile().FindMode(mode_)) {
        const auto project = [&](const MATH::Vec3& point, ImVec2& screen) {
            return DIRECTOR_VIEW::ProjectWorldToViewport(camera, point, {rect.x, rect.y}, {rect.width, rect.height},
                                                         screen);
        };
        ImVec2 a{}, b{};
        if (mode->mount == CAMERA::CameraMount::Rail && project(mode->railStart, a) && project(mode->railEnd, b)) {
            draw.AddLine(a, b, IM_COL32(120, 255, 170, 230), 2.5f);
            draw.AddCircleFilled(a, 4.0f, IM_COL32(120, 255, 170, 230));
            draw.AddCircleFilled(b, 4.0f, IM_COL32(120, 255, 170, 230));
        } else if (mode->mount == CAMERA::CameraMount::Arena && project(mode->arenaCenter, a)) {
            draw.AddCircle(a, 10.0f, IM_COL32(255, 140, 120, 230), 24, 2.0f);
            draw.AddLine({a.x - 6, a.y}, {a.x + 6, a.y}, IM_COL32(255, 140, 120, 230));
            draw.AddLine({a.x, a.y - 6}, {a.x, a.y + 6}, IM_COL32(255, 140, 120, 230));
        }
    }
    draw.PopClipRect();
}

void GameplayCameraWorkspaceController::UpdateZoneSimulation(DocumentSceneBase& scene) {
    activeZone_ = {};
    const GameObject* target = scene.GetWorld().FindObject(target_);
    if (target == nullptr) {
        return;
    }
    // 追う物の位置で、優先度の最も高い区域を探す（実行時の区域の判定と同じ）。
    const MATH::Vec3 position = CAMERA::WorldPosition(*target);
    int bestPriority = 0;
    SceneObjectId zoneCamera{};
    scene.GetWorld().ForEachObjectWith<CameraActivationVolumeComponent>(
        [&](GameObject& object, CameraActivationVolumeComponent& zone) {
            if (!zone.IsEnabled() || (zone.GetSubjectObjectId().value && zone.GetSubjectObjectId() != target_) ||
                !CAMERA::ContainsCameraSubject(object, zone.GetHalfExtents(), position)) {
                return;
            }
            if (!activeZone_.value || zone.GetPriority() > bestPriority) {
                activeZone_ = object.GetDocumentId();
                bestPriority = zone.GetPriority();
                zoneCamera = zone.GetCameraObjectId();
            }
        });
    if (!simulateZones_ || scene.IsRuntimePlayActive()) {
        return;
    }
    // 模拟走动：区域の機位（区域の外なら既定の機位）を相机画面に出す。
    const auto defaultCamera = scene.GetSceneDocument().camera.defaultCameraObjectId;
    const SceneObjectId wanted = zoneCamera.value ? zoneCamera : defaultCamera.value_or(SceneObjectId{});
    if (wanted.value && wanted != camera_) {
        const SceneObjectId keepTarget = target_;
        SelectCamera(scene, wanted);
        target_ = keepTarget;
    }
}

void GameplayCameraWorkspaceController::SetZoneSimulation(DocumentSceneBase& scene, bool enabled) {
    if (simulateZones_ == enabled) {
        return;
    }
    simulateZones_ = enabled;
    GameObject* target = scene.GetWorld().FindObject(target_);
    if (enabled) {
        // 動かす前の位置を覚え、終わったら戻す（模拟は文書を変えない）。
        simulatedTargetOrigin_ = target ? std::optional<Transform3D>(target->GetTransform()) : std::nullopt;
        simulatedTarget_ = target_;
        return;
    }
    if (GameObject* moved = scene.GetWorld().FindObject(simulatedTarget_); moved && simulatedTargetOrigin_) {
        (void)moved->SetLocalTransform(*simulatedTargetOrigin_);
    }
    simulatedTargetOrigin_.reset();
    simulatedTarget_ = {};
}

} // namespace HIKARI::EDITOR
