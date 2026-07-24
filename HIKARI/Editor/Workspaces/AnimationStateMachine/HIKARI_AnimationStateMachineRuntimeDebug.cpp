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
  ImGui::SeparatorText("Live Runtime");
  if (!document_.GetAssetGuid().IsValid()) {
    ImGui::TextDisabled(
        "Save the asset before attaching a live runtime instance.");
    return;
  }

  auto *service = scene.GetWorld()
                      .Services()
                      .Find<ANIMATION::AnimationStateMachineRuntimeService>();
  if (service == nullptr) {
    ImGui::TextDisabled("Animation runtime service is unavailable.");
    return;
  }

  std::string currentName = "<no active instance>";
  if (const GameObject *selected =
          scene.GetWorld().FindObject(runtimeDebugObject_)) {
    currentName = selected->GetName();
  }
  if (ImGui::BeginCombo("Runtime Object", currentName.c_str())) {
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
    ImGui::TextDisabled("No playing object currently uses this saved asset.");
    return;
  }

  ImGui::Text("State: %s", snapshot->currentStateName.c_str());
  ImGui::SameLine();
  ImGui::TextColored(snapshot->running ? ImVec4(0.35f, 0.9f, 0.55f, 1.0f)
                                       : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                     "%s", snapshot->running ? "Running" : "Stopped");
  ImGui::Text("Primary: %s", snapshot->primaryClipName.empty()
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
    ImGui::Text("Secondary: %s", snapshot->secondaryClipName.empty()
                                     ? "<none>"
                                     : snapshot->secondaryClipName.c_str());
    ImGui::ProgressBar(snapshot->motionBlendWeight, {-1.0f, 0.0f},
                       "Motion Blend");
  }
  ImGui::Text("Effective Speed: %.3f", snapshot->effectiveSpeed);
  const auto *instance = service->Find(runtimeDebugObject_);
  const auto *asset = instance != nullptr ? instance->GetAsset() : nullptr;
  if (asset != nullptr &&
      ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
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
          ImGui::TextUnformatted(*boolean ? "True" : "False");
        } else if (const float *number =
                       value != nullptr ? std::get_if<float>(value) : nullptr) {
          ImGui::Text("%.3f", *number);
        } else if (const int32_t *integer = value != nullptr
                                                ? std::get_if<int32_t>(value)
                                                : nullptr) {
          ImGui::Text("%d", *integer);
        } else {
          ImGui::TextDisabled("Unavailable");
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
