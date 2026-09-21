#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <string>

#include "Animation/StateMachine/HIKARI_AnimationStateMachineRuntimeService.h"
#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

const ANIMATION::AnimationStateMachineRuntimeSnapshot *
AnimationStateMachineWorkspaceController::ResolveRuntimeDebugSnapshot(
    DocumentSceneBase &scene) {
  if (!document_.GetAssetGuid().IsValid())
    return nullptr;
  auto *service = scene.GetWorld()
                      .Services()
                      .Find<ANIMATION::AnimationStateMachineRuntimeService>();
  if (service == nullptr)
    return nullptr;

  const auto findForObject = [this, service](GameObject *object)
      -> const ANIMATION::AnimationStateMachineRuntimeSnapshot * {
    if (object == nullptr)
      return nullptr;
    const auto *component =
        object->GetComponent<AnimationStateMachineComponent>();
    if (component == nullptr ||
        component->GetAssetGuid() != document_.GetAssetGuid().value) {
      return nullptr;
    }
    const auto *instance = service->Find(object->GetRuntimeHandle());
    return instance != nullptr ? &instance->GetSnapshot() : nullptr;
  };

  if (const auto *selected =
          findForObject(scene.GetWorld().FindObject(runtimeDebugObject_))) {
    return selected;
  }
  for (const auto &object : scene.GetWorld().GetObjects()) {
    if (const auto *snapshot = findForObject(object.get())) {
      runtimeDebugObject_ = object->GetRuntimeHandle();
      return snapshot;
    }
  }
  runtimeDebugObject_ = {};
  return nullptr;
}

void AnimationStateMachineWorkspaceController::DrawRuntimeDebug(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  ImGui::SeparatorText(TEXT::Localize("editor.ui.live.runtime.90b49b").c_str());
  if (!document_.GetAssetGuid().IsValid()) {
    ImGui::TextDisabled(TEXT::Localize("editor.ui.save.the.asset.before.attaching.a.live.runtime.instance.0d56b7").c_str());
    return;
  }

  auto *service = scene.GetWorld()
                      .Services()
                      .Find<ANIMATION::AnimationStateMachineRuntimeService>();
  if (service == nullptr) {
    ImGui::TextDisabled(TEXT::Localize("editor.ui.animation.runtime.service.is.unavailable.57d3a5").c_str());
    return;
  }

  std::string currentName = "<no active instance>";
  if (const GameObject *selected =
          scene.GetWorld().FindObject(runtimeDebugObject_)) {
    currentName = selected->GetName();
  }
  if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.runtime.object.2b05fe", "Runtime Object").c_str(), currentName.c_str())) {
    for (const auto &object : scene.GetWorld().GetObjects()) {
      const auto *component =
          object->GetComponent<AnimationStateMachineComponent>();
      const auto *instance = service->Find(object->GetRuntimeHandle());
      if (component == nullptr || instance == nullptr ||
          component->GetAssetGuid() != document_.GetAssetGuid().value) {
        continue;
      }
      const bool selected = object->GetRuntimeHandle() == runtimeDebugObject_;
      const std::string label =
          object->GetName() + "##Runtime" +
          std::to_string(object->GetRuntimeHandle().ToValue());
      if (ImGui::Selectable(label.c_str(), selected)) {
        runtimeDebugObject_ = object->GetRuntimeHandle();
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const auto *snapshot = ResolveRuntimeDebugSnapshot(scene);
  if (snapshot == nullptr) {
    ImGui::TextDisabled(TEXT::Localize("editor.ui.no.playing.object.currently.uses.this.saved.asset.122e34").c_str());
    return;
  }

  ImGui::Text(TEXT::Localize("ui.quality.stateFormat").c_str(), snapshot->currentStateName.c_str());
  ImGui::SameLine();
  ImGui::TextColored(snapshot->running ? ImVec4(0.35f, 0.9f, 0.55f, 1.0f)
                                       : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                     "%s", snapshot->running ? "Running" : "Stopped");
  ImGui::Text(TEXT::Localize("editor.ui.primary.s.e5c3d0").c_str(), snapshot->primaryClipName.empty()
                                 ? "<none>"
                                 : snapshot->primaryClipName.c_str());
  if (snapshot->motionType ==
      ANIMATION::AnimationStateMotionType::BlendTree1D) {
    ImGui::Text("%s = %.3f  [%.3f .. %.3f]",
                snapshot->blendParameterName.empty()
                    ? "Blend"
                    : snapshot->blendParameterName.c_str(),
                snapshot->blendInputValue, snapshot->blendLowerThreshold,
                snapshot->blendUpperThreshold);
    ImGui::Text(TEXT::Localize("editor.ui.secondary.s.eac035").c_str(), snapshot->secondaryClipName.empty()
                                     ? "<none>"
                                     : snapshot->secondaryClipName.c_str());
    ImGui::ProgressBar(snapshot->motionBlendWeight, {-1.0f, 0.0f},
                       "Motion Blend");
  }
  ImGui::Text(TEXT::Localize("editor.ui.effective.speed.3f.338a4e").c_str(), snapshot->effectiveSpeed);
  const auto *instance = service->Find(runtimeDebugObject_);
  const auto *asset = instance != nullptr ? instance->GetAsset() : nullptr;
  if (asset != nullptr &&
      ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.parameters.a975ee", "Parameters").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("RuntimeParameters", 2, flags)) {
      ImGui::TableSetupColumn("Parameter");
      ImGui::TableSetupColumn("Value");
      ImGui::TableHeadersRow();
      for (const auto &parameter : asset->definition.parameters) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(parameter.name.c_str());
        ImGui::TableSetColumnIndex(1);
        const auto *value = instance->FindValue(parameter.id);
        if (const bool *boolean =
                value != nullptr ? std::get_if<bool>(value) : nullptr) {
          ImGui::TextUnformatted(*boolean ? TEXT::LocalizeCStr("editor.ui.true.88b33e") : TEXT::LocalizeCStr("editor.ui.false.97cdbd"));
        } else if (const float *number =
                       value != nullptr ? std::get_if<float>(value) : nullptr) {
          ImGui::Text("%.3f", *number);
        } else if (const int32_t *integer = value != nullptr
                                                ? std::get_if<int32_t>(value)
                                                : nullptr) {
          ImGui::Text("%d", *integer);
        } else {
          ImGui::TextDisabled(TEXT::Localize("ui.lightingBake.unavailable").c_str());
        }
      }
      ImGui::EndTable();
    }
  }
  if (!snapshot->lastError.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s",
                       snapshot->lastError.c_str());
  }
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
