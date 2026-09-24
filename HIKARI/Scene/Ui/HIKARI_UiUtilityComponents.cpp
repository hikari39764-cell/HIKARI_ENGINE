#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace HIKARI {
namespace {
float Number(const nlohmann::json& in, const char* key, float fallback, float minimum, float maximum) {
    const float value = JSONREAD::NumberMemberOr(in, key, fallback);
    return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
} // namespace
void UiGroupComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1}, {"visible", visible_}, {"interactable", interactable_}, {"opacity", opacity_}};
    out["modal"] = modal_;
}
void UiGroupComponent::Deserialize(const nlohmann::json& in) {
    visible_ = JSONREAD::BooleanMemberOr(in, "visible", true);
    modal_ = JSONREAD::BooleanMemberOr(in, "modal", false);
    interactable_ = JSONREAD::BooleanMemberOr(in, "interactable", true);
    opacity_ = Number(in, "opacity", 1, 0, 1);
}
void UiGroupComponent::SetOpacity(float value) noexcept {
    if (std::isfinite(value))
        opacity_ = std::clamp(value, 0.f, 1.f);
}
void UiGroupComponent::BuildInspector(IInspectorBuilder& b) {
    b.Bool(AuthoringFieldDescriptor{"modal", L("uix.modal")}, modal_);
    b.Bool(AuthoringFieldDescriptor{"visible", L("uix.visible")}, visible_);
    b.Bool(AuthoringFieldDescriptor{"interactable", L("uix.interactable")}, interactable_);
    b.FloatRange(AuthoringFieldDescriptor{"opacity", L("uix.opacity")}, opacity_, 0, 1);
}
void UiLayoutGroupComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1},
           {"enabled", enabled_},
           {"direction", static_cast<int>(direction_)},
           {"cellSize", {cellSize_.x, cellSize_.y}},
           {"spacing", spacing_},
           {"padding", padding_},
           {"columns", columns_}};
}
void UiLayoutGroupComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    direction_ = static_cast<UiLayoutDirection>(std::clamp(JSONREAD::IntegerMemberOr(in, "direction", 1), 0, 2));
    cellSize_ = {240, 56};
    if (auto v = JSONREAD::FindMember(in, "cellSize"))
        cellSize_ = JSONREAD::Vec2Or(*v, {240, 56});
    cellSize_.x = std::clamp(std::isfinite(cellSize_.x) ? cellSize_.x : 240.f, 1.f, 16384.f);
    cellSize_.y = std::clamp(std::isfinite(cellSize_.y) ? cellSize_.y : 56.f, 1.f, 16384.f);
    spacing_ = Number(in, "spacing", 12, 0, 16384);
    padding_ = Number(in, "padding", 16, 0, 16384);
    columns_ = std::clamp(JSONREAD::IntegerMemberOr(in, "columns", 3), 1, 256);
}
void UiLayoutGroupComponent::BuildInspector(IInspectorBuilder& b) {
    b.Bool(AuthoringFieldDescriptor{"enabled", L("uix.enabled")}, enabled_);
    const std::array<AuthoringChoice, 3> choices{
        {{"0", L("uix.horizontal")}, {"1", L("uix.vertical")}, {"2", L("uix.grid")}}};
    int direction = static_cast<int>(direction_);
    if (b.Choice(AuthoringFieldDescriptor{"direction", L("uix.arrangement")}, direction, choices))
        direction_ = static_cast<UiLayoutDirection>(std::clamp(direction, 0, 2));
    b.Vec2(AuthoringFieldDescriptor{"cellSize", L("uix.cellSize")}, cellSize_.x, cellSize_.y);
    b.FloatRange(AuthoringFieldDescriptor{"spacing", L("uix.spacing")}, spacing_, 0, 16384);
    b.FloatRange(AuthoringFieldDescriptor{"padding", L("uix.padding")}, padding_, 0, 16384);
    b.Int(AuthoringFieldDescriptor{"columns", L("uix.columns")}, columns_);
    nlohmann::json value;
    Serialize(value);
    Deserialize(value);
}
void UiProgressComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1},
           {"enabled", enabled_},
           {"value", value_},
           {"minimum", minimum_},
           {"maximum", maximum_},
           {"direction", static_cast<int>(direction_)},
           {"variableId", variableId_}};
}
void UiProgressComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    minimum_ = Number(in, "minimum", 0, -1e9f, 1e9f);
    maximum_ = Number(in, "maximum", 1, -1e9f, 1e9f);
    if (maximum_ < minimum_)
        std::swap(minimum_, maximum_);
    value_ = Number(in, "value", .75f, minimum_, maximum_);
    direction_ = static_cast<UiFillDirection>(std::clamp(JSONREAD::IntegerMemberOr(in, "direction", 0), 0, 3));
    variableId_ = JSONREAD::StringMemberOr(in, "variableId", {});
}
void UiProgressComponent::SetValue(float value) noexcept {
    if (std::isfinite(value))
        value_ = std::clamp(value, minimum_, maximum_);
}
float UiProgressComponent::ResolveFraction(const BINDING::RuntimeBindingStore* bindings) const noexcept {
    float value = value_;
    if (bindings && !variableId_.empty()) {
        BINDING::Value v;
        if (bindings->ReadVariable(variableId_, v)) {
            if (v.type == BINDING::ValueType::Float)
                value = v.numeric.x;
            else if (v.type == BINDING::ValueType::Int)
                value = static_cast<float>(v.integer);
        }
    }
    if (!std::isfinite(value) || maximum_ <= minimum_)
        return 0;
    return std::clamp((value - minimum_) / (maximum_ - minimum_), 0.f, 1.f);
}
void UiProgressComponent::BuildInspector(IInspectorBuilder& b) {
    b.Bool(AuthoringFieldDescriptor{"enabled", L("uix.enabled")}, enabled_);
    b.Float(AuthoringFieldDescriptor{"value", L("uix.value")}, value_);
    b.Float(AuthoringFieldDescriptor{"minimum", L("uix.minimum")}, minimum_);
    b.Float(AuthoringFieldDescriptor{"maximum", L("uix.maximum")}, maximum_);
    const std::array<AuthoringChoice, 4> choices{
        {{"0", L("uix.right")}, {"1", L("uix.left")}, {"2", L("uix.up")}, {"3", L("uix.down")}}};
    int direction = static_cast<int>(direction_);
    if (b.Choice(AuthoringFieldDescriptor{"direction", L("uix.fill")}, direction, choices))
        direction_ = static_cast<UiFillDirection>(std::clamp(direction, 0, 3));
    b.RuntimeVariableIdPicker(AuthoringFieldDescriptor{"variableId", L("uix.variable")}, BINDING::ValueType::Float,
                              variableId_);
    nlohmann::json value;
    Serialize(value);
    Deserialize(value);
}
} // namespace HIKARI
