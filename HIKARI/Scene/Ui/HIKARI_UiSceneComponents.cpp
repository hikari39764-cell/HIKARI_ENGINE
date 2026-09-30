#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "Core/Text/HIKARI_Localization.h"

#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_ComponentAuthoringTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }

float FiniteOr(float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; }

constexpr TEXT::EnumName<UiCanvasInputMode> kCanvasInputModeNames[] = {
    {UiCanvasInputMode::Hud, "Hud"},
    {UiCanvasInputMode::Menu, "Menu"},
};

MATH::Vec2 ReadVec2Member(const nlohmann::json& source, const char* key, MATH::Vec2 fallback) {
    const nlohmann::json* node = JSONREAD::FindMember(source, key);
    return node != nullptr ? JSONREAD::Vec2Or(*node, fallback) : fallback;
}

GameProjectUiColor ReadColorMember(const nlohmann::json& source, const char* key, GameProjectUiColor fallback) {
    const nlohmann::json* node = JSONREAD::FindMember(source, key);
    if (node == nullptr) {
        return fallback;
    }
    const MATH::Vec4 value = JSONREAD::Vec4Or(*node, {fallback.r, fallback.g, fallback.b, fallback.a});
    return {FiniteOr(value.x, fallback.r), FiniteOr(value.y, fallback.g), FiniteOr(value.z, fallback.b),
            FiniteOr(value.w, fallback.a)};
}

nlohmann::json WriteVec2(const MATH::Vec2& value) { return nlohmann::json::array({value.x, value.y}); }

nlohmann::json WriteColor(const GameProjectUiColor& value) {
    return nlohmann::json::array({value.r, value.g, value.b, value.a});
}

void InspectColor(IInspectorBuilder& builder, std::string_view idPrefix, std::string_view label,
                  GameProjectUiColor& color) {
    const std::string colorId = std::string(idPrefix) + ".rgb";
    const std::string alphaId = std::string(idPrefix) + ".alpha";
    (void)builder.Color3(AuthoringFieldDescriptor{colorId, label, "Linear RGBA color multiplier."}, color.r, color.g,
                         color.b);
    (void)builder.FloatRange(AuthoringFieldDescriptor{alphaId, std::string(label) + " Alpha", "Opacity multiplier."},
                             color.a, 0.0f, 1.0f, 0.01f);
}

constexpr TEXT::EnumName<GameProjectUiTextAlign> kGameProjectUiTextAlignNames[] = {
    {GameProjectUiTextAlign::Center, "Center"},
    {GameProjectUiTextAlign::Right, "Right"},
    {GameProjectUiTextAlign::Left, "Left"},
};
const char* HorizontalAlignName(GameProjectUiTextAlign value) noexcept {
    return TEXT::EnumToName(kGameProjectUiTextAlignNames, value, "Left");
}

GameProjectUiTextAlign ParseHorizontalAlign(std::string_view value) noexcept {
    return TEXT::ParseEnum(kGameProjectUiTextAlignNames, value, GameProjectUiTextAlign::Left);
}

constexpr TEXT::EnumName<GameProjectUiVerticalAlign> kGameProjectUiVerticalAlignNames[] = {
    {GameProjectUiVerticalAlign::Center, "Center"},
    {GameProjectUiVerticalAlign::Bottom, "Bottom"},
    {GameProjectUiVerticalAlign::Top, "Top"},
};
const char* VerticalAlignName(GameProjectUiVerticalAlign value) noexcept {
    return TEXT::EnumToName(kGameProjectUiVerticalAlignNames, value, "Top");
}

GameProjectUiVerticalAlign ParseVerticalAlign(std::string_view value) noexcept {
    return TEXT::ParseEnum(kGameProjectUiVerticalAlignNames, value, GameProjectUiVerticalAlign::Top);
}

} // namespace

void UiCanvasComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1},
           {"enabled", enabled_},
           {"interactable", interactable_},
           {"inputMode", TEXT::EnumToName(kCanvasInputModeNames, inputMode_, "Hud")},
           {"referenceWidth", referenceWidth_},
           {"referenceHeight", referenceHeight_},
           {"sortOrder", sortOrder_}};
}

void UiCanvasComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    interactable_ = JSONREAD::BooleanMemberOr(in, "interactable", true);
    inputMode_ = UiCanvasInputMode::Hud;
    if (const auto* node = JSONREAD::FindMember(in, "inputMode"))
        inputMode_ = JSONREAD::EnumOr(*node, kCanvasInputModeNames, UiCanvasInputMode::Hud);
    if (!TEXT::IsNamedEnum(kCanvasInputModeNames, inputMode_))
        inputMode_ = UiCanvasInputMode::Hud;
    inputActive_ = true;
    pendingFocus_ = {};
    referenceWidth_ =
        std::clamp(FiniteOr(JSONREAD::NumberMemberOr(in, "referenceWidth", 1920.0f), 1920.0f), 1.0f, 16384.0f);
    referenceHeight_ =
        std::clamp(FiniteOr(JSONREAD::NumberMemberOr(in, "referenceHeight", 1080.0f), 1080.0f), 1.0f, 16384.0f);
    sortOrder_ = JSONREAD::IntegerMemberOr(in, "sortOrder", 0);
    sortOrder_ = std::clamp(sortOrder_, -kUiCanvasSortOrderLimit, kUiCanvasSortOrderLimit);
}

void UiCanvasComponent::BuildInspector(IInspectorBuilder& builder) {
    if (!builder.Section(L("UI Canvas"), true))
        return;
    (void)builder.Bool({"enabled", L("Enabled"), "Enables this Canvas and every child UI element."}, enabled_);
    (void)builder.Bool({"interactable", L("Interactable"), "Allows child buttons to receive pointer and action input."},
                       interactable_);
    constexpr auto inputModeValues = TEXT::EnumNameList(kCanvasInputModeNames);
    const std::array<AuthoringChoice, 2u> inputModes{{
        {inputModeValues[0], L("uix.hudMode")},
        {inputModeValues[1], L("uix.menuMode")},
    }};
    int inputMode = static_cast<int>(inputMode_);
    if (builder.Choice({"inputMode", L("uix.inputMode"), L("uix.inputModeHelp")}, inputMode, inputModes)) {
        inputMode_ = TEXT::IsNamedEnum(kCanvasInputModeNames, static_cast<UiCanvasInputMode>(inputMode))
                         ? static_cast<UiCanvasInputMode>(inputMode)
                         : UiCanvasInputMode::Hud;
    }
    (void)builder.FloatRange(
        {"referenceWidth", L("Reference Width"), "Authored logical width before aspect-preserving fit."},
        referenceWidth_, 1.0f, 16384.0f, 1.0f);
    (void)builder.FloatRange(
        {"referenceHeight", L("Reference Height"), "Authored logical height before aspect-preserving fit."},
        referenceHeight_, 1.0f, 16384.0f, 1.0f);
    (void)builder.Int({"sortOrder", L("Sort Order"), "Canvas ordering; larger values render and interact on top."},
                      sortOrder_);
    sortOrder_ = std::clamp(sortOrder_, -kUiCanvasSortOrderLimit, kUiCanvasSortOrderLimit);
}

void UiRectComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1},
           {"enabled", enabled_},
           {"anchorMinimum", WriteVec2(anchorMinimum_)},
           {"anchorMaximum", WriteVec2(anchorMaximum_)},
           {"offsetMinimum", WriteVec2(offsetMinimum_)},
           {"offsetMaximum", WriteVec2(offsetMaximum_)},
           {"pivot", WriteVec2(pivot_)},
           {"rotationDegrees", rotationDegrees_},
           {"order", order_}};
}

void UiRectComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    anchorMinimum_ = ReadVec2Member(in, "anchorMinimum", {0.5f, 0.5f});
    anchorMaximum_ = ReadVec2Member(in, "anchorMaximum", {0.5f, 0.5f});
    offsetMinimum_ = ReadVec2Member(in, "offsetMinimum", {-160.0f, -40.0f});
    offsetMaximum_ = ReadVec2Member(in, "offsetMaximum", {160.0f, 40.0f});
    pivot_ = ReadVec2Member(in, "pivot", {0.5f, 0.5f});
    rotationDegrees_ = FiniteOr(JSONREAD::NumberMemberOr(in, "rotationDegrees", 0.0f), 0.0f);
    order_ = JSONREAD::IntegerMemberOr(in, "order", 0);
    Sanitize();
}

void UiRectComponent::Sanitize() noexcept {
    anchorMinimum_.x = std::clamp(anchorMinimum_.x, 0.0f, 1.0f);
    anchorMinimum_.y = std::clamp(anchorMinimum_.y, 0.0f, 1.0f);
    anchorMaximum_.x = std::clamp(anchorMaximum_.x, 0.0f, 1.0f);
    anchorMaximum_.y = std::clamp(anchorMaximum_.y, 0.0f, 1.0f);
    if (anchorMinimum_.x > anchorMaximum_.x) {
        std::swap(anchorMinimum_.x, anchorMaximum_.x);
    }
    if (anchorMinimum_.y > anchorMaximum_.y) {
        std::swap(anchorMinimum_.y, anchorMaximum_.y);
    }
    pivot_.x = std::clamp(pivot_.x, 0.0f, 1.0f);
    pivot_.y = std::clamp(pivot_.y, 0.0f, 1.0f);
    order_ = std::clamp(order_, -kUiElementOrderLimit, kUiElementOrderLimit);
}

void UiRectComponent::BuildInspector(IInspectorBuilder& builder) {
    if (!builder.Section(L("UI Rect"), true))
        return;
    (void)builder.Bool({"enabled", L("Enabled"), "Enables layout, rendering and hit testing for this element."},
                       enabled_);
    const std::array<AuthoringChoice, 11u> presets{
        AuthoringChoice{"Custom", L("Custom")},  AuthoringChoice{"TopLeft", L("Top Left")},
        AuthoringChoice{"Top", L("Top")},        AuthoringChoice{"TopRight", L("Top Right")},
        AuthoringChoice{"Left", L("Left")},      AuthoringChoice{"Center", L("Center")},
        AuthoringChoice{"Right", L("Right")},    AuthoringChoice{"BottomLeft", L("Bottom Left")},
        AuthoringChoice{"Bottom", L("Bottom")},  AuthoringChoice{"BottomRight", L("Bottom Right")},
        AuthoringChoice{"Stretch", L("Stretch")}};
    int preset = 0;
    if (anchorMinimum_.x == anchorMaximum_.x && anchorMinimum_.y == anchorMaximum_.y) {
        const int column = anchorMinimum_.x <= 0.0f ? 0 : anchorMinimum_.x >= 1.0f ? 2 : 1;
        const int row = anchorMinimum_.y <= 0.0f ? 0 : anchorMinimum_.y >= 1.0f ? 2 : 1;
        preset = 1 + row * 3 + column;
    } else if (anchorMinimum_.x == 0.0f && anchorMinimum_.y == 0.0f && anchorMaximum_.x == 1.0f &&
               anchorMaximum_.y == 1.0f) {
        preset = 10;
    }
    if (builder.Choice({"anchorPreset", L("Anchor Preset"), "Common anchor placement; Custom preserves exact values."},
                       preset, presets) &&
        preset > 0) {
        if (preset == 10) {
            anchorMinimum_ = {0.0f, 0.0f};
            anchorMaximum_ = {1.0f, 1.0f};
        } else {
            const int index = preset - 1;
            const float x = static_cast<float>(index % 3) * 0.5f;
            const float y = static_cast<float>(index / 3) * 0.5f;
            anchorMinimum_ = {x, y};
            anchorMaximum_ = anchorMinimum_;
        }
    }
    (void)builder.Vec2(
        {"anchorMinimum", L("Anchor Minimum"), "Normalized minimum anchor in the parent Rect or Canvas."},
        anchorMinimum_.x, anchorMinimum_.y);
    (void)builder.Vec2(
        {"anchorMaximum", L("Anchor Maximum"), "Normalized maximum anchor in the parent Rect or Canvas."},
        anchorMaximum_.x, anchorMaximum_.y);
    (void)builder.Vec2({"offsetMinimum", L("Offset Minimum"), "Pixel offset from the minimum anchor."},
                       offsetMinimum_.x, offsetMinimum_.y);
    (void)builder.Vec2({"offsetMaximum", L("Offset Maximum"), "Pixel offset from the maximum anchor."},
                       offsetMaximum_.x, offsetMaximum_.y);
    (void)builder.Vec2({"pivot", L("Pivot"), "Normalized rotation and alignment pivot."}, pivot_.x, pivot_.y);
    (void)builder.Float({"rotationDegrees", L("Rotation"), "Clockwise rotation in degrees."}, rotationDegrees_);
    (void)builder.Int({"order", L("Order"), "Element ordering inside its Canvas."}, order_);
    Sanitize();
}

void UiImageComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1},
           {"enabled", enabled_},
           {"textureAssetGuid", textureAssetGuid_.value},
           {"textureAssetPath", textureAssetPath_},
           {"tint", WriteColor(tint_)},
           {"orderOffset", orderOffset_}};
}

void UiImageComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    textureAssetGuid_.value = JSONREAD::StringMemberOr(in, "textureAssetGuid", {});
    textureAssetPath_ = JSONREAD::StringMemberOr(in, "textureAssetPath", "generated://solid/ffffffff");
    tint_ = ReadColorMember(in, "tint", {0.08f, 0.12f, 0.20f, 0.96f});
    orderOffset_ = JSONREAD::IntegerMemberOr(in, "orderOffset", 0);
    orderOffset_ = std::clamp(orderOffset_, -kUiElementOrderLimit, kUiElementOrderLimit);
}

void UiImageComponent::BuildInspector(IInspectorBuilder& builder) {
    if (!builder.Section(L("UI Image"), true))
        return;
    (void)builder.Bool({"enabled", L("Enabled"), "Submits this image to the active Canvas."}, enabled_);
    (void)builder.AssetIdPicker({"textureAssetGuid", L("Texture"), "Texture asset GUID."}, AssetType::Texture,
                                textureAssetGuid_.value);
    (void)builder.String(
        {"textureAssetPath", L("Fallback Texture"), "Project-relative path or generated://solid/RRGGBBAA."},
        textureAssetPath_);
    InspectColor(builder, "tint", L("Tint"), tint_);
    (void)builder.Int({"orderOffset", L("Order Offset"), "Additional ordering relative to the UI Rect."}, orderOffset_);
    orderOffset_ = std::clamp(orderOffset_, -kUiElementOrderLimit, kUiElementOrderLimit);
}

void UiTextComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1},
           {"enabled", enabled_},
           {"text", text_},
           {"fontAssetPath", fontAssetPath_},
           {"pixelHeight", pixelHeight_},
           {"color", WriteColor(color_)},
           {"horizontalAlign", HorizontalAlignName(horizontalAlign_)},
           {"verticalAlign", VerticalAlignName(verticalAlign_)},
           {"orderOffset", orderOffset_}};
    style_.Write(out);
    binding_.Write(out);
}

void UiTextComponent::Deserialize(const nlohmann::json& in) {
    style_.Read(in);
    binding_.Read(in);
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    text_ = JSONREAD::StringMemberOr(in, "text", "Button");
    fontAssetPath_ = JSONREAD::StringMemberOr(in, "fontAssetPath", {});
    pixelHeight_ = std::clamp(FiniteOr(JSONREAD::NumberMemberOr(in, "pixelHeight", 34.0f), 34.0f), 4.0f, 512.0f);
    color_ = ReadColorMember(in, "color", {});
    horizontalAlign_ = ParseHorizontalAlign(JSONREAD::StringMemberOr(in, "horizontalAlign", "Center"));
    verticalAlign_ = ParseVerticalAlign(JSONREAD::StringMemberOr(in, "verticalAlign", "Center"));
    orderOffset_ = JSONREAD::IntegerMemberOr(in, "orderOffset", 1);
    orderOffset_ = std::clamp(orderOffset_, -kUiElementOrderLimit, kUiElementOrderLimit);
}

void UiTextComponent::BuildInspector(IInspectorBuilder& builder) {
    if (!builder.Section(L("UI Text"), true))
        return;
    (void)builder.Bool({"enabled", L("Enabled"), "Submits this text to the active Canvas."}, enabled_);
    (void)builder.MultilineString({"text", L("Text"), "UTF-8 display text."}, text_);
    binding_.Inspect(builder);
    if (!binding_.style.IsValid() || binding_.overrideSize)
        (void)builder.FloatRange({"pixelHeight", L("Pixel Height"), "Font size in reference-canvas pixels."},
                                 pixelHeight_, 4.0f, 512.0f, 1.0f);
    if (!binding_.style.IsValid() || binding_.overrideColor)
        InspectColor(builder, "color", L("Color"), color_);
    const std::array<AuthoringChoice, 3u> horizontal{AuthoringChoice{"Left", L("Left")},
                                                     AuthoringChoice{"Center", L("Center")},
                                                     AuthoringChoice{"Right", L("Right")}};
    int horizontalIndex = static_cast<int>(horizontalAlign_);
    if (builder.Choice({"horizontalAlign", L("Horizontal Align"), "Horizontal text alignment."}, horizontalIndex,
                       horizontal)) {
        horizontalAlign_ = static_cast<GameProjectUiTextAlign>(std::clamp(horizontalIndex, 0, 2));
    }
    const std::array<AuthoringChoice, 3u> vertical{AuthoringChoice{"Top", L("Top")},
                                                   AuthoringChoice{"Center", L("Center")},
                                                   AuthoringChoice{"Bottom", L("Bottom")}};
    int verticalIndex = static_cast<int>(verticalAlign_);
    if (builder.Choice({"verticalAlign", L("Vertical Align"), "Vertical text alignment."}, verticalIndex, vertical)) {
        verticalAlign_ = static_cast<GameProjectUiVerticalAlign>(std::clamp(verticalIndex, 0, 2));
    }
    (void)builder.Int({"orderOffset", L("Order Offset"), "Additional ordering relative to the UI Rect."}, orderOffset_);
    orderOffset_ = std::clamp(orderOffset_, -kUiElementOrderLimit, kUiElementOrderLimit);
    if (!binding_.style.IsValid() || binding_.overrideEffects)
        style_.Inspect(builder);
    else
        style_.InspectContent(builder);
}

void UiButtonComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1},
           {"enabled", enabled_},
           {"interactable", interactable_},
           {"defaultFocus", defaultFocus_},
           {"navigationOrder", navigationOrder_},
           {"submitActionId", submitActionId_},
           {"onClickEventId", onClickEventId_},
           {"groupTarget", SCENE::SERIALIZATION::WriteSceneObjectReference(groupTarget_.value)},
           {"groupAction", groupAction_},
           {"normalTint", WriteColor(normalTint_)},
           {"transitionSeconds", transitionSeconds_},
           {"hoverTint", WriteColor(hoverTint_)},
           {"pressedTint", WriteColor(pressedTint_)},
           {"disabledTint", WriteColor(disabledTint_)}};
}

void UiButtonComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    interactable_ = JSONREAD::BooleanMemberOr(in, "interactable", true);
    defaultFocus_ = JSONREAD::BooleanMemberOr(in, "defaultFocus", false);
    navigationOrder_ = JSONREAD::IntegerMemberOr(in, "navigationOrder", 0);
    submitActionId_ = JSONREAD::StringMemberOr(in, "submitActionId", "Gameplay.Interact");
    onClickEventId_ = JSONREAD::StringMemberOr(in, "onClickEventId", {});
    groupTarget_ = {};
    if (const auto* v = JSONREAD::FindMember(in, "groupTarget"))
        groupTarget_.value = SCENE::SERIALIZATION::SceneObjectReferenceOr(*v);
    groupAction_ = std::clamp(JSONREAD::IntegerMemberOr(in, "groupAction", 0), 0, 3);
    normalTint_ = ReadColorMember(in, "normalTint", {});
    hoverTint_ = ReadColorMember(in, "hoverTint", {0.72f, 0.88f, 1.0f, 1.0f});
    pressedTint_ = ReadColorMember(in, "pressedTint", {0.45f, 0.72f, 1.0f, 1.0f});
    disabledTint_ = ReadColorMember(in, "disabledTint", {0.38f, 0.38f, 0.42f, 0.65f});
    visualState_ = UiButtonVisualState::Normal;
    transitionSeconds_ =
        std::clamp(FiniteOr(JSONREAD::NumberMemberOr(in, "transitionSeconds", 0.1f), 0.1f), 0.0f, 2.0f);
}

void UiButtonComponent::BuildInspector(IInspectorBuilder& builder) {
    const std::array<AuthoringChoice, 4> groupActions{{{"none", L("uix.actionNone")},
                                                       {"show", L("uix.actionShow")},
                                                       {"hide", L("uix.actionHide")},
                                                       {"toggle", L("uix.actionToggle")}}};
    builder.Choice(AuthoringFieldDescriptor{"groupAction", L("uix.groupAction")}, groupAction_, groupActions);
    builder.SceneObjectIdPicker(AuthoringFieldDescriptor{"groupTarget", L("uix.groupTarget")}, groupTarget_);
    if (!builder.Section(L("UI Button"), true))
        return;
    (void)builder.Bool({"enabled", L("Enabled"), "Enables button state and activation."}, enabled_);
    (void)builder.Bool({"interactable", L("Interactable"), "Allows pointer and submit-action activation."},
                       interactable_);
    (void)builder.Bool(
        {"defaultFocus", L("Default Focus"), "Selects this button when the Canvas first becomes active."},
        defaultFocus_);
    (void)builder.Int({"navigationOrder", L("Navigation Order"), "Keyboard/gamepad focus order."}, navigationOrder_);
    (void)builder.InputActionIdPicker(
        {"submitActionId", L("Submit Action"), "Button action used while this element has focus."},
        INPUT::InputActionValueType::Button, submitActionId_);
    (void)builder.RuntimeEventIdPicker(
        {"onClickEventId", L("On Click Event"), "Optional no-payload Runtime Binding event."},
        AuthoringEventPayloadPolicy::None, BINDING::ValueType::Bool, onClickEventId_);
    if (builder.Section(L("Button Colors"), false)) {
        builder.FloatRange(AuthoringFieldDescriptor{"transitionSeconds", L("State transition seconds")},
                           transitionSeconds_, 0, 2);
        InspectColor(builder, "normalTint", L("Normal"), normalTint_);
        InspectColor(builder, "hoverTint", L("Hover"), hoverTint_);
        InspectColor(builder, "pressedTint", L("Pressed"), pressedTint_);
        InspectColor(builder, "disabledTint", L("Disabled"), disabledTint_);
    }
}

void UiButtonComponent::ApplyGroupAction(World& world) const {
    if (groupAction_ == 0)
        return;
    if (auto* object = world.FindObject(groupTarget_))
        if (auto* group = object->GetComponent<UiGroupComponent>()) {
            auto* animations = world.Services().Find<UiAnimationPlayer>();
            const bool visible =
                groupAction_ == 1 ||
                (groupAction_ == 3 &&
                 (!group->IsVisible() || (animations && animations->IsClosing(object->GetRuntimeHandle()))));
            if (animations)
                animations->SetVisible(world, *object, visible);
            else
                group->SetVisible(visible);
        }
}

const GameProjectUiColor& UiButtonComponent::GetStateTint() const noexcept {
    switch (visualState_) {
    case UiButtonVisualState::Hovered: return hoverTint_;
    case UiButtonVisualState::Pressed: return pressedTint_;
    case UiButtonVisualState::Disabled: return disabledTint_;
    case UiButtonVisualState::Normal:
    default: return normalTint_;
    }
}

} // namespace HIKARI
