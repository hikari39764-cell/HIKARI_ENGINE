#pragma once

#include <string>
#include <utility>

#include "Assets/HIKARI_AssetTypeEnum.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kAnimationStateMachineFields）が保存・読み取り・検査器・説明を
// 決める。
struct AnimationStateMachineSettings {
    bool enabled = true;
    std::string stateMachineAssetGuid{};
    bool playOnStart = true;
    bool syncCharacterMotion = true;
};

inline constexpr auto kAnimationStateMachineFields = [](auto&& field) {
    using S = AnimationStateMachineSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"});
    field("stateMachineAssetGuid", &S::stateMachineAssetGuid, Label{"State Machine Asset"},
          AssetRef{AssetType::AnimationStateMachine});
    field("playOnStart", &S::playOnStart, Label{"Play On Start"});
    field("syncCharacterMotion", &S::syncCharacterMotion, Label{"Sync Character Motion"});
};

class AnimationStateMachineComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "AnimationStateMachineComponent"; }

    void Serialize(nlohmann::json& out) const override {
        out = nlohmann::json::object();
        JSONFIELDS::Write(out, settings_, kAnimationStateMachineFields);
    }
    void Deserialize(const nlohmann::json& in) override {
        AnimationStateMachineSettings decoded{};
        JSONFIELDS::Read(in, decoded, kAnimationStateMachineFields);
        settings_ = std::move(decoded);
    }
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return settings_.enabled; }
    void SetEnabled(bool enabled) noexcept { settings_.enabled = enabled; }
    const std::string& GetAssetGuid() const noexcept { return settings_.stateMachineAssetGuid; }
    void SetAssetGuid(std::string guid) { settings_.stateMachineAssetGuid = std::move(guid); }
    bool GetPlayOnStart() const noexcept { return settings_.playOnStart; }
    bool GetSyncCharacterMotion() const noexcept { return settings_.syncCharacterMotion; }

private:
    AnimationStateMachineSettings settings_{};
};

} // namespace HIKARI
