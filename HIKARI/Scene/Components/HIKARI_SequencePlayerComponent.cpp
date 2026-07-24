#include "Scene/Components/HIKARI_SequencePlayerComponent.h"

#include <algorithm>
#include <cmath>

#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Sequence/HIKARI_SequenceAssetStore.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        const char* ToString(
            SEQUENCER::SequenceChannelPolicy policy) noexcept {

            using Policy = SEQUENCER::SequenceChannelPolicy;
            switch (policy) {
            case Policy::Parallel: return "Parallel";
            case Policy::Replace: return "Replace";
            case Policy::RejectIfOccupied: return "RejectIfOccupied";
            case Policy::ReplaceIfHigherOrEqual:
            default:
                return "ReplaceIfHigherOrEqual";
            }
        }

        SEQUENCER::SequenceChannelPolicy ParseChannelPolicy(
            const nlohmann::json& value,
            SEQUENCER::SequenceChannelPolicy fallback) noexcept {

            using Policy = SEQUENCER::SequenceChannelPolicy;
            if (value.is_number_integer()) {
                const int raw = value.get<int>();
                if (raw >= static_cast<int>(Policy::Parallel) &&
                    raw <= static_cast<int>(Policy::RejectIfOccupied)) {
                    return static_cast<Policy>(raw);
                }
                return fallback;
            }
            if (!value.is_string()) {
                return fallback;
            }
            const std::string text = value.get<std::string>();
            if (text == "Parallel") return Policy::Parallel;
            if (text == "Replace") return Policy::Replace;
            if (text == "RejectIfOccupied") {
                return Policy::RejectIfOccupied;
            }
            if (text == "ReplaceIfHigherOrEqual") {
                return Policy::ReplaceIfHigherOrEqual;
            }
            return fallback;
        }
    }

    void SequencePlayerComponent::OnAttach() {
        ResetRuntimeState();
    }

    void SequencePlayerComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["sequenceAssetGuid"] = sequenceAssetGuid_;
        out["playOnStart"] = playOnStart_;
        out["loop"] = loop_;
        out["playbackRate"] = playbackRate_;
        out["startTimeSeconds"] = startTimeSeconds_;
        out["channel"] = channel_;
        out["priority"] = priority_;
        out["channelPolicy"] = ToString(channelPolicy_);
        out["stopOnDisable"] = stopOnDisable_;
        out["restartIfPlaying"] = restartIfPlaying_;
        out["ownerSlotName"] = ownerSlotName_;
        out["bindings"] = nlohmann::json::array();
        for (const SequencePlayerBinding& binding : bindings_) {
            out["bindings"].push_back({
                { "slotName", binding.slotName },
                { "sceneObjectId", binding.sceneObjectId.value }
            });
        }
    }

    void SequencePlayerComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        sequenceAssetGuid_ = in.value(
            "sequenceAssetGuid",
            sequenceAssetGuid_);
        playOnStart_ = in.value("playOnStart", playOnStart_);
        loop_ = in.value("loop", loop_);
        playbackRate_ = in.value("playbackRate", playbackRate_);
        startTimeSeconds_ = in.value(
            "startTimeSeconds",
            startTimeSeconds_);
        channel_ = in.value("channel", channel_);
        priority_ = in.value("priority", priority_);
        channelPolicy_ = ParseChannelPolicy(
            in.value("channelPolicy", nlohmann::json{}),
            channelPolicy_);
        stopOnDisable_ = in.value("stopOnDisable", stopOnDisable_);
        restartIfPlaying_ = in.value(
            "restartIfPlaying",
            restartIfPlaying_);
        ownerSlotName_ = in.value("ownerSlotName", ownerSlotName_);

        if (in.contains("bindings") && in["bindings"].is_array()) {
            bindings_.clear();
            for (const nlohmann::json& node : in["bindings"]) {
                if (!node.is_object()) {
                    continue;
                }
                SequencePlayerBinding binding{};
                binding.slotName = node.value("slotName", std::string{});
                binding.sceneObjectId.value = node.value(
                    "sceneObjectId",
                    0ull);
                bindings_.push_back(std::move(binding));
            }
        }
        NormalizeSettings();
        ResetRuntimeState();
    }

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
                    lastDiagnostic_ = "Binding slots synchronized from asset.";
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
#if defined(HIKARI_WITH_EDITOR)
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
#endif
    }

    void SequencePlayerComponent::Play() {
        pendingAction_ = PendingAction::Play;
    }

    void SequencePlayerComponent::Stop() {
        pendingAction_ = PendingAction::Stop;
    }

    void SequencePlayerComponent::Pause() {
        pendingAction_ = PendingAction::Pause;
    }

    void SequencePlayerComponent::Resume() {
        pendingAction_ = PendingAction::Resume;
    }

    void SequencePlayerComponent::Seek(float timeSeconds) {
        pendingSeekTimeSeconds_ = std::isfinite(timeSeconds)
            ? (std::max)(timeSeconds, 0.0f)
            : 0.0f;
        pendingAction_ = PendingAction::Seek;
    }

    bool SequencePlayerComponent::IsEnabled() const noexcept {
        return enabled_;
    }

    bool SequencePlayerComponent::IsPlaying() const noexcept {
        return playbackHandle_.IsValid() && !runtimePaused_;
    }

    bool SequencePlayerComponent::IsPaused() const noexcept {
        return playbackHandle_.IsValid() && runtimePaused_;
    }

    SEQUENCER::SequencePlaybackHandle
        SequencePlayerComponent::GetPlaybackHandle() const noexcept {

        return playbackHandle_;
    }

    const std::string&
        SequencePlayerComponent::GetLastDiagnostic() const noexcept {

        return lastDiagnostic_;
    }

    void SequencePlayerComponent::NormalizeSettings() {
        playbackRate_ = std::clamp(
            std::isfinite(playbackRate_) ? playbackRate_ : 1.0f,
            0.01f,
            8.0f);
        startTimeSeconds_ = std::isfinite(startTimeSeconds_)
            ? (std::max)(startTimeSeconds_, 0.0f)
            : 0.0f;
        if (bindings_.size() > 32) {
            bindings_.resize(32);
        }
    }

    void SequencePlayerComponent::ResetRuntimeState() {
        playbackHandle_ = {};
        pendingAction_ = PendingAction::None;
        pendingSeekTimeSeconds_ = 0.0f;
        autoPlayPending_ = playOnStart_;
        runtimePaused_ = false;
        lastDiagnostic_.clear();
    }

} // namespace HIKARI
