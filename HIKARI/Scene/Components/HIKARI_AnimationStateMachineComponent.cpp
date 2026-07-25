#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"

#include <utility>

namespace HIKARI {

    void AnimationStateMachineComponent::Serialize(
        nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["stateMachineAssetGuid"] = assetGuid_;
        out["playOnStart"] = playOnStart_;
        out["syncCharacterMotion"] = syncCharacterMotion_;
    }

    void AnimationStateMachineComponent::Deserialize(
        const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        assetGuid_ = in.value("stateMachineAssetGuid", assetGuid_);
        playOnStart_ = in.value("playOnStart", playOnStart_);
        syncCharacterMotion_ = in.value(
            "syncCharacterMotion", syncCharacterMotion_);
        lastDiagnostic_.clear();
    }

    bool AnimationStateMachineComponent::IsEnabled() const noexcept {
        return enabled_;
    }

    void AnimationStateMachineComponent::SetEnabled(bool enabled) noexcept {
        enabled_ = enabled;
    }

    const std::string&
        AnimationStateMachineComponent::GetAssetGuid() const noexcept {
        return assetGuid_;
    }

    void AnimationStateMachineComponent::SetAssetGuid(std::string guid) {
        assetGuid_ = std::move(guid);
    }

    bool AnimationStateMachineComponent::GetPlayOnStart() const noexcept {
        return playOnStart_;
    }

    bool AnimationStateMachineComponent::GetSyncCharacterMotion()
        const noexcept {
        return syncCharacterMotion_;
    }

} // namespace HIKARI
