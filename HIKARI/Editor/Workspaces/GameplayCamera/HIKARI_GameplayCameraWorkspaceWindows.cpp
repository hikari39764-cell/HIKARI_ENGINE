#include "Editor/Widgets/HIKARI_CameraMotionWidgets.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_NumberField.h"
#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::Localize(key).c_str(); }
bool Icon(EditorGlyph glyph, const char* key) {
    return IconButton(glyph, key, EditorButtonTone::Quiet, {28, 28}, L(key));
}
bool TextField(const char* key, std::string& value) {
    char text[512]{};
    std::snprintf(text, sizeof(text), "%s", value.c_str());
    if (!ImGui::InputText(L(key), text, sizeof(text)))
        return false;
    value = text;
    return true;
}
} // namespace
void GameplayCameraWorkspaceController::DrawDockSpace(bool reset) {
    const auto* viewport = ImGui::GetMainViewport();
    const auto id = ImGui::GetID("HIKARI_GameplayCamera_v4");
    if (reset || !ImGui::DockBuilderGetNode(id)) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(id, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);
        ImGuiID center = id, left = 0, right = 0, monitor = 0, bottom = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, &bottom, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, &right, &center);
        // サイドバーを除いた実際の表示領域で、二つの映像が細長くならない配置を選ぶ。
        const auto* viewArea = ImGui::DockBuilderGetNode(center);
        const bool sideBySide = viewArea && viewArea->Size.x > viewArea->Size.y * 2.0f;
        ImGui::DockBuilderSplitNode(center, sideBySide ? ImGuiDir_Right : ImGuiDir_Down, 0.32f, &monitor, &center);
        ImGui::DockBuilderDockWindow("###GameplayCamera/Library", left);
        ImGui::DockBuilderDockWindow("###GameplayCamera/Inspector", right);
        ImGui::DockBuilderDockWindow("###GameplayCamera/Observer", monitor);
        ImGui::DockBuilderDockWindow("###GameplayCamera/Monitor", center);
        ImGui::DockBuilderDockWindow("###GameplayCamera/Playtest", bottom);
        ImGui::DockBuilderFinish(id);
    }
    DrawWorkspaceDockSpace(id, viewport);
}
bool GameplayCameraWorkspaceController::SelectObject(DocumentSceneBase& scene, const char* key, SceneObjectId& id,
                                                     bool camerasOnly) {
    const auto* selected = scene.GetWorld().FindObject(id);
    const char* name = selected ? selected->GetName().c_str() : L("gc.none");
    bool changed = false;
    if (ImGui::BeginCombo(L(key), name)) {
        static ImGuiTextFilter filter;
        if (ImGui::IsWindowAppearing()) {
            filter.Clear();
            ImGui::SetKeyboardFocusHere();
        }
        filter.Draw(L("gc.search"));
        if (ImGui::Selectable(L("gc.none"), !id.value)) {
            id = {};
            changed = true;
        }
        for (const auto& object : scene.GetWorld().GetObjects()) {
            if (!object || (camerasOnly && !object->GetComponent<CameraComponent>()))
                continue;
            const auto label = object->GetName() + "  #" + std::to_string(object->GetDocumentId().value);
            if (!filter.PassFilter(label.c_str()))
                continue;
            ImGui::PushID(std::to_string(object->GetDocumentId().value).c_str());
            if (ImGui::Selectable(label.c_str(), object->GetDocumentId() == id)) {
                id = object->GetDocumentId();
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}
void GameplayCameraWorkspaceController::DrawLibrary(DocumentSceneBase& scene, EditorOperationRegistry& operations) {
    if (ImGui::Begin(TEXT::LocalizeWindow("gc.library", "GameplayCamera/Library").c_str())) {
        DrawCameraList(scene, operations);
        DrawZoneList(scene, operations);
        if (ImGui::CollapsingHeader(L("gc.assets")))
            DrawProfileAssets(scene, operations);
        DrawModeList();
    }
    ImGui::End();
}
void GameplayCameraWorkspaceController::DrawProfileAssets(DocumentSceneBase& scene,
                                                          EditorOperationRegistry& operations) {
    ImGui::BeginDisabled(!document_.IsOpen());
    if (Icon(EditorGlyph::Save, "gc.save"))
        message_ = operations.Execute("gameplay_camera.save", {}).message;
    ImGui::SameLine();
    if (Icon(EditorGlyph::Duplicate, "gc.saveAs"))
        message_ = operations.Execute("gameplay_camera.save", {{"saveAs", true}}).message;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (Icon(EditorGlyph::Revert, "gc.discard"))
        ImGui::OpenPopup(TEXT::LocalizeWindow("gc.discard", "DiscardCameraDraft").c_str());
    if (ImGui::BeginPopupModal(TEXT::LocalizeWindow("gc.discard", "DiscardCameraDraft").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", L("gc.discardQuestion"));
        if (ImGui::Button(L("gc.discard"))) {
            if (document_.Guid().IsValid())
                document_.Open(scene.GetAssetDatabase(), document_.Guid(), message_, true);
            else
                document_ = GameplayCameraEditorDocument{};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("gc.cancel")))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    TextField("gc.search", search_);
    for (const auto* asset : scene.GetAssetDatabase().CollectByType(AssetType::GameplayCamera)) {
        if (!search_.empty() && asset->displayName.find(search_) == std::string::npos)
            continue;
        ImGui::PushID(asset->guid.value.c_str());
        if (ImGui::Selectable(asset->displayName.c_str(), asset->guid == document_.Guid()))
            message_ = operations.Execute("gameplay_camera.open", {{"assetGuid", asset->guid.value}}).message;
        ImGui::PopID();
    }
}
void GameplayCameraWorkspaceController::DrawModeList() {
    if (!document_.IsOpen())
        return;
    ImGui::SeparatorText(L("gc.modes"));
    auto profile = document_.Profile();
    if (TextField("gc.name", profile.name))
        document_.Apply(std::move(profile), message_, ImGui::GetActiveID());
    for (const auto& mode : document_.Profile().modes) {
        ImGui::PushID(mode.id.c_str());
        if (ImGui::Selectable(mode.name.c_str(), mode_ == mode.id)) {
            mode_ = mode.id;
            effectsEditing_ = false;
            selectedZone_ = {};
            document_.SealMerge();
        }
        // 項目の追加と削除で一覧が変わるので、操作したら描き終える。
        bool edited = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem(L("gc.default"))) {
                Edit({{"action", "set_default"}, {"id", mode.id}}, message_);
                edited = true;
            } else if (ImGui::MenuItem(L("gc.duplicate"))) {
                std::string id = mode.id + "_copy";
                for (unsigned suffix = 2; document_.Profile().FindMode(id); ++suffix)
                    id = mode.id + "_" + std::to_string(suffix);
                Edit({{"action", "duplicate_mode"}, {"id", mode.id}, {"newId", id}, {"name", mode.name + " Copy"}},
                     message_);
                edited = true;
            } else if (ImGui::MenuItem(L("gc.remove"), nullptr, false, mode.id != document_.Profile().defaultMode)) {
                const auto id = mode.id;
                Edit({{"action", "remove_mode"}, {"id", id}}, message_);
                mode_ = document_.Profile().defaultMode;
                edited = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        if (edited)
            break;
    }
    DrawEffectLibrary();
    if (document_.IsDirty())
        StatusBadge(L("gc.unsaved"), EditorStatusTone::Warning);
}
void GameplayCameraWorkspaceController::DrawInspector(DocumentSceneBase& scene, EditorOperationRegistry& operations,
                                                      SceneObjectId selectedObject) {
    if (ImGui::Begin(TEXT::LocalizeWindow("gc.inspector", "GameplayCamera/Inspector").c_str())) {
        if (ImGui::RadioButton(L("gc.camera"), !effectsEditing_))
            effectsEditing_ = false;
        ImGui::SameLine();
        if (ImGui::RadioButton(L("gc.effects"), effectsEditing_)) {
            effectsEditing_ = true;
            if (!document_.Profile().FindEffect(effect_) && !document_.Profile().effects.empty())
                effect_ = document_.Profile().effects.front().id;
        }
        if (effectsEditing_) {
            DrawEffectInspector();
            ImGui::End();
            return;
        }
        ImGui::Separator();
        if (selectedZone_.value) {
            DrawZoneInspector(scene, operations);
            ImGui::End();
            return;
        }
        const auto* cameraObject = scene.GetWorld().FindObject(camera_);
        if (cameraObject == nullptr) {
            EmptyState(L("gc.noCameraTitle"), L("gc.noCameraHint"));
            ImGui::End();
            return;
        }
        PanelTitle(cameraObject->GetName().c_str(), L("gc.cameraTitle"));
        if (cameraObject->GetComponent<CameraFollowComponent>() == nullptr) {
            // 普通の機位（固定の镜头）。模板を選ぶと、玩家を追う游戏摄像机になる。
            ImGui::TextWrapped("%s", L("gc.plainCameraHint"));
            if (ActionButton(L("gc.makeGameCamera"), "##makeGameCamera", EditorButtonTone::Primary))
                ImGui::OpenPopup("CameraTemplates");
            DrawTemplateMenu(scene, operations, camera_);
            ImGui::End();
            return;
        }
        {
            bool rebind = SelectObject(scene, "gc.target", target_, false);
            ImGui::SameLine();
            ImGui::BeginDisabled(!scene.GetWorld().FindObject(selectedObject) || selectedObject == camera_);
            if (ImGui::SmallButton(L("gc.followSelected"))) {
                target_ = selectedObject;
                rebind = true;
            }
            ImGui::EndDisabled();
            rebind |= SelectObject(scene, "gc.lookTarget", lookTarget_, false);
            if (!scene.GetWorld().FindObject(target_))
                ImGui::TextWrapped("%s", L("gc.targetRequired"));
            if (rebind && !scene.IsRuntimePlayActive())
                RebindTargets(scene, operations);
        }
        auto profile = document_.Profile();
        auto selected = std::find_if(profile.modes.begin(), profile.modes.end(),
                                     [&](const auto& mode) { return mode.id == mode_; });
        if (document_.IsOpen() && selected != profile.modes.end()) {
            if (profile.modes.size() > 1u)
                ImGui::TextDisabled("%s: %s", L("cm.editingMode"), selected->name.c_str());
            bool changed = DrawMountSettings(scene, *selected);
            if (ImGui::CollapsingHeader(L("gc.effects"))) {
                changed |= DrawCameraMotionStack(selected->effects, preview_.GameplayState().elapsed, {}, true);
                ImGui::SliderFloat(L("cm.previewSpeed"), &previewSpeed_, 0, 20);
            }
            changed |= DrawModeAdvanced(profile, *selected);
            if (changed) {
                selected->follow.Normalize();
                document_.Apply(std::move(profile), message_, ImGui::GetActiveID());
            }
        }
    }
    ImGui::End();
}
bool GameplayCameraWorkspaceController::DrawModeAdvanced(CAMERA::GameplayCameraProfile& profile,
                                                         CAMERA::GameplayCameraMode& mode) {
    if (!BeginAdvancedSection("cameraAdvanced"))
        return false;
    auto& follow = mode.follow;
    const CAMERA::GameplayCameraMode defaults{};
    bool changed = TextField("gc.modeName", mode.name);
    // 游戏代码から呼ぶ時の ID（プログラムの人向け）。
    ImGui::TextDisabled("%s: %s", L("cm.callId"), mode.id.c_str());
    if (IconTextButton(EditorGlyph::Duplicate, L("cm.copyCall"), "copyModeCall")) {
        const auto quoted = nlohmann::json(mode.id).dump(-1, ' ', true);
        ImGui::SetClipboardText(
            ("// Submit while this mode should be active. Stop submitting to release.\nworld.GetCamera().SubmitGameplayMode(cameraHandle, " +
             quoted + ", sourceId);")
                .c_str());
    }
    const auto number = [&](const char* key, float& value, NumberRange range, const float* fallback) {
        changed |= DragNumber(L(key), value, range, fallback);
    };
    ImGui::SeparatorText(L("gc.response"));
    number("gc.blend", mode.blendSeconds, {.minimum = 0.0, .maximum = 5.0, .step = 0.01}, &defaults.blendSeconds);
    number("gc.lookSmooth", follow.lookSmooth, {.minimum = 0.0, .maximum = 40.0, .step = 0.05},
           &defaults.follow.lookSmooth);
    number("gc.lookAhead", follow.lookAheadDistance, {.minimum = 0.0, .maximum = 20.0, .step = 0.01},
           &defaults.follow.lookAheadDistance);
    number("gc.verticalDeadZone", follow.verticalDeadZone, {.minimum = 0.0, .maximum = 10.0, .step = 0.01},
           &defaults.follow.verticalDeadZone);
    changed |= ImGui::Checkbox(L("gc.keepOrbit"), &mode.keepUserOrbit);
    changed |= TextField("gc.activation", mode.activationAction);
    if (!mode.activationAction.empty() &&
        ImGui::BeginCombo(L("gc.activationFrom"),
                          mode.activationFromModes.empty() ? L("cf.anyMode") : L("gc.selectedModes"))) {
        for (const auto& candidate : profile.modes) {
            auto it = std::find(mode.activationFromModes.begin(), mode.activationFromModes.end(), candidate.id);
            bool included = it != mode.activationFromModes.end();
            ImGui::PushID(candidate.id.c_str());
            if (ImGui::Selectable(candidate.name.c_str(), included, ImGuiSelectableFlags_DontClosePopups)) {
                if (included)
                    mode.activationFromModes.erase(it);
                else
                    mode.activationFromModes.push_back(candidate.id);
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    changed |= DragNumber(L("gc.priority"), mode.priority, {.minimum = -1000.0, .maximum = 1000.0, .step = 1.0});
    ImGui::SeparatorText(L("gc.safety"));
    changed |= ImGui::Checkbox(L("gc.collision"), &follow.collisionEnabled);
    changed |= ImGui::Checkbox(L("gc.staticOnly"), &follow.collisionStaticOnly);
    number("gc.radius", follow.collisionRadius, {.minimum = 0.01, .maximum = 2.0, .step = 0.01},
           &defaults.follow.collisionRadius);
    number("gc.padding", follow.collisionPadding, {.minimum = 0.0, .maximum = 1.0, .step = 0.005},
           &defaults.follow.collisionPadding);
    number("gc.minDistance", follow.minimumDistance, {.minimum = 0.01, .maximum = 100.0, .step = 0.05},
           &defaults.follow.minimumDistance);
    number("gc.maxDistance", follow.maximumDistance, {.minimum = 0.01, .maximum = 100.0, .step = 0.05},
           &defaults.follow.maximumDistance);
    number("gc.near", mode.nearClip, {.minimum = 0.001, .maximum = 2.0, .step = 0.001}, &defaults.nearClip);
    ImGui::SeparatorText(L("gc.composition"));
    number("gc.sensitivity", follow.mouseSensitivityDegreesPerPixel, {.minimum = 0.01, .maximum = 1.0, .step = 0.005},
           &defaults.follow.mouseSensitivityDegreesPerPixel);
    changed |= ImGui::Checkbox(L("gc.recenter"), &follow.autoRecenterEnabled);
    changed |= ImGui::Checkbox(L("gc.composition"), &follow.screenCompositionEnabled);
    number("gc.anchorX", follow.screenCompositionPrimaryAnchorX, {.minimum = 0.05, .maximum = 0.49, .step = 0.002},
           &defaults.follow.screenCompositionPrimaryAnchorX);
    number("gc.anchorY", follow.screenCompositionPrimaryAnchorY, {.minimum = 0.05, .maximum = 0.95, .step = 0.002},
           &defaults.follow.screenCompositionPrimaryAnchorY);
    number("gc.softZone", follow.screenCompositionSoftZoneHalfWidth, {.minimum = 0.0, .maximum = 0.45, .step = 0.002},
           &defaults.follow.screenCompositionSoftZoneHalfWidth);
    number("gc.hardZone", follow.screenCompositionHardZoneHalfWidth, {.minimum = 0.02, .maximum = 0.5, .step = 0.002},
           &defaults.follow.screenCompositionHardZoneHalfWidth);
    ImGui::SeparatorText(L("gc.handoff"));
    const CAMERA::GameplayCameraProfile defaultProfile{};
    number("gc.enter", profile.cinematicEnterSeconds, {.minimum = 0.0, .maximum = 3.0, .step = 0.01},
           &defaultProfile.cinematicEnterSeconds);
    number("gc.return", profile.cinematicReturnSeconds, {.minimum = 0.0, .maximum = 3.0, .step = 0.01},
           &defaultProfile.cinematicReturnSeconds);
    EndAdvancedSection();
    return changed;
}
GameplayCameraWorkspaceResult GameplayCameraWorkspaceController::Draw(DocumentSceneBase& scene,
                                                                      EditorWorkspaceHost& host,
                                                                      EditorOperationRegistry& operations,
                                                                      SceneObjectId selectedObject) {
    GameplayCameraWorkspaceResult result;
    if (!document_.Profile().FindMode(mode_))
        mode_ = document_.Profile().defaultMode;
    UpdateZoneSimulation(scene);
    DrawLibrary(scene, operations);
    DrawInspector(scene, operations, selectedObject);
    DrawViews(scene, host, result);
    if (ImGui::Begin(TEXT::LocalizeWindow("gc.playtest", "GameplayCamera/Playtest").c_str())) {
        if (Icon(scene.IsRuntimePlayActive() ? EditorGlyph::Stop : EditorGlyph::Play, "gc.play"))
            result.togglePlay = true;
        ImGui::SameLine();
        ImGui::Checkbox(L("gc.simulate"), &simulate_);
        ImGui::SameLine();
        bool walk = walk_.Active();
        ImGui::BeginDisabled(scene.IsRuntimePlayActive() || !target_.value);
        if (ImGui::Checkbox(L("gc.walk"), &walk))
            SetZoneSimulation(scene, walk);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", L("gc.walkHint"));
        ImGui::SameLine();
        if (Icon(EditorGlyph::Focus, "gc.focus"))
            focused_ = false;
        ImGui::SameLine();
        if (Icon(EditorGlyph::Revert, "gc.reset")) {
            preview_.ResetRuntimeState();
            preview_.GameplayState().Reset();
            previewRevision_ = 0;
        }
        ImGui::SameLine();
        StatusBadge(L(scene.IsRuntimePlayActive() ? "gc.running" : "gc.preview"), EditorStatusTone::Ready);
        if (scene.IsRuntimePlayActive())
            if (const auto* object = scene.GetWorld().FindObject(camera_)) {
                if (const auto* live = object->GetComponent<CameraFollowComponent>()) {
                    ImGui::Text("%s: %s", L("gc.liveMode"), live->GameplayState().mode.name.c_str());
                    for (const auto& active : live->GameplayState().effects) {
                        ImGui::Text("%s  %.2f / %.2f s", active.effect.name.c_str(), active.elapsed,
                                    active.effect.duration);
                    }
                    if (!live->GameplayState().error.empty())
                        ImGui::TextWrapped("%s", live->GameplayState().error.c_str());
                }
            }
        if (const auto* zone = scene.GetWorld().FindObject(activeZone_))
            ImGui::Text("%s%s", L("gc.inZone"), zone->GetName().c_str());
        else if (target_.value)
            ImGui::TextDisabled("%s", L("gc.outsideZones"));
        walk_.DrawControls(scene.GetWorld());
        if (effectsEditing_)
            DrawEffectTimeline(scene);
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
        if (!preview_.GameplayState().error.empty())
            ImGui::TextWrapped("%s", preview_.GameplayState().error.c_str());
        ImGui::TextDisabled("%s", L("gc.controls"));
    }
    ImGui::End();
    interactive_ = ImGui::IsAnyItemActive();
    if (!interactive_)
        document_.SealMerge();
    return result;
}
} // namespace HIKARI::EDITOR
