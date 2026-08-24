#include "Editor/Workspaces/Sequence/HIKARI_SequenceBindingPanel.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "Editor/Authoring/HIKARI_SequencePreviewBindingResolver.h"
#include "Scene/Sequencer/HIKARI_SequenceBindingNaming.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {
const SceneObjectData *FindSceneObject(const SceneDocument &document,
                                       SceneObjectId objectId) noexcept {

  const auto found =
      std::find_if(document.objects.begin(), document.objects.end(),
                   [objectId](const SceneObjectData &object) {
                     return object.id == objectId;
                   });
  return found != document.objects.end() ? &*found : nullptr;
}

bool HasCameraComponent(const SceneObjectData &object) noexcept {
  return std::any_of(object.components.begin(), object.components.end(),
                     [](const SceneComponentData &component) {
                       return component.type == "CameraComponent";
                     });
}

bool IsBindingUsed(const CinematicSequence &sequence,
                   SEQUENCER::SequenceBindingId bindingId) noexcept {

  return std::any_of(sequence.cameraCutTrack.clips.begin(),
                     sequence.cameraCutTrack.clips.end(),
                     [bindingId](const SEQUENCER::CameraCutClip &clip) {
                       return clip.cameraBindingId == bindingId;
                     }) ||
         std::any_of(
             sequence.cameraTransformTrack.channels.begin(),
             sequence.cameraTransformTrack.channels.end(),
             [bindingId](const SEQUENCER::CameraTransformChannel &channel) {
               return channel.cameraBindingId == bindingId;
             }) ||
         std::any_of(sequence.cameraLensTrack.channels.begin(),
                     sequence.cameraLensTrack.channels.end(),
                     [bindingId](const SEQUENCER::CameraLensChannel &channel) {
                       return channel.cameraBindingId == bindingId;
                     });
}

std::string MakeUniqueSlotName(const SEQUENCER::SequenceBindingTable &bindings,
                               std::string baseName,
                               SEQUENCER::SequenceBindingId ignoredId = {}) {

  baseName = SEQUENCER::NormalizeSequenceBindingSlotName(
      std::move(baseName));
  std::string candidate = baseName;
  uint32_t suffix = 2;
  for (;;) {
    const bool used =
        std::any_of(bindings.begin(), bindings.end(),
                    [&](const SEQUENCER::SequenceBinding &binding) {
                      return binding.id != ignoredId &&
                             binding.targetKind ==
                                 SEQUENCER::SequenceBindingTargetKind::Slot &&
                             binding.slotName == candidate;
                    });
    if (!used) {
      return candidate;
    }
    candidate = baseName + "." + std::to_string(suffix++);
  }
}

#if defined(HIKARI_WITH_EDITOR)
bool DrawCameraObjectCombo(const char *label, const SceneDocument &document,
                           SceneObjectId &selectedObjectId) {

  const SceneObjectData *selected = FindSceneObject(document, selectedObjectId);
  const std::string preview =
      selectedObjectId.value == 0
          ? std::string("<none>")
          : (selected != nullptr
                 ? selected->name
                 : "Missing Object " + std::to_string(selectedObjectId.value));
  bool changed = false;
  if (ImGui::BeginCombo(label, preview.c_str())) {
    if (ImGui::Selectable("<none>", selectedObjectId.value == 0)) {
      selectedObjectId = {};
      changed = true;
    }
    for (const SceneObjectData &object : document.objects) {
      if (!HasCameraComponent(object)) {
        continue;
      }
      const bool isSelected = object.id == selectedObjectId;
      const std::string itemLabel = object.name + "  [" +
                                    std::to_string(object.id.value) + "]##" +
                                    std::to_string(object.id.value);
      if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
        selectedObjectId = object.id;
        changed = true;
      }
      if (isSelected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}
#endif

void ConvertToPortableSlot(const SceneDocument &document,
                           SEQUENCER::SequenceBindingTable &bindings,
                           SEQUENCER::SequenceBinding &binding,
                           SEQUENCER::SequenceBindingContext &previewBindings) {

  const SceneObjectId previousObject = binding.sceneObjectId;
  const std::string baseName =
      previousObject.value != 0
          ? BuildCameraPreviewSlotName(document, previousObject)
          : "Camera." +
              SEQUENCER::NormalizeSequenceBindingSlotName(
                  binding.name);
  binding.targetKind = SEQUENCER::SequenceBindingTargetKind::Slot;
  binding.slotName = MakeUniqueSlotName(bindings, baseName, binding.id);
  binding.sceneObjectId = {};
  if (previousObject.value != 0) {
    (void)previewBindings.Bind(binding.id, previousObject);
  }
}
} // namespace

SEQUENCER::SequenceBindingId FindOrCreatePortableCameraBinding(
    const SceneDocument &document, CinematicSequence &sequence,
    SceneObjectId cameraObjectId,
    SEQUENCER::SequenceBindingContext &previewBindings) {

  if (cameraObjectId.value == 0) {
    return {};
  }
  SEQUENCER::SequenceBindingId availableBindingId{};
  for (const SEQUENCER::SequenceBinding &binding : sequence.bindings) {
    if (binding.targetKind != SEQUENCER::SequenceBindingTargetKind::Slot) {
      continue;
    }
    SceneObjectId previewObject{};
    if (previewBindings.Resolve(sequence.bindings, binding.id, previewObject) &&
        previewObject == cameraObjectId) {
      if (IsBindingUsed(sequence, binding.id)) {
        return binding.id;
      }
      if (!availableBindingId.IsValid()) {
        availableBindingId = binding.id;
      }
    }
  }
  if (availableBindingId.IsValid()) {
    return availableBindingId;
  }

  const std::string canonicalSlotName =
      BuildCameraPreviewSlotName(document, cameraObjectId);
  const auto canonicalBinding =
      std::find_if(sequence.bindings.begin(), sequence.bindings.end(),
                   [&](const SEQUENCER::SequenceBinding &binding) {
                     return binding.targetKind ==
                                SEQUENCER::SequenceBindingTargetKind::Slot &&
                            binding.slotName == canonicalSlotName;
                   });
  if (canonicalBinding != sequence.bindings.end()) {
    (void)previewBindings.Bind(canonicalBinding->id, cameraObjectId);
    return canonicalBinding->id;
  }

  const std::string slotName =
      MakeUniqueSlotName(sequence.bindings, canonicalSlotName);
  const SEQUENCER::SequenceBindingId bindingId =
      SEQUENCER::FindOrCreateSlotBinding(sequence.bindings, slotName, slotName);
  if (bindingId.IsValid()) {
    (void)previewBindings.Bind(bindingId, cameraObjectId);
  }
  return bindingId;
}

SequenceBindingPanelResult
SequenceBindingPanel::Draw(const SceneDocument &document,
                           CinematicSequence &sequence,
                           const AssetGuid &sequenceAssetGuid,
                           bool portableAsset, bool editingAllowed) {

  SequenceBindingPanelResult result{};
  PreparePreviewBindings(document, sequence, sequenceAssetGuid, portableAsset);
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::CollapsingHeader("Bindings", ImGuiTreeNodeFlags_None)) {
    return result;
  }
  ImGui::TextDisabled(
      portableAsset ? "Slots are saved in the asset; Preview Cameras are "
                      "resolved from this scene."
                    : "Embedded sequences may bind directly to scene cameras.");

  const bool hasDirectBindings =
      std::any_of(sequence.bindings.begin(), sequence.bindings.end(),
                  [](const SEQUENCER::SequenceBinding &binding) {
                    return binding.targetKind ==
                           SEQUENCER::SequenceBindingTargetKind::SceneObject;
                  });
  if (portableAsset && hasDirectBindings) {
    if (!editingAllowed) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Convert Direct Bindings To Slots")) {
      for (SEQUENCER::SequenceBinding &binding : sequence.bindings) {
        if (binding.targetKind ==
            SEQUENCER::SequenceBindingTargetKind::SceneObject) {
          ConvertToPortableSlot(document, sequence.bindings, binding,
                                previewBindings_);
        }
      }
      result.sequenceChanged = true;
    }
    if (!editingAllowed) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                       "Direct scene references reduce asset portability.");
  }

  if (!editingAllowed) {
    ImGui::BeginDisabled();
  }
  if (ImGui::SmallButton("Add Binding Slot")) {
    const std::string slotName =
        MakeUniqueSlotName(sequence.bindings, "Camera.Slot");
    (void)SEQUENCER::FindOrCreateSlotBinding(sequence.bindings, slotName,
                                             slotName);
    result.sequenceChanged = true;
  }
  if (!editingAllowed) {
    ImGui::EndDisabled();
  }

  SEQUENCER::SequenceBindingId bindingToRemove{};
  for (SEQUENCER::SequenceBinding &binding : sequence.bindings) {
    ImGui::PushID(static_cast<int>(binding.id.value));
    ImGui::Separator();
    ImGui::Text("Binding %llu", binding.id.value);
    if (!editingAllowed) {
      ImGui::BeginDisabled();
    }

    std::array<char, 128> nameBuffer{};
    std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s",
                  binding.name.c_str());
    if (ImGui::InputText("Display Name", nameBuffer.data(),
                         nameBuffer.size())) {
      binding.name = nameBuffer.data();
      result.sequenceChanged = true;
    }
    if (ImGui::Checkbox("Required", &binding.required)) {
      result.sequenceChanged = true;
    }

    const bool slotBinding =
        binding.targetKind == SEQUENCER::SequenceBindingTargetKind::Slot;
    const char *typeLabel = slotBinding ? "Slot" : "Scene Object";
    if (ImGui::BeginCombo("Target Kind", typeLabel)) {
      if (ImGui::Selectable("Slot", slotBinding)) {
        if (!slotBinding) {
          ConvertToPortableSlot(document, sequence.bindings, binding,
                                previewBindings_);
          result.sequenceChanged = true;
        }
      }
      if (!portableAsset && ImGui::Selectable("Scene Object", !slotBinding)) {
        if (slotBinding) {
          SceneObjectId previewObject{};
          (void)previewBindings_.Resolve(sequence.bindings, binding.id,
                                         previewObject);
          binding.targetKind =
              SEQUENCER::SequenceBindingTargetKind::SceneObject;
          binding.sceneObjectId = previewObject;
          binding.slotName.clear();
          (void)previewBindings_.Unbind(binding.id);
          result.sequenceChanged = true;
        }
      }
      ImGui::EndCombo();
    }

    if (binding.targetKind == SEQUENCER::SequenceBindingTargetKind::Slot) {
      std::array<char, 128> slotBuffer{};
      std::snprintf(slotBuffer.data(), slotBuffer.size(), "%s",
                    binding.slotName.c_str());
      if (ImGui::InputText("Slot Name", slotBuffer.data(), slotBuffer.size())) {
        binding.slotName = MakeUniqueSlotName(sequence.bindings,
                                              slotBuffer.data(), binding.id);
        if (binding.name.empty()) {
          binding.name = binding.slotName;
        }
        result.sequenceChanged = true;
      }
      SceneObjectId previewObject{};
      (void)previewBindings_.Resolve(sequence.bindings, binding.id,
                                     previewObject);
      if (DrawCameraObjectCombo("Preview Camera", document, previewObject)) {
        if (previewObject.value == 0) {
          (void)previewBindings_.Unbind(binding.id);
        } else {
          (void)previewBindings_.Bind(binding.id, previewObject);
        }
      }
    } else if (DrawCameraObjectCombo("Scene Camera", document,
                                     binding.sceneObjectId)) {
      result.sequenceChanged = true;
    }
    const bool bindingUsed = IsBindingUsed(sequence, binding.id);
    if (bindingUsed) {
      ImGui::TextDisabled("Used by one or more camera tracks");
    } else if (ImGui::SmallButton("Remove Unused Binding")) {
      bindingToRemove = binding.id;
    }
    if (!editingAllowed) {
      ImGui::EndDisabled();
    }
    ImGui::PopID();
  }
  if (bindingToRemove.IsValid()) {
    sequence.bindings.erase(
        std::remove_if(
            sequence.bindings.begin(), sequence.bindings.end(),
            [bindingToRemove](const SEQUENCER::SequenceBinding &binding) {
              return binding.id == bindingToRemove;
            }),
        sequence.bindings.end());
    (void)previewBindings_.Unbind(bindingToRemove);
    result.sequenceChanged = true;
  }
  SEQUENCER::NormalizeSequenceBindings(sequence.bindings);
#else
  (void)document;
  (void)sequence;
  (void)sequenceAssetGuid;
  (void)portableAsset;
  (void)editingAllowed;
#endif
  return result;
}

SEQUENCER::SequenceBindingContext &
SequenceBindingPanel::GetPreviewBindings() noexcept {

  return previewBindings_;
}

const SEQUENCER::SequenceBindingContext &
SequenceBindingPanel::GetPreviewBindings() const noexcept {

  return previewBindings_;
}

void SequenceBindingPanel::Reset() {
  previewBindings_.Clear();
  previewSourceAssetGuid_ = {};
  previewSourceSequenceId_ = {};
  previewSourcePortable_ = false;
  previewSourceInitialized_ = false;
}

void SequenceBindingPanel::PreparePreviewBindings(
    const SceneDocument &document, const CinematicSequence &sequence,
    const AssetGuid &sequenceAssetGuid, bool portableAsset) {

  const bool sameSource =
      previewSourceInitialized_ && previewSourcePortable_ == portableAsset &&
      previewSourceSequenceId_ == sequence.id &&
      (!portableAsset || previewSourceAssetGuid_ == sequenceAssetGuid);
  if (sameSource) {
    return;
  }
  previewBindings_.Clear();
  if (portableAsset) {
    (void)RestoreSequencePreviewBindings(document, sequenceAssetGuid, sequence,
                                         previewBindings_);
  }
  previewSourceAssetGuid_ = sequenceAssetGuid;
  previewSourceSequenceId_ = sequence.id;
  previewSourcePortable_ = portableAsset;
  previewSourceInitialized_ = true;
}

} // namespace HIKARI::EDITOR
