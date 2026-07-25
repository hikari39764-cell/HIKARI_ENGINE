#include "Scene/Components/HIKARI_SequencePlayerComponent.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Sequence/HIKARI_SequenceAssetStore.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "imgui.h"

namespace HIKARI {

    void SequencePlayerComponent::BuildInspector(
        IInspectorBuilder& builder) {

        builder.Bool("Enabled", enabled_);
        const bool assetChanged = builder.AssetIdPicker(
            "Sequence Asset",
            AssetType::Sequence,
            sequenceAssetGuid_);
        builder.Bool("Play On Start", playOnStart_);
        builder.Bool("Loop", loop_);
        builder.Float("Playback Rate", playbackRate_);
        builder.Float("Start Time", startTimeSeconds_);
        builder.String("Channel", channel_);
        builder.Int("Priority", priority_);
        int policy = static_cast<int>(channelPolicy_);
        constexpr const char* kChannelPolicies[]{
            "Parallel",
            "Replace",
            "Replace If Higher Or Equal",
            "Reject If Occupied",
        };
        if (builder.Choice(
                "Channel Policy",
                policy,
                kChannelPolicies)) {
            channelPolicy_ = static_cast<
                SEQUENCER::SequenceChannelPolicy>(policy);
        }
        builder.Bool("Stop On Disable", stopOnDisable_);
        builder.Bool("Restart If Playing", restartIfPlaying_);
        builder.String("Owner Slot", ownerSlotName_);

        const bool syncBindingsRequested =
            builder.Button("Sync Binding Slots");
        if (assetChanged || syncBindingsRequested) {
            const InspectorContext& context = builder.GetContext();
            if (context.assetDatabase == nullptr ||
                sequenceAssetGuid_.empty()) {
                lastDiagnostic_ =
                    "Assign a valid Sequence Asset before synchronizing slots.";
            } else {
                SequenceAssetStore store{};
                store.SetAssetDatabase(context.assetDatabase);
                std::string error{};
                const std::shared_ptr<const SequenceAsset> asset =
                    store.Load(AssetGuid{ sequenceAssetGuid_ }, &error);
                if (!asset) {
                    lastDiagnostic_ = error.empty()
                        ? "Sequence Asset could not be loaded."
                        : error;
                } else {
                    std::vector<SequencePlayerBinding> synchronized{};
                    for (const SEQUENCER::SequenceBinding& assetBinding :
                            asset->sequence.bindings) {
                        if (assetBinding.targetKind !=
                                SEQUENCER::SequenceBindingTargetKind::Slot ||
                            assetBinding.slotName.empty() ||
                            assetBinding.slotName == ownerSlotName_) {
                            continue;
                        }
                        const auto existing = std::find_if(
                            bindings_.begin(),
                            bindings_.end(),
                            [&](const SequencePlayerBinding& binding) {
                                return binding.slotName ==
                                    assetBinding.slotName;
                            });
                        synchronized.push_back({
                            assetBinding.slotName,
                            existing != bindings_.end()
                                ? existing->sceneObjectId
                                : SceneObjectId{}
                        });
                    }
                    bindings_ = std::move(synchronized);
                    lastDiagnostic_ =
                        "Binding slots synchronized from asset.";
                }
            }
        }
        if (!lastDiagnostic_.empty()) {
            builder.Text(lastDiagnostic_);
        }

        int bindingCount = static_cast<int>(bindings_.size());
        if (builder.Int("Binding Count", bindingCount)) {
            bindingCount = std::clamp(bindingCount, 0, 32);
            bindings_.resize(static_cast<size_t>(bindingCount));
        }
        for (size_t index = 0; index < bindings_.size(); ++index) {
            SequencePlayerBinding& binding = bindings_[index];
            builder.String(
                "Slot " + std::to_string(index),
                binding.slotName);
            builder.SceneObjectIdPicker(
                "Object " + std::to_string(index),
                binding.sceneObjectId);
        }
        NormalizeSettings();
    }

    void SequencePlayerComponent::RenderImGui() {
        if (ImGui::Button("Play##SequencePlayer")) {
            Play();
        }
        ImGui::SameLine();
        if (ImGui::Button("Pause##SequencePlayer")) {
            Pause();
        }
        ImGui::SameLine();
        if (ImGui::Button("Resume##SequencePlayer")) {
            Resume();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop##SequencePlayer")) {
            Stop();
        }
        if (!lastDiagnostic_.empty()) {
            ImGui::TextWrapped("%s", lastDiagnostic_.c_str());
        }
    }

} // namespace HIKARI
