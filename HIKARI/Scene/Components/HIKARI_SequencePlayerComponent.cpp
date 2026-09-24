#include "Scene/Components/HIKARI_SequencePlayerComponent.h"

#include <algorithm>
#include <cmath>
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

namespace {
const char* ToString(SEQUENCER::SequenceChannelPolicy policy) noexcept {
    using Policy = SEQUENCER::SequenceChannelPolicy;
    switch (policy) {
    case Policy::Parallel: return "Parallel";
    case Policy::Replace: return "Replace";
    case Policy::RejectIfOccupied: return "RejectIfOccupied";
    case Policy::ReplaceIfHigherOrEqual:
    default: return "ReplaceIfHigherOrEqual";
    }
}

SEQUENCER::SequenceChannelPolicy ParseChannelPolicy(const nlohmann::json& value,
                                                    SEQUENCER::SequenceChannelPolicy fallback) noexcept {
    using Policy = SEQUENCER::SequenceChannelPolicy;
    if (value.is_number_integer()) {
        const int raw = value.get<int>();
        if (raw >= static_cast<int>(Policy::Parallel) && raw <= static_cast<int>(Policy::RejectIfOccupied)) {
            return static_cast<Policy>(raw);
        }
        return fallback;
    }
    if (!value.is_string()) {
        return fallback;
    }
    const std::string text = value.get<std::string>();
    if (text == "Parallel")
        return Policy::Parallel;
    if (text == "Replace")
        return Policy::Replace;
    if (text == "RejectIfOccupied") {
        return Policy::RejectIfOccupied;
    }
    if (text == "ReplaceIfHigherOrEqual") {
        return Policy::ReplaceIfHigherOrEqual;
    }
    return fallback;
}
} // namespace

void SequencePlayerComponent::OnAttach() { ResetRuntimeState(); }

void SequencePlayerComponent::Serialize(nlohmann::json& out) const {
    out["enabled"] = enabled_;
    out["sequenceAssetGuid"] = sequenceAssetGuid_;
    out["playOnStart"] = playOnStart_;
    out["loop"] = loop_;
    out["holdAtEnd"] = holdAtEnd_;
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
        out["bindings"].push_back(
            {{"slotName", binding.slotName},
             {"sceneObjectId", SCENE::SERIALIZATION::WriteSceneObjectReference(binding.sceneObjectId.value)}});
    }
}

void SequencePlayerComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = in.value("enabled", enabled_);
    sequenceAssetGuid_ = in.value("sequenceAssetGuid", sequenceAssetGuid_);
    playOnStart_ = in.value("playOnStart", playOnStart_);
    loop_ = in.value("loop", loop_);
    holdAtEnd_ = in.value("holdAtEnd", holdAtEnd_);
    playbackRate_ = in.value("playbackRate", playbackRate_);
    startTimeSeconds_ = in.value("startTimeSeconds", startTimeSeconds_);
    channel_ = in.value("channel", channel_);
    priority_ = in.value("priority", priority_);
    channelPolicy_ = ParseChannelPolicy(in.value("channelPolicy", nlohmann::json{}), channelPolicy_);
    stopOnDisable_ = in.value("stopOnDisable", stopOnDisable_);
    restartIfPlaying_ = in.value("restartIfPlaying", restartIfPlaying_);
    ownerSlotName_ = in.value("ownerSlotName", ownerSlotName_);

    if (in.contains("bindings") && in["bindings"].is_array()) {
        bindings_.clear();
        for (const nlohmann::json& node : in["bindings"]) {
            if (!node.is_object()) {
                continue;
            }
            SequencePlayerBinding binding{};
            binding.slotName = node.value("slotName", std::string{});
            binding.sceneObjectId.value =
                SCENE::SERIALIZATION::SceneObjectReferenceOr(node.value("sceneObjectId", nlohmann::json{}));
            bindings_.push_back(std::move(binding));
        }
    }
    NormalizeSettings();
    ResetRuntimeState();
}

void SequencePlayerComponent::Play() { pendingAction_ = PendingAction::Play; }

void SequencePlayerComponent::Stop() { pendingAction_ = PendingAction::Stop; }

void SequencePlayerComponent::Pause() { pendingAction_ = PendingAction::Pause; }

void SequencePlayerComponent::Resume() { pendingAction_ = PendingAction::Resume; }

void SequencePlayerComponent::Seek(float timeSeconds) {
    pendingSeekTimeSeconds_ = std::isfinite(timeSeconds) ? (std::max)(timeSeconds, 0.0f) : 0.0f;
    pendingAction_ = PendingAction::Seek;
}

bool SequencePlayerComponent::IsEnabled() const noexcept { return enabled_; }

bool SequencePlayerComponent::IsPlaying() const noexcept { return playbackHandle_.IsValid() && !runtimePaused_; }

bool SequencePlayerComponent::IsPaused() const noexcept { return playbackHandle_.IsValid() && runtimePaused_; }

void SequencePlayerComponent::NormalizeSettings() {
    const float finiteRate = std::isfinite(playbackRate_) ? playbackRate_ : 1.0f;
    const float magnitude = std::clamp(std::abs(finiteRate), 0.01f, 8.0f);
    playbackRate_ = finiteRate < 0.0f ? -magnitude : magnitude;
    startTimeSeconds_ = std::isfinite(startTimeSeconds_) ? (std::max)(startTimeSeconds_, 0.0f) : 0.0f;
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
