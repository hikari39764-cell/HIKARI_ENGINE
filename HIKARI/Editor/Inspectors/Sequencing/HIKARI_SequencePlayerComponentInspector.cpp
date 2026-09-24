#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Components/HIKARI_SequencePlayerComponent.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/HIKARI_DocumentAssetStores.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "imgui.h"

namespace HIKARI {

void SequencePlayerComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled", ""}, enabled_);
    const bool assetChanged = builder.AssetIdPicker(
        AuthoringFieldDescriptor{"sequenceAssetGuid", "Sequence Asset",
                                 "Sequence asset played by this component; assigning synchronizes binding slots."},
        AssetType::Sequence, sequenceAssetGuid_);
    builder.Bool(AuthoringFieldDescriptor{"playOnStart", "Play On Start",
                                          "Automatically play the assigned sequence when the scene starts."},
                 playOnStart_);
    builder.Bool(AuthoringFieldDescriptor{"loop", "Loop", ""}, loop_);
    builder.Bool(AuthoringFieldDescriptor{"holdAtEnd", "Hold At End", ""}, holdAtEnd_);
    builder.Float(AuthoringFieldDescriptor{"playbackRate", "Playback Rate", ""}, playbackRate_);
    builder.Float(AuthoringFieldDescriptor{"startTimeSeconds", "Start Time", "Playback offset in seconds."},
                  startTimeSeconds_);
    builder.String(AuthoringFieldDescriptor{"channel", "Channel", ""}, channel_);
    builder.Int(AuthoringFieldDescriptor{"priority", "Priority", ""}, priority_);
    int policy = static_cast<int>(channelPolicy_);
    constexpr AuthoringChoice kChannelPolicies[]{
        {"parallel", "Parallel"},
        {"replace", "Replace"},
        {"replaceIfHigherOrEqual", "Replace If Higher Or Equal"},
        {"rejectIfOccupied", "Reject If Occupied"},
    };
    if (builder.Choice(
            AuthoringFieldDescriptor{"channelPolicy", "Channel Policy", "How playback shares the selected channel."},
            policy, kChannelPolicies)) {
        channelPolicy_ = static_cast<SEQUENCER::SequenceChannelPolicy>(policy);
    }
    builder.Bool(AuthoringFieldDescriptor{"stopOnDisable", "Stop On Disable", ""}, stopOnDisable_);
    builder.Bool(AuthoringFieldDescriptor{"restartIfPlaying", "Restart If Playing", ""}, restartIfPlaying_);
    builder.String(AuthoringFieldDescriptor{"ownerSlotName", "Owner Slot", ""}, ownerSlotName_);

    const bool syncBindingsRequested = builder.Button("Sync Binding Slots");
    if (assetChanged || syncBindingsRequested) {
        const InspectorContext& context = builder.GetContext();
        if (context.assetDatabase == nullptr || sequenceAssetGuid_.empty()) {
            lastDiagnostic_ = "Assign a valid Sequence Asset before synchronizing slots.";
        } else {
            SequenceAssetStore store{};
            store.SetAssetDatabase(context.assetDatabase);
            std::string error{};
            const std::shared_ptr<const SequenceAsset> asset = store.Load(AssetGuid{sequenceAssetGuid_}, &error);
            if (!asset) {
                lastDiagnostic_ = error.empty() ? "Sequence Asset could not be loaded." : error;
            } else {
                std::vector<SequencePlayerBinding> synchronized{};
                for (const SEQUENCER::SequenceBinding& assetBinding : asset->sequence.bindings) {
                    if (assetBinding.targetKind != SEQUENCER::SequenceBindingTargetKind::Slot ||
                        assetBinding.slotName.empty() || assetBinding.slotName == ownerSlotName_) {
                        continue;
                    }
                    const auto existing =
                        std::find_if(bindings_.begin(), bindings_.end(), [&](const SequencePlayerBinding& binding) {
                            return binding.slotName == assetBinding.slotName;
                        });
                    synchronized.push_back({assetBinding.slotName,
                                            existing != bindings_.end() ? existing->sceneObjectId : SceneObjectId{}});
                }
                bindings_ = std::move(synchronized);
                lastDiagnostic_ = "Binding slots synchronized from asset.";
            }
        }
    }
    if (!lastDiagnostic_.empty()) {
        builder.Text(lastDiagnostic_);
    }

    int bindingCount = static_cast<int>(bindings_.size());
    if (builder.Int(AuthoringFieldDescriptor{"bindingCount", "Binding Count",
                                             "Number of explicit sequence slot to scene object bindings (0 to 32)."},
                    bindingCount)) {
        bindingCount = std::clamp(bindingCount, 0, 32);
        bindings_.resize(static_cast<size_t>(bindingCount));
    }
    for (size_t index = 0; index < bindings_.size(); ++index) {
        SequencePlayerBinding& binding = bindings_[index];
        const auto prefix = "bindings." + std::to_string(index);
        builder.String(AuthoringFieldDescriptor{prefix + ".slotName", "Slot " + std::to_string(index),
                                                "Sequence binding slot name."},
                       binding.slotName);
        builder.SceneObjectIdPicker(AuthoringFieldDescriptor{prefix + ".sceneObjectId",
                                                             "Object " + std::to_string(index),
                                                             "Scene object assigned to this slot."},
                                    binding.sceneObjectId);
    }
    NormalizeSettings();
}

void SequencePlayerComponent::RenderImGui() {
    if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Play", "Play##SequencePlayer").c_str())) {
        Play();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Pause", "Pause##SequencePlayer").c_str())) {
        Pause();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Resume", "Resume##SequencePlayer").c_str())) {
        Resume();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("ai.task.stop", "Stop##SequencePlayer").c_str())) {
        Stop();
    }
    if (!lastDiagnostic_.empty()) {
        ImGui::TextWrapped("%s", lastDiagnostic_.c_str());
    }
}

} // namespace HIKARI
