#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include "imgui.h"
#include <cstdio>
namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::Localize(key).c_str(); }
bool Field(const char* key, std::string& value) {
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (!ImGui::InputText(L(key), buffer, sizeof(buffer)))
        return false;
    value = buffer;
    return true;
}
} // namespace
nlohmann::json GameplayCameraWorkspaceController::DescribeFlows(const DocumentSceneBase& scene) const {
    nlohmann::json flows = nlohmann::json::array();
    for (const auto& object : scene.GetWorld().GetObjects()) {
        if (!object)
            continue;
        const auto* flow = object->GetComponent<CameraFlowComponent>();
        if (!flow)
            continue;
        nlohmann::json settings;
        flow->Serialize(settings);
        const auto& r = flow->Runtime();
        flows.push_back({{"objectId", object->GetDocumentId().value},
                         {"name", object->GetName()},
                         {"settings", settings},
                         {"stage", CameraFlowStageId(r.stage)},
                         {"inside", r.inside},
                         {"runs", r.runs},
                         {"time", r.time},
                         {"duration", r.duration},
                         {"diagnostic", r.diagnostic}});
    }
    CameraFlowComponent defaults;
    nlohmann::json settings;
    defaults.Serialize(settings);
    return {{"flows", flows},
            {"defaults", settings},
            {"playing", scene.IsRuntimePlayActive()},
            {"bindings", {{"Player", "subjectObjectId"}, {"Camera", "cameraObjectId"}, {"Owner", "flow object"}}},
            {"semantics", "Region entry/exit or explicit trigger. Optional "
                          "sequence; empty afterMode keeps current "
                          "mode. requiredMode filters the base mode. Temporary "
                          "aim/input overrides remain available. "
                          "Re-entry retry requires leaving the region. Settings "
                          "belong to scene undo/save."}};
}
void GameplayCameraWorkspaceController::SelectFlow(DocumentSceneBase& scene, SceneObjectId id) {
    if (flowDirty_) {
        if (flowId_ != id)
            message_ = L("cf.applyFirst");
        return;
    }
    flowId_ = id;
    flowEditing_ = true;
    effectsEditing_ = false;
    flowDirty_ = false;
    flowRevision_ = scene.GetSceneDocumentRevision();
    CameraFlowComponent defaults;
    nlohmann::json settings;
    defaults.Serialize(settings);
    flowDraft_ = {{"name", L("cf.newName")}, {"position", {0, 1, 0}}, {"rotation", {0, 0, 0}}, {"settings", settings}};
    for (const auto& object : scene.GetSceneDocument().objects)
        if (object.id == id) {
            for (const auto& c : object.components)
                if (c.type == "CameraFlowComponent")
                    flowDraft_["settings"] = c.properties;
            flowDraft_["name"] = object.name;
            flowDraft_["position"] = SERIALIZATION::JSON::MATH::ToJsonArray(object.transform.position);
            flowDraft_["rotation"] = SERIALIZATION::JSON::MATH::ToJsonArray(object.transform.rotationEulerDeg);
        }
    if (!id.value) {
        auto& props = flowDraft_["settings"];
        props["subjectObjectId"] = SCENE::SERIALIZATION::WriteSceneObjectReference(target_.value);
        props["cameraObjectId"] = SCENE::SERIALIZATION::WriteSceneObjectReference(camera_.value);
    } else {
        CameraFlowComponent flow;
        flow.Deserialize(flowDraft_["settings"]);
        camera_ = flow.Settings().cameraObjectId;
        target_ = flow.Settings().subjectObjectId;
    }
    if (const auto* camera = scene.GetWorld().FindObject(camera_))
        if (const auto* follow = camera->GetComponent<CameraFollowComponent>()) {
            if (!document_.IsDirty() && follow->GameplayBinding().profile != document_.Guid())
                document_.Open(scene.GetAssetDatabase(), follow->GameplayBinding().profile, message_);
            lookTarget_ = follow->GameplayBinding().lookTarget;
        }
}
void GameplayCameraWorkspaceController::DrawFlowLibrary(DocumentSceneBase& scene) {
    ImGui::SeparatorText(L("cf.flows"));
    ImGui::BeginDisabled(scene.IsRuntimePlayActive() || flowDirty_);
    if (IconButton(EditorGlyph::Add, "add_flow", EditorButtonTone::Quiet, {28, 28}, L("cf.create")))
        SelectFlow(scene, {});
    ImGui::EndDisabled();
    for (const auto& object : scene.GetWorld().GetObjects()) {
        if (!object || !object->GetComponent<CameraFlowComponent>())
            continue;
        if (flowDraft_.empty())
            SelectFlow(scene, object->GetDocumentId());
        ImGui::PushID(static_cast<int>(object->GetDocumentId().value));
        if (scene.IsRuntimePlayActive()) {
            const auto& state = object->GetComponent<CameraFlowComponent>()->Runtime();
            const bool active = state.stage == CameraFlowStage::Playing || state.stage == CameraFlowStage::Returning;
            const auto color = active ? IM_COL32(65, 203, 219, 255)
                               : state.stage == CameraFlowStage::Invalid || state.stage == CameraFlowStage::Interrupted
                                   ? IM_COL32(245, 168, 85, 255)
                                   : IM_COL32(118, 139, 158, 180);
            const auto pos = ImGui::GetCursorScreenPos();
            const float height = ImGui::GetTextLineHeight();
            ImGui::InvisibleButton("state", {16, height});
            ImGui::GetWindowDrawList()->AddCircleFilled({pos.x + 6, pos.y + height * .5f}, active ? 5.0f : 3.0f, color);
            if (ImGui::IsItemHovered()) {
                const auto key = std::string("cf.state.") + CameraFlowStageId(state.stage);
                ImGui::SetTooltip("%s", L(key.c_str()));
            }
            ImGui::SameLine();
        }
        if (ImGui::Selectable(object->GetName().c_str(), flowId_ == object->GetDocumentId() && flowEditing_))
            SelectFlow(scene, object->GetDocumentId());
        ImGui::PopID();
    }
    if (flowDirty_)
        StatusBadge(L("cf.draft"), EditorStatusTone::Warning);
}
void GameplayCameraWorkspaceController::DrawFlowInspector(DocumentSceneBase& scene, EditorOperationRegistry& operations,
                                                          EditorWorkspaceHost& host) {
    if (flowDraft_.empty()) {
        ImGui::TextWrapped("%s", L("cf.start"));
        return;
    }
    if (!flowDirty_ && flowRevision_ != scene.GetSceneDocumentRevision())
        SelectFlow(scene, flowId_);
    CameraFlowComponent typed;
    typed.Deserialize(flowDraft_.at("settings"));
    auto settings = typed.Settings();
    ImGui::BeginDisabled(scene.IsRuntimePlayActive());
    std::string name = flowDraft_.value("name", std::string{});
    flowDirty_ |= Field("gc.name", name);
    flowDraft_["name"] = name;
    flowDirty_ |= ImGui::Checkbox(L("cf.enabled"), &settings.enabled);
    flowDirty_ |= SelectObject(scene, "gc.target", settings.subjectObjectId, false);
    flowDirty_ |= SelectObject(scene, "gc.camera", settings.cameraObjectId, true);
    ImGui::SeparatorText(L("cf.region"));
    float position[3], rotation[3],
        size[3] = {settings.halfExtents.x * 2, settings.halfExtents.y * 2, settings.halfExtents.z * 2};
    for (int i = 0; i < 3; ++i) {
        position[i] = flowDraft_["position"][i].get<float>();
        rotation[i] = flowDraft_["rotation"][i].get<float>();
    }
    flowDirty_ |= ImGui::DragFloat3(L("cf.position"), position, .05f);
    flowDirty_ |= ImGui::DragFloat3(L("cf.rotation"), rotation, .5f);
    flowDirty_ |= ImGui::DragFloat3(L("cf.size"), size, .05f, .02f, 20000);
    settings.halfExtents = {size[0] * .5f, size[1] * .5f, size[2] * .5f};
    flowDraft_["position"] = {position[0], position[1], position[2]};
    flowDraft_["rotation"] = {rotation[0], rotation[1], rotation[2]};
    flowDirty_ |= ImGui::Checkbox(L("cf.onExit"), &settings.triggerOnExit);
    ImGui::SeparatorText(L("cf.handoff"));
    const auto* asset = scene.GetAssetDatabase().FindByGuid(settings.sequence);
    if (ImGui::BeginCombo(L("cf.sequence"), asset ? asset->displayName.c_str() : L("cf.direct"))) {
        if (ImGui::Selectable(L("cf.direct"), !settings.sequence.IsValid())) {
            settings.sequence = {};
            flowDirty_ = true;
        }
        for (const auto* candidate : scene.GetAssetDatabase().CollectByType(AssetType::Sequence)) {
            ImGui::PushID(candidate->guid.value.c_str());
            if (ImGui::Selectable(candidate->displayName.c_str(), candidate->guid == settings.sequence)) {
                settings.sequence = candidate->guid;
                flowDirty_ = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    std::string error;
    const auto* camera = scene.GetWorld().FindObject(settings.cameraObjectId);
    const auto* follow = camera ? camera->GetComponent<CameraFollowComponent>() : nullptr;
    // 編集用プロファイルを毎フレーム読み直さず、World
    // のリビジョンキャッシュを利用する。
    const auto* cache = scene.GetWorld().Services().Find<GameplayCameraAssetStore>();
    const auto profile = follow && cache ? cache->Load(follow->GameplayBinding().profile, error) : nullptr;
    auto modePicker = [&](const char* key, std::string& id, const char* emptyKey) {
        const auto* mode = profile ? profile->FindMode(id) : nullptr;
        if (ImGui::BeginCombo(L(key), id.empty() ? L(emptyKey) : mode ? mode->name.c_str() : id.c_str())) {
            if (ImGui::Selectable(L(emptyKey), id.empty())) {
                id.clear();
                flowDirty_ = true;
            }
            if (profile)
                for (const auto& m : profile->modes) {
                    ImGui::PushID(m.id.c_str());
                    if (ImGui::Selectable(m.name.c_str(), m.id == id)) {
                        id = m.id;
                        flowDirty_ = true;
                    }
                    ImGui::PopID();
                }
            ImGui::EndCombo();
        }
    };
    modePicker("cf.after", settings.afterMode, "cf.keepMode");
    if (ImGui::CollapsingHeader(L("cf.policy"))) {
        modePicker("cf.requiredMode", settings.requiredMode, "cf.anyMode");
        flowDirty_ |= ImGui::Checkbox(L("cf.repeat"), &settings.repeatOnReentry);
        flowDirty_ |= ImGui::Checkbox(L("cf.finishInterrupt"), &settings.finishOnInterrupt);
        flowDirty_ |= ImGui::InputInt(L("gc.priority"), &settings.priority);
        ImGui::TextWrapped("%s", L("cf.policyHint"));
    }
    auto& p = flowDraft_["settings"];
    p["enabled"] = settings.enabled;
    p["cameraObjectId"] = SCENE::SERIALIZATION::WriteSceneObjectReference(settings.cameraObjectId.value);
    p["subjectObjectId"] = SCENE::SERIALIZATION::WriteSceneObjectReference(settings.subjectObjectId.value);
    p["halfExtents"] = {settings.halfExtents.x, settings.halfExtents.y, settings.halfExtents.z};
    p["sequenceAssetGuid"] = settings.sequence.value;
    p["afterMode"] = settings.afterMode;
    p["requiredMode"] = settings.requiredMode;
    p["triggerOnExit"] = settings.triggerOnExit;
    p["repeatOnReentry"] = settings.repeatOnReentry;
    p["finishOnInterrupt"] = settings.finishOnInterrupt;
    p["priority"] = settings.priority;
    if (ImGui::Button(L(flowId_.value ? "cf.apply" : "cf.create"))) {
        auto command = flowDraft_;
        command["objectId"] = flowId_.value;
        command["action"] = flowId_.value ? "update" : "create";
        auto result = operations.Execute("camera_flow.edit", command);
        message_ = result.message;
        if (result.ok) {
            flowDirty_ = false;
            SelectFlow(scene, {result.data.at("objectId").get<uint64_t>()});
            message_ = L("cf.applied");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(L("gc.discard"))) {
        flowDirty_ = false;
        SelectFlow(scene, flowId_);
    }
    if (flowId_.value) {
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Delete, "remove_flow", EditorButtonTone::Quiet, {26, 26}, L("cf.remove"))) {
            auto result = operations.Execute("camera_flow.edit", {{"action", "remove"}, {"objectId", flowId_.value}});
            message_ = result.message;
            if (result.ok) {
                flowId_ = {};
                flowDraft_.clear();
                flowDirty_ = false;
            }
        }
    }
    ImGui::EndDisabled();
    if (settings.sequence.IsValid() && ImGui::Button(L("cf.editSequence"))) {
        EditorWorkspaceOpenRequest request;
        request.workspaceId = EditorWorkspaceId::Cinematics;
        request.sequenceAssetGuid = settings.sequence;
        host.RequestOpen(request);
    }
    if (ImGui::Button(L("cf.focusRegion"))) {
        const auto* object = scene.GetWorld().FindObject(flowId_);
        const auto matrix = object ? object->GetTransform().GetWorldMatrix() : MATH::Mat4::Identity();
        const MATH::Vec3 point = object ? MATH::Vec3{matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]}
                                        : MATH::Vec3{position[0], position[1], position[2]};
        observer_.FocusPoint(point, 10);
        focused_ = true;
    }
    ImGui::TextWrapped("%s", L("cf.saveHint"));
}
} // namespace HIKARI::EDITOR
