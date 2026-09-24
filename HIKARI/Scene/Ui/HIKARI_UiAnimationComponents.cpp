#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <cmath>

namespace HIKARI {
void UiAnimatorComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1},
           {"animationAssetGuid", animation_.value},
           {"enabled", enabled_},
           {"unscaled", unscaled_},
           {"speed", speed_}};
}
void UiAnimatorComponent::Deserialize(const nlohmann::json& in) {
    animation_.value = JSONREAD::StringMemberOr(in, "animationAssetGuid", "");
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    unscaled_ = JSONREAD::BooleanMemberOr(in, "unscaled", true);
    const auto speed = JSONREAD::NumberMemberOr(in, "speed", 1.f);
    speed_ = std::clamp(std::isfinite(speed) ? speed : 1.f, 0.01f, 100.f);
}
void UiAnimatorComponent::BuildInspector(IInspectorBuilder& b) {
    b.Bool(AuthoringFieldDescriptor{"enabled", TEXT::LocalizeCStr("uix.enabled")}, enabled_);
    b.AssetIdPicker(AuthoringFieldDescriptor{"animationAssetGuid", TEXT::LocalizeCStr("uianim.asset")},
                    AssetType::UiAnimation, animation_.value);
    b.FloatRange(AuthoringFieldDescriptor{"speed", TEXT::LocalizeCStr("uianim.speed")}, speed_, .01f, 100);
    b.Bool(AuthoringFieldDescriptor{"unscaled", TEXT::LocalizeCStr("uianim.unscaled")}, unscaled_);
}
void UiAnimationTargetComponent::Serialize(nlohmann::json& out) const { out = {{"version", 1}, {"slot", slot_}}; }
void UiAnimationTargetComponent::Deserialize(const nlohmann::json& in) {
    slot_ = JSONREAD::StringMemberOr(in, "slot", "");
}
void UiAnimationTargetComponent::BuildInspector(IInspectorBuilder& b) {
    b.String(AuthoringFieldDescriptor{"slot", TEXT::LocalizeCStr("uianim.slot")}, slot_);
}
} // namespace HIKARI
