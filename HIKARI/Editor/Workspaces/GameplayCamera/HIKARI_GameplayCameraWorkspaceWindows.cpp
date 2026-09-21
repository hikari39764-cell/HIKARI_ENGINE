#include "Editor/Widgets/HIKARI_CameraMotionWidgets.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
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
const char *L(const char *key) { return TEXT::LocalizeCStr(key); }
bool Icon(EditorGlyph glyph, const char *key) {
  return IconButton(glyph, key, EditorButtonTone::Quiet, {28, 28}, L(key));
}
bool TextField(const char *key, std::string &value) {
  char text[512]{};
  std::snprintf(text, sizeof(text), "%s", value.c_str());
  if (!ImGui::InputText(L(key), text, sizeof(text)))
    return false;
  value = text;
  return true;
}
} // namespace
void GameplayCameraWorkspaceController::DrawDockSpace(bool reset) {
  const auto *viewport = ImGui::GetMainViewport();
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
    const auto *viewArea = ImGui::DockBuilderGetNode(center);
    const bool sideBySide =
        viewArea && viewArea->Size.x > viewArea->Size.y * 2.0f;
    ImGui::DockBuilderSplitNode(center,
                                sideBySide ? ImGuiDir_Right : ImGuiDir_Down,
                                0.32f, &monitor, &center);
    ImGui::DockBuilderDockWindow("###GameplayCamera/Library", left);
    ImGui::DockBuilderDockWindow("###GameplayCamera/Inspector", right);
    ImGui::DockBuilderDockWindow("###GameplayCamera/Observer", monitor);
    ImGui::DockBuilderDockWindow("###GameplayCamera/Monitor", center);
    ImGui::DockBuilderDockWindow("###GameplayCamera/Playtest", bottom);
    ImGui::DockBuilderFinish(id);
  }
  DrawWorkspaceDockSpace(id, viewport);
}
bool GameplayCameraWorkspaceController::SelectObject(DocumentSceneBase &scene,
                                                     const char *key,
                                                     SceneObjectId &id,
                                                     bool camerasOnly) {
  const auto *selected = scene.GetWorld().FindObject(id);
  const char *name = selected ? selected->GetName().c_str() : L("gc.none");
  bool changed = false;
  if (ImGui::BeginCombo(L(key), name)) {
    static ImGuiTextFilter filter;
    if (ImGui::IsWindowAppearing()) {filter.Clear();ImGui::SetKeyboardFocusHere();}
    filter.Draw(L("gc.search"));
    if (ImGui::Selectable(L("gc.none"), !id.value)) {
      id = {};
      changed = true;
    }
    for (const auto &object : scene.GetWorld().GetObjects()) {
      if (!object || (camerasOnly && !object->GetComponent<CameraComponent>()))
        continue;
      const auto label=object->GetName()+"  #"+std::to_string(object->GetDocumentId().value);
      if (!filter.PassFilter(label.c_str())) continue;
      ImGui::PushID(std::to_string(object->GetDocumentId().value).c_str());
      if (ImGui::Selectable(label.c_str(),
                            object->GetDocumentId() == id)) {
        id = object->GetDocumentId();
        changed = true;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  return changed;
}
void GameplayCameraWorkspaceController::DrawLibrary(
    DocumentSceneBase &scene, EditorOperationRegistry &operations) {
  if (ImGui::Begin(TEXT::LocalizeWindow("gc.library", "GameplayCamera/Library")
                       .c_str())) {
    auto selectedCamera=camera_;
    if(SelectObject(scene,"gc.camera",selectedCamera,true)) SelectCamera(scene,selectedCamera);
    ImGui::Separator();
    if (Icon(EditorGlyph::Add, "gc.new"))
      ImGui::OpenPopup("CameraPresets");
    if (ImGui::BeginPopup("CameraPresets")) {
      for (const char *preset : {"third_person", "first_person", "top_down",
                                 "side_scroller", "fixed"}) {
        const auto key = std::string("gc.preset.") + preset;
        if (ImGui::MenuItem(L(key.c_str())))
          message_ =
              operations.Execute("gameplay_camera.new", {{"preset", preset}})
                  .message;
      }
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_.IsOpen());
    if (Icon(EditorGlyph::Save, "gc.save"))
      message_ = operations.Execute("gameplay_camera.save", {}).message;
    ImGui::SameLine();
    if (Icon(EditorGlyph::Duplicate, "gc.saveAs"))
      message_ = operations.Execute("gameplay_camera.save", {{"saveAs", true}})
                     .message;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (Icon(EditorGlyph::Revert, "gc.discard"))
      ImGui::OpenPopup(
          TEXT::LocalizeWindow("gc.discard", "DiscardCameraDraft").c_str());
    if (ImGui::BeginPopupModal(
            TEXT::LocalizeWindow("gc.discard", "DiscardCameraDraft").c_str(),
            nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", L("gc.discardQuestion"));
      if (ImGui::Button(L("gc.discard"))) {
        if (document_.Guid().IsValid())
          document_.Open(scene.GetAssetDatabase(), document_.Guid(), message_,
                         true);
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
    if (ImGui::CollapsingHeader(L("gc.assets"))) {
      for (const auto *asset :
           scene.GetAssetDatabase().CollectByType(AssetType::GameplayCamera)) {
        if (!search_.empty() &&
            asset->displayName.find(search_) == std::string::npos)
          continue;
        ImGui::PushID(asset->guid.value.c_str());
        if (ImGui::Selectable(asset->displayName.c_str(),
                              asset->guid == document_.Guid()))
          message_ = operations
                         .Execute("gameplay_camera.open",
                                  {{"assetGuid", asset->guid.value}})
                         .message;
        ImGui::PopID();
      }
    }
    if(ImGui::CollapsingHeader(L("cf.flow"))) DrawFlowLibrary(scene);
    if (document_.IsOpen()) {
      ImGui::SeparatorText(L("gc.modes"));
      auto profile = document_.Profile();
      if (TextField("gc.name", profile.name))
        document_.Apply(std::move(profile), message_, ImGui::GetActiveID());
      for (const auto &mode : document_.Profile().modes) {
        if (!search_.empty() && mode.name.find(search_) == std::string::npos &&
            mode.id.find(search_) == std::string::npos)
          continue;
        ImGui::PushID(mode.id.c_str());
        if (ImGui::Selectable(mode.name.c_str(), mode_ == mode.id)) {
          mode_ = mode.id;
          flowEditing_ = false; effectsEditing_ = false;
          document_.SealMerge();
        }
        if (ImGui::BeginPopupContextItem()) {
          if (ImGui::MenuItem(L("gc.default"))) {
            Edit({{"action", "set_default"}, {"id", mode.id}}, message_);
            ImGui::EndPopup();
            ImGui::PopID();
            break;
          }
          if (ImGui::MenuItem(L("gc.duplicate"))) {
            std::string id = mode.id + "_copy";
            for (unsigned suffix = 2; document_.Profile().FindMode(id);
                 ++suffix)
              id = mode.id + "_" + std::to_string(suffix);
            Edit({{"action", "duplicate_mode"},
                  {"id", mode.id},
                  {"newId", id},
                  {"name", mode.name + " Copy"}},
                 message_);
            ImGui::EndPopup();
            ImGui::PopID();
            break;
          }
          if (ImGui::MenuItem(L("gc.remove"), nullptr, false,
                              mode.id != document_.Profile().defaultMode)) {
            const auto id = mode.id;
            Edit({{"action", "remove_mode"}, {"id", id}}, message_);
            mode_ = document_.Profile().defaultMode;
            ImGui::EndPopup();
            ImGui::PopID();
            break;
          }
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
      DrawEffectLibrary();
      if (document_.IsDirty())
        StatusBadge(L("gc.unsaved"), EditorStatusTone::Warning);
    } else
      ImGui::TextWrapped("%s", L("gc.start"));
  }
  ImGui::End();
}
void GameplayCameraWorkspaceController::DrawInspector(
    DocumentSceneBase &scene, EditorOperationRegistry &operations,
    EditorWorkspaceHost &host) {
  if (ImGui::Begin(
          TEXT::LocalizeWindow("gc.inspector", "GameplayCamera/Inspector")
              .c_str())) {
    if (ImGui::RadioButton(L("gc.camera"), !flowEditing_ && !effectsEditing_)) {
      flowEditing_ = false; effectsEditing_=false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(L("gc.effects"), effectsEditing_)) {
      effectsEditing_=true;flowEditing_=false;
      if(!document_.Profile().FindEffect(effect_) && !document_.Profile().effects.empty()) effect_=document_.Profile().effects.front().id;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(L("cf.flow"), flowEditing_)) {flowEditing_ = true;effectsEditing_=false;}
    if(effectsEditing_) {DrawEffectInspector();ImGui::End();return;}
    ImGui::Separator();
    if (flowEditing_) {
      DrawFlowInspector(scene, operations, host);
      ImGui::End();
      return;
    }
    if (ImGui::CollapsingHeader(L("gc.bindings"),
                                ImGuiTreeNodeFlags_DefaultOpen)) {

      SelectObject(scene, "gc.target", target_, false);
      SelectObject(scene, "gc.lookTarget", lookTarget_, false);
      ImGui::BeginDisabled(scene.IsRuntimePlayActive() || !document_.IsOpen() || !target_.value);
      if (ImGui::Button(L("gc.applyBinding"))) {
        bool saved=true;
        if(document_.IsDirty() || !document_.Guid().IsValid()) {
          auto save=operations.Execute("gameplay_camera.save",{});message_=save.message;saved=save.ok;
        }
        auto result = saved ?
            operations.Execute("gameplay_camera.bind",
                               {{"cameraId", camera_.value},
                                {"targetId", target_.value},
                                {"lookTargetId", lookTarget_.value},
                                {"assetGuid", document_.Guid().value},
                                {"mode", document_.Profile().defaultMode}}) : EditorOperationResult{false,"SAVE_FAILED",message_};
        message_ = result.message;
        if (result.ok)
          camera_.value = result.data.value("cameraId", camera_.value);
      }
      ImGui::EndDisabled();
      if (document_.IsDirty() || !document_.Guid().IsValid())
        ImGui::TextWrapped("%s", L("gc.saveBeforeBinding"));
    }
    auto profile = document_.Profile();
    auto selected =
        std::find_if(profile.modes.begin(), profile.modes.end(),
                     [&](const auto &mode) { return mode.id == mode_; });
    if (document_.IsOpen() && selected != profile.modes.end()) {
      auto &mode = *selected;
      auto &follow = mode.follow;
      bool changed = TextField("gc.modeName", mode.name);
      ImGui::TextDisabled("%s: %s",L("cm.callId"),mode.id.c_str());
      if(IconTextButton(EditorGlyph::Duplicate,L("cm.copyCall"),"copyModeCall")) {
        const auto quoted=nlohmann::json(mode.id).dump(-1,' ',true);
        ImGui::SetClipboardText(("// Submit while this mode should be active. Stop submitting to release.\nworld.GetCamera().SubmitGameplayMode(cameraHandle, "+quoted+", sourceId);").c_str());
      }
      ImGui::TextDisabled("%s",L("cm.editingMode"));
      if (ImGui::CollapsingHeader(L("gc.framing"),
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        int mount = static_cast<int>(mode.mount);
        const char *labels[] = {L("gc.orbit"), L("gc.firstPerson"),
                                L("gc.fixed")};
        if (ImGui::Combo(L("gc.mount"), &mount, labels, 3)) {
          mode.mount = static_cast<CAMERA::CameraMount>(mount);
          changed = true;
        }
        if(mode.mount==CAMERA::CameraMount::Orbit)
          changed |= ImGui::DragFloat(L("gc.distance"), &follow.initialDistance,0.02f,0.01f,100);
        changed |= ImGui::DragFloat(L("gc.height"), &follow.pivotOffset.y,
                                    0.01f, -10, 30);
        changed |= ImGui::SliderFloat(L("gc.pitch"),
                                      &follow.initialPitchDegrees, -85, 85);
        changed |= ImGui::SliderFloat(L("gc.yaw"), &follow.initialYawDegrees,
                                      -180, 180);
        changed |=
            ImGui::SliderFloat(L("gc.fov"), &mode.verticalFovDegrees, 10, 140);
        changed |=
            ImGui::SliderFloat(L("gc.shoulder"), &mode.shoulderOffset, -2, 2);
        changed |= ImGui::Checkbox(L("gc.lockTarget"), &mode.trackLookTarget);
      }
      if (ImGui::CollapsingHeader(L("gc.response"))) {
        changed |= ImGui::SliderFloat(L("gc.blend"), &mode.blendSeconds, 0, 3);
        changed |= ImGui::SliderFloat(L("gc.followSmooth"),
                                      &follow.followSmooth, 0, 40);
        changed |=
            ImGui::SliderFloat(L("gc.lookSmooth"), &follow.lookSmooth, 0, 40);
        changed |= ImGui::Checkbox(L("gc.keepOrbit"), &mode.keepUserOrbit);
        changed |= ImGui::Checkbox(L("gc.input"), &follow.orbitInputEnabled);
        changed |= TextField("gc.activation", mode.activationAction);
        if (!mode.activationAction.empty() &&
            ImGui::BeginCombo(L("gc.activationFrom"),
                              mode.activationFromModes.empty()
                                  ? L("cf.anyMode")
                                  : L("gc.selectedModes"))) {
          for (const auto &candidate : profile.modes) {
            auto it = std::find(mode.activationFromModes.begin(),
                                mode.activationFromModes.end(), candidate.id);
            bool included = it != mode.activationFromModes.end();
            ImGui::PushID(candidate.id.c_str());
            if (ImGui::Selectable(candidate.name.c_str(), included,
                                  ImGuiSelectableFlags_DontClosePopups)) {
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
        changed |= ImGui::InputInt(L("gc.priority"), &mode.priority);
      }
      if (ImGui::CollapsingHeader(L("gc.safety"))) {
        changed |= ImGui::Checkbox(L("gc.collision"), &follow.collisionEnabled);
        changed |=
            ImGui::Checkbox(L("gc.staticOnly"), &follow.collisionStaticOnly);
        changed |= ImGui::DragFloat(L("gc.radius"), &follow.collisionRadius,
                                    0.01f, 0.01f, 2);
        changed |= ImGui::DragFloat(L("gc.padding"), &follow.collisionPadding,
                                    0.005f, 0, 1);
        changed |=
            ImGui::DragFloatRange2(L("gc.zoomRange"), &follow.minimumDistance,
                                   &follow.maximumDistance, 0.05f, 0.01f, 100);
        changed |=
            ImGui::DragFloat(L("gc.near"), &mode.nearClip, 0.001f, 0.001f, 2);
      }
      if (ImGui::CollapsingHeader(L("gc.handoff"))) {
        changed |= ImGui::SliderFloat(L("gc.enter"),
                                      &profile.cinematicEnterSeconds, 0, 3);
        changed |= ImGui::SliderFloat(L("gc.return"),
                                      &profile.cinematicReturnSeconds, 0, 3);
      }
      if (ImGui::CollapsingHeader(L("gc.effects"),ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= DrawCameraMotionStack(mode.effects,preview_.GameplayState().elapsed,{},true);
        ImGui::SliderFloat(L("cm.previewSpeed"),&previewSpeed_,0,20);
      }
      if (ImGui::CollapsingHeader(L("gc.advanced"))) {
        changed |= ImGui::SliderFloat(L("gc.sensitivity"),
                                      &follow.mouseSensitivityDegreesPerPixel,
                                      0.01f, 1);
        changed |=
            ImGui::Checkbox(L("gc.recenter"), &follow.autoRecenterEnabled);
        changed |= ImGui::Checkbox(L("gc.composition"),
                                   &follow.screenCompositionEnabled);
        changed |= ImGui::SliderFloat(L("gc.anchorX"),
                                      &follow.screenCompositionPrimaryAnchorX,
                                      0.05f, 0.49f);
        changed |= ImGui::SliderFloat(L("gc.anchorY"),
                                      &follow.screenCompositionPrimaryAnchorY,
                                      0.05f, 0.95f);
        changed |= ImGui::SliderFloat(
            L("gc.softZone"), &follow.screenCompositionSoftZoneHalfWidth, 0,
            0.45f);
        changed |= ImGui::SliderFloat(
            L("gc.hardZone"), &follow.screenCompositionHardZoneHalfWidth, 0.02f,
            0.5f);
      }
      if (changed) {
        follow.Normalize();
        document_.Apply(std::move(profile), message_, ImGui::GetActiveID());
      }
    }
  }
  ImGui::End();
}
GameplayCameraWorkspaceResult
GameplayCameraWorkspaceController::Draw(DocumentSceneBase &scene,
                                        EditorWorkspaceHost &host,
                                        EditorOperationRegistry &operations) {
  GameplayCameraWorkspaceResult result;
  if (!document_.Profile().FindMode(mode_))
    mode_ = document_.Profile().defaultMode;
  DrawLibrary(scene, operations);
  DrawInspector(scene, operations, host);
  DrawViews(scene, host, result);
  if (ImGui::Begin(
          TEXT::LocalizeWindow("gc.playtest", "GameplayCamera/Playtest")
              .c_str())) {
    ImGui::BeginDisabled(flowDirty_ && !scene.IsRuntimePlayActive());
    if (Icon(scene.IsRuntimePlayActive() ? EditorGlyph::Stop
                                         : EditorGlyph::Play,
             "gc.play"))
      result.togglePlay = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox(L("gc.simulate"), &simulate_);
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
    StatusBadge(L(scene.IsRuntimePlayActive() ? "gc.running" : "gc.preview"),
                EditorStatusTone::Ready);
    if (scene.IsRuntimePlayActive())
      if (const auto *object = scene.GetWorld().FindObject(camera_)) {
        if (const auto *live = object->GetComponent<CameraFollowComponent>()) {
          ImGui::Text("%s: %s", L("gc.liveMode"),
                      live->GameplayState().mode.name.c_str());
          for(const auto& active:live->GameplayState().effects) {
            ImGui::Text("%s  %.2f / %.2f s",active.effect.name.c_str(),active.elapsed,active.effect.duration);
          }
          if (!live->GameplayState().error.empty())
            ImGui::TextWrapped("%s", live->GameplayState().error.c_str());
        }
      }
    if(effectsEditing_) DrawEffectTimeline(scene);
    else if(flowEditing_) DrawFlowTimeline(scene,host,operations);
    else ImGui::TextWrapped("%s",L("cm.workflow"));
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
