#pragma once

#include <string>
#include <utility>

#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

class AnimationStateMachineComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "AnimationStateMachineComponent"; }

    void Serialize(nlohmann::json& out) const override {
        out["enabled"] = enabled_;
        out["stateMachineAssetGuid"] = assetGuid_;
        out["playOnStart"] = playOnStart_;
        out["syncCharacterMotion"] = syncCharacterMotion_;
    }
    void Deserialize(const nlohmann::json& in) override {
        enabled_ = in.value("enabled", enabled_);
        assetGuid_ = in.value("stateMachineAssetGuid", assetGuid_);
        playOnStart_ = in.value("playOnStart", playOnStart_);
        syncCharacterMotion_ = in.value("syncCharacterMotion", syncCharacterMotion_);
        lastDiagnostic_.clear();
    }
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return enabled_; }
    void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
    const std::string& GetAssetGuid() const noexcept { return assetGuid_; }
    void SetAssetGuid(std::string guid) { assetGuid_ = std::move(guid); }
    bool GetPlayOnStart() const noexcept { return playOnStart_; }
    bool GetSyncCharacterMotion() const noexcept { return syncCharacterMotion_; }

private:
    bool enabled_ = true;
    std::string assetGuid_{};
    bool playOnStart_ = true;
    bool syncCharacterMotion_ = true;
    std::string lastDiagnostic_{};
};

} // namespace HIKARI
