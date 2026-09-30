#include "Scene/Ui/HIKARI_UiDataBindingComponent.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiBindingSourceComponent.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include <algorithm>
#include <charconv>

namespace HIKARI {
namespace {
bool ReadBool(const IComponent& component, const BINDING::RuntimeBindingStore* store, const std::string& variable,
              UiBindingReadMode mode) {
    if (variable.empty()) {
        return true;
    }
    BINDING::Value value;
    const UiBoundValueStatus status = ReadUiBoundValue(component, store, variable, value, mode);
    if (status == UiBoundValueStatus::InvalidObjectSource) {
        return false;
    }
    return status != UiBoundValueStatus::Value || value.type != BINDING::ValueType::Bool || value.boolean;
}
} // namespace

UiBoundValueStatus ReadUiBoundValue(const IComponent& component, const BINDING::RuntimeBindingStore* store,
                                    std::string_view variableId, BINDING::Value& outValue,
                                    UiBindingReadMode mode) noexcept {
    const GameObject* owner = component.GetOwner();
    if (owner == nullptr) {
        return UiBoundValueStatus::Unbound;
    }
    RuntimeObjectHandle target{};
    const UiBindingSourceState sourceState = ResolveInheritedUiBindingSource(*owner, target);
    if (sourceState == UiBindingSourceState::Invalid) {
        return mode == UiBindingReadMode::AuthoringPreview ? UiBoundValueStatus::Unbound
                                                           : UiBoundValueStatus::InvalidObjectSource;
    }
    if (store == nullptr || variableId.empty()) {
        return UiBoundValueStatus::Unbound;
    }
    const bool read = sourceState == UiBindingSourceState::Resolved
                          ? store->ReadObjectVariable(target, variableId, outValue)
                          : store->ReadVariable(variableId, outValue);
    return read ? UiBoundValueStatus::Value : UiBoundValueStatus::Unbound;
}

void UiDataBindingComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1},
           {"enabled", enabled_},
           {"integer", integer_},
           {"textVariable", textVariable_},
           {"visibleVariable", visibleVariable_},
           {"interactableVariable", interactableVariable_},
           {"prefix", prefix_},
           {"suffix", suffix_},
           {"decimals", decimals_}};
}
void UiDataBindingComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    integer_ = JSONREAD::BooleanMemberOr(in, "integer", true);
    textVariable_ = JSONREAD::StringMemberOr(in, "textVariable", {});
    visibleVariable_ = JSONREAD::StringMemberOr(in, "visibleVariable", {});
    interactableVariable_ = JSONREAD::StringMemberOr(in, "interactableVariable", {});
    prefix_ = JSONREAD::StringMemberOr(in, "prefix", {});
    suffix_ = JSONREAD::StringMemberOr(in, "suffix", {});
    decimals_ = std::clamp(JSONREAD::IntegerMemberOr(in, "decimals", 0), 0, 6);
}
std::string UiDataBindingComponent::ResolveText(const BINDING::RuntimeBindingStore* store,
                                                std::string_view fallback) const {
    return ResolveTextWithMode(store, fallback, UiBindingReadMode::Runtime);
}
std::string UiDataBindingComponent::ResolveTextForPreview(const BINDING::RuntimeBindingStore* store,
                                                          std::string_view fallback) const {
    return ResolveTextWithMode(store, fallback, UiBindingReadMode::AuthoringPreview);
}
std::string UiDataBindingComponent::ResolveTextWithMode(const BINDING::RuntimeBindingStore* store,
                                                        std::string_view fallback, UiBindingReadMode mode) const {
    BINDING::Value value;
    if (!enabled_ || textVariable_.empty()) {
        return std::string(fallback);
    }
    const UiBoundValueStatus status = ReadUiBoundValue(*this, store, textVariable_, value, mode);
    if (status == UiBoundValueStatus::InvalidObjectSource) {
        return {};
    }
    if (status != UiBoundValueStatus::Value) {
        return std::string(fallback);
    }
    char buffer[128]{};
    std::to_chars_result result;
    if (value.type == BINDING::ValueType::Int)
        result = std::to_chars(buffer, buffer + sizeof(buffer), value.integer);
    else if (value.type == BINDING::ValueType::Float && MATH::IsFinite(value.numeric.x))
        result = std::to_chars(buffer, buffer + sizeof(buffer), value.numeric.x, std::chars_format::fixed, decimals_);
    else
        return std::string(fallback);
    if (result.ec != std::errc{})
        return std::string(fallback);
    return prefix_ + std::string(buffer, result.ptr) + suffix_;
}
bool UiDataBindingComponent::IsVisible(const BINDING::RuntimeBindingStore* store) const {
    return IsVisibleWithMode(store, UiBindingReadMode::Runtime);
}
bool UiDataBindingComponent::IsInteractable(const BINDING::RuntimeBindingStore* store) const {
    return IsInteractableWithMode(store, UiBindingReadMode::Runtime);
}
bool UiDataBindingComponent::IsVisibleForPreview(const BINDING::RuntimeBindingStore* store) const {
    return IsVisibleWithMode(store, UiBindingReadMode::AuthoringPreview);
}
bool UiDataBindingComponent::IsInteractableForPreview(const BINDING::RuntimeBindingStore* store) const {
    return IsInteractableWithMode(store, UiBindingReadMode::AuthoringPreview);
}
bool UiDataBindingComponent::IsVisibleWithMode(const BINDING::RuntimeBindingStore* store,
                                               UiBindingReadMode mode) const {
    return !enabled_ || ReadBool(*this, store, visibleVariable_, mode);
}
bool UiDataBindingComponent::IsInteractableWithMode(const BINDING::RuntimeBindingStore* store,
                                                    UiBindingReadMode mode) const {
    return !enabled_ || ReadBool(*this, store, interactableVariable_, mode);
}
void UiDataBindingComponent::BuildInspector(IInspectorBuilder& b) {
    const auto l = [](const char* id) { return TEXT::LocalizeCStr(id); };
    b.Bool(AuthoringFieldDescriptor{"enabled", l("uix.enabled")}, enabled_);
    b.Bool(AuthoringFieldDescriptor{"integer", l("uibind.integer")}, integer_);
    b.RuntimeVariableIdPicker(AuthoringFieldDescriptor{"textVariable", l("uibind.text")},
                              integer_ ? BINDING::ValueType::Int : BINDING::ValueType::Float, textVariable_);
    b.String(AuthoringFieldDescriptor{"prefix", l("uibind.prefix")}, prefix_);
    b.String(AuthoringFieldDescriptor{"suffix", l("uibind.suffix")}, suffix_);
    if (!integer_)
        b.Int(AuthoringFieldDescriptor{"decimals", l("uibind.decimals")}, decimals_);
    b.RuntimeVariableIdPicker(AuthoringFieldDescriptor{"visibleVariable", l("uibind.visible")},
                              BINDING::ValueType::Bool, visibleVariable_);
    b.RuntimeVariableIdPicker(AuthoringFieldDescriptor{"interactableVariable", l("uibind.interactable")},
                              BINDING::ValueType::Bool, interactableVariable_);
    decimals_ = std::clamp(decimals_, 0, 6);
}
} // namespace HIKARI
