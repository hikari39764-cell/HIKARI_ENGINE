#include "Scene/Ui/HIKARI_UiSceneComponents.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_ComponentAuthoringTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"

namespace HIKARI {
    namespace {

        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        MATH::Vec2 ReadVec2Member(
            const nlohmann::json& source,
            const char* key,
            MATH::Vec2 fallback) {
            const nlohmann::json* node = JSONREAD::FindMember(source, key);
            return node != nullptr ? JSONREAD::Vec2Or(*node, fallback) : fallback;
        }

        GameProjectUiColor ReadColorMember(
            const nlohmann::json& source,
            const char* key,
            GameProjectUiColor fallback) {
            const nlohmann::json* node = JSONREAD::FindMember(source, key);
            if (node == nullptr) {
                return fallback;
            }
            const MATH::Vec4 value = JSONREAD::Vec4Or(
                *node,
                { fallback.r, fallback.g, fallback.b, fallback.a });
            return {
                FiniteOr(value.x, fallback.r),
                FiniteOr(value.y, fallback.g),
                FiniteOr(value.z, fallback.b),
                FiniteOr(value.w, fallback.a)
            };
        }

        nlohmann::json WriteVec2(const MATH::Vec2& value) {
            return nlohmann::json::array({ value.x, value.y });
        }

        nlohmann::json WriteColor(const GameProjectUiColor& value) {
            return nlohmann::json::array({
                value.r, value.g, value.b, value.a });
        }

        void InspectColor(
            IInspectorBuilder& builder,
            std::string_view idPrefix,
            std::string_view label,
            GameProjectUiColor& color) {
            const std::string colorId = std::string(idPrefix) + ".rgb";
            const std::string alphaId = std::string(idPrefix) + ".alpha";
            (void)builder.Color3(
                AuthoringFieldDescriptor{
                    colorId,
                    label,
                    "Linear RGBA color multiplier." },
                color.r,
                color.g,
                color.b);
            (void)builder.FloatRange(
                AuthoringFieldDescriptor{
                    alphaId,
                    std::string(label) + " Alpha",
                    "Opacity multiplier." },
                color.a,
                0.0f,
                1.0f,
                0.01f);
        }

        const char* HorizontalAlignName(
            GameProjectUiTextAlign value) noexcept {
            switch (value) {
            case GameProjectUiTextAlign::Center: return "Center";
            case GameProjectUiTextAlign::Right: return "Right";
            case GameProjectUiTextAlign::Left:
            default: return "Left";
            }
        }

        GameProjectUiTextAlign ParseHorizontalAlign(
            std::string_view value) noexcept {
            if (value == "Center") return GameProjectUiTextAlign::Center;
            if (value == "Right") return GameProjectUiTextAlign::Right;
            return GameProjectUiTextAlign::Left;
        }

        const char* VerticalAlignName(
            GameProjectUiVerticalAlign value) noexcept {
            switch (value) {
            case GameProjectUiVerticalAlign::Center: return "Center";
            case GameProjectUiVerticalAlign::Bottom: return "Bottom";
            case GameProjectUiVerticalAlign::Top:
            default: return "Top";
            }
        }

        GameProjectUiVerticalAlign ParseVerticalAlign(
            std::string_view value) noexcept {
            if (value == "Center") return GameProjectUiVerticalAlign::Center;
            if (value == "Bottom") return GameProjectUiVerticalAlign::Bottom;
            return GameProjectUiVerticalAlign::Top;
        }

    } // namespace

    void UiCanvasComponent::Serialize(nlohmann::json& out) const {
        out = {
            { "version", 1 },
            { "enabled", enabled_ },
            { "interactable", interactable_ },
            { "referenceWidth", referenceWidth_ },
            { "referenceHeight", referenceHeight_ },
            { "sortOrder", sortOrder_ }
        };
    }

    void UiCanvasComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
        interactable_ = JSONREAD::BooleanMemberOr(
            in, "interactable", true);
        referenceWidth_ = std::clamp(
            FiniteOr(JSONREAD::NumberMemberOr(
                in, "referenceWidth", 1920.0f), 1920.0f),
            1.0f,
            16384.0f);
        referenceHeight_ = std::clamp(
            FiniteOr(JSONREAD::NumberMemberOr(
                in, "referenceHeight", 1080.0f), 1080.0f),
            1.0f,
            16384.0f);
        sortOrder_ = JSONREAD::IntegerMemberOr(in, "sortOrder", 0);
        sortOrder_ = std::clamp(
            sortOrder_,
            -kUiCanvasSortOrderLimit,
            kUiCanvasSortOrderLimit);
    }

    void UiCanvasComponent::BuildInspector(IInspectorBuilder& builder) {
        if (!builder.Section("UI Canvas", true)) return;
        (void)builder.Bool({ "enabled", "Enabled",
            "Enables this Canvas and every child UI element." }, enabled_);
        (void)builder.Bool({ "interactable", "Interactable",
            "Allows child buttons to receive pointer and action input." },
            interactable_);
        (void)builder.FloatRange({ "referenceWidth", "Reference Width",
            "Authored logical width before aspect-preserving fit." },
            referenceWidth_, 1.0f, 16384.0f, 1.0f);
        (void)builder.FloatRange({ "referenceHeight", "Reference Height",
            "Authored logical height before aspect-preserving fit." },
            referenceHeight_, 1.0f, 16384.0f, 1.0f);
        (void)builder.Int({ "sortOrder", "Sort Order",
            "Canvas ordering; larger values render and interact on top." },
            sortOrder_);
        sortOrder_ = std::clamp(
            sortOrder_,
            -kUiCanvasSortOrderLimit,
            kUiCanvasSortOrderLimit);
    }

    void UiRectComponent::Serialize(nlohmann::json& out) const {
        out = {
            { "version", 1 }, { "enabled", enabled_ },
            { "anchorMinimum", WriteVec2(anchorMinimum_) },
            { "anchorMaximum", WriteVec2(anchorMaximum_) },
            { "offsetMinimum", WriteVec2(offsetMinimum_) },
            { "offsetMaximum", WriteVec2(offsetMaximum_) },
            { "pivot", WriteVec2(pivot_) },
            { "rotationDegrees", rotationDegrees_ },
            { "order", order_ }
        };
    }

    void UiRectComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
        anchorMinimum_ = ReadVec2Member(
            in, "anchorMinimum", { 0.5f, 0.5f });
        anchorMaximum_ = ReadVec2Member(
            in, "anchorMaximum", { 0.5f, 0.5f });
        offsetMinimum_ = ReadVec2Member(
            in, "offsetMinimum", { -160.0f, -40.0f });
        offsetMaximum_ = ReadVec2Member(
            in, "offsetMaximum", { 160.0f, 40.0f });
        pivot_ = ReadVec2Member(in, "pivot", { 0.5f, 0.5f });
        rotationDegrees_ = FiniteOr(JSONREAD::NumberMemberOr(
            in, "rotationDegrees", 0.0f), 0.0f);
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
        order_ = std::clamp(
            order_, -kUiElementOrderLimit, kUiElementOrderLimit);
    }

    void UiRectComponent::BuildInspector(IInspectorBuilder& builder) {
        if (!builder.Section("UI Rect", true)) return;
        (void)builder.Bool({ "enabled", "Enabled",
            "Enables layout, rendering and hit testing for this element." },
            enabled_);
        constexpr std::array<AuthoringChoice, 11u> presets{
            AuthoringChoice{ "Custom", "Custom" },
            AuthoringChoice{ "TopLeft", "Top Left" },
            AuthoringChoice{ "Top", "Top" },
            AuthoringChoice{ "TopRight", "Top Right" },
            AuthoringChoice{ "Left", "Left" },
            AuthoringChoice{ "Center", "Center" },
            AuthoringChoice{ "Right", "Right" },
            AuthoringChoice{ "BottomLeft", "Bottom Left" },
            AuthoringChoice{ "Bottom", "Bottom" },
            AuthoringChoice{ "BottomRight", "Bottom Right" },
            AuthoringChoice{ "Stretch", "Stretch" }
        };
        int preset = 0;
        if (anchorMinimum_.x == anchorMaximum_.x &&
            anchorMinimum_.y == anchorMaximum_.y) {
            const int column = anchorMinimum_.x <= 0.0f
                ? 0 : anchorMinimum_.x >= 1.0f ? 2 : 1;
            const int row = anchorMinimum_.y <= 0.0f
                ? 0 : anchorMinimum_.y >= 1.0f ? 2 : 1;
            preset = 1 + row * 3 + column;
        } else if (anchorMinimum_.x == 0.0f &&
            anchorMinimum_.y == 0.0f &&
            anchorMaximum_.x == 1.0f &&
            anchorMaximum_.y == 1.0f) {
            preset = 10;
        }
        if (builder.Choice({ "anchorPreset", "Anchor Preset",
                "Common anchor placement; Custom preserves exact values." },
                preset, presets) && preset > 0) {
            if (preset == 10) {
                anchorMinimum_ = { 0.0f, 0.0f };
                anchorMaximum_ = { 1.0f, 1.0f };
            } else {
                const int index = preset - 1;
                const float x = static_cast<float>(index % 3) * 0.5f;
                const float y = static_cast<float>(index / 3) * 0.5f;
                anchorMinimum_ = { x, y };
                anchorMaximum_ = anchorMinimum_;
            }
        }
        (void)builder.Vec2({ "anchorMinimum", "Anchor Minimum",
            "Normalized minimum anchor in the parent Rect or Canvas." },
            anchorMinimum_.x, anchorMinimum_.y);
        (void)builder.Vec2({ "anchorMaximum", "Anchor Maximum",
            "Normalized maximum anchor in the parent Rect or Canvas." },
            anchorMaximum_.x, anchorMaximum_.y);
        (void)builder.Vec2({ "offsetMinimum", "Offset Minimum",
            "Pixel offset from the minimum anchor." },
            offsetMinimum_.x, offsetMinimum_.y);
        (void)builder.Vec2({ "offsetMaximum", "Offset Maximum",
            "Pixel offset from the maximum anchor." },
            offsetMaximum_.x, offsetMaximum_.y);
        (void)builder.Vec2({ "pivot", "Pivot",
            "Normalized rotation and alignment pivot." }, pivot_.x, pivot_.y);
        (void)builder.Float({ "rotationDegrees", "Rotation",
            "Clockwise rotation in degrees." }, rotationDegrees_);
        (void)builder.Int({ "order", "Order",
            "Element ordering inside its Canvas." }, order_);
        Sanitize();
    }

    void UiImageComponent::Serialize(nlohmann::json& out) const {
        out = {
            { "version", 1 }, { "enabled", enabled_ },
            { "textureAssetGuid", textureAssetGuid_.value },
            { "textureAssetPath", textureAssetPath_ },
            { "tint", WriteColor(tint_) },
            { "orderOffset", orderOffset_ }
        };
    }

    void UiImageComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
        textureAssetGuid_.value = JSONREAD::StringMemberOr(
            in, "textureAssetGuid", {});
        textureAssetPath_ = JSONREAD::StringMemberOr(
            in, "textureAssetPath", "generated://solid/ffffffff");
        tint_ = ReadColorMember(
            in, "tint", { 0.08f, 0.12f, 0.20f, 0.96f });
        orderOffset_ = JSONREAD::IntegerMemberOr(in, "orderOffset", 0);
        orderOffset_ = std::clamp(
            orderOffset_, -kUiElementOrderLimit, kUiElementOrderLimit);
    }

    void UiImageComponent::BuildInspector(IInspectorBuilder& builder) {
        if (!builder.Section("UI Image", true)) return;
        (void)builder.Bool({ "enabled", "Enabled",
            "Submits this image to the active Canvas." }, enabled_);
        (void)builder.AssetIdPicker(
            { "textureAssetGuid", "Texture", "Texture asset GUID." },
            AssetType::Texture,
            textureAssetGuid_.value);
        (void)builder.String(
            { "textureAssetPath", "Fallback Texture",
              "Project-relative path or generated://solid/RRGGBBAA." },
            textureAssetPath_);
        InspectColor(builder, "tint", "Tint", tint_);
        (void)builder.Int({ "orderOffset", "Order Offset",
            "Additional ordering relative to the UI Rect." }, orderOffset_);
        orderOffset_ = std::clamp(
            orderOffset_, -kUiElementOrderLimit, kUiElementOrderLimit);
    }

    void UiTextComponent::Serialize(nlohmann::json& out) const {
        out = {
            { "version", 1 }, { "enabled", enabled_ },
            { "text", text_ }, { "fontAssetPath", fontAssetPath_ },
            { "pixelHeight", pixelHeight_ }, { "color", WriteColor(color_) },
            { "horizontalAlign", HorizontalAlignName(horizontalAlign_) },
            { "verticalAlign", VerticalAlignName(verticalAlign_) },
            { "orderOffset", orderOffset_ }
        };
    }

    void UiTextComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
        text_ = JSONREAD::StringMemberOr(in, "text", "Button");
        fontAssetPath_ = JSONREAD::StringMemberOr(
            in, "fontAssetPath", {});
        pixelHeight_ = std::clamp(FiniteOr(JSONREAD::NumberMemberOr(
            in, "pixelHeight", 34.0f), 34.0f), 4.0f, 512.0f);
        color_ = ReadColorMember(in, "color", {});
        horizontalAlign_ = ParseHorizontalAlign(JSONREAD::StringMemberOr(
            in, "horizontalAlign", "Center"));
        verticalAlign_ = ParseVerticalAlign(JSONREAD::StringMemberOr(
            in, "verticalAlign", "Center"));
        orderOffset_ = JSONREAD::IntegerMemberOr(in, "orderOffset", 1);
        orderOffset_ = std::clamp(
            orderOffset_, -kUiElementOrderLimit, kUiElementOrderLimit);
    }

    void UiTextComponent::BuildInspector(IInspectorBuilder& builder) {
        if (!builder.Section("UI Text", true)) return;
        (void)builder.Bool({ "enabled", "Enabled",
            "Submits this text to the active Canvas." }, enabled_);
        (void)builder.String({ "text", "Text", "UTF-8 display text." }, text_);
        (void)builder.String({ "fontAssetPath", "Font Path",
            "TTF, OTF or TTC path, e.g. Fonts/RobotoMono.ttf; an Assets/ prefix is also accepted." },
            fontAssetPath_);
        if (fontAssetPath_.empty()) {
            builder.Text(
                "Assign a project font path to render this text. The Button "
                "image remains usable without text.");
        }
        (void)builder.FloatRange({ "pixelHeight", "Pixel Height",
            "Font size in reference-canvas pixels." },
            pixelHeight_, 4.0f, 512.0f, 1.0f);
        InspectColor(builder, "color", "Color", color_);
        constexpr std::array<AuthoringChoice, 3u> horizontal{
            AuthoringChoice{ "Left", "Left" },
            AuthoringChoice{ "Center", "Center" },
            AuthoringChoice{ "Right", "Right" }
        };
        int horizontalIndex = static_cast<int>(horizontalAlign_);
        if (builder.Choice({ "horizontalAlign", "Horizontal Align",
                "Horizontal text alignment." }, horizontalIndex, horizontal)) {
            horizontalAlign_ = static_cast<GameProjectUiTextAlign>(
                std::clamp(horizontalIndex, 0, 2));
        }
        constexpr std::array<AuthoringChoice, 3u> vertical{
            AuthoringChoice{ "Top", "Top" },
            AuthoringChoice{ "Center", "Center" },
            AuthoringChoice{ "Bottom", "Bottom" }
        };
        int verticalIndex = static_cast<int>(verticalAlign_);
        if (builder.Choice({ "verticalAlign", "Vertical Align",
                "Vertical text alignment." }, verticalIndex, vertical)) {
            verticalAlign_ = static_cast<GameProjectUiVerticalAlign>(
                std::clamp(verticalIndex, 0, 2));
        }
        (void)builder.Int({ "orderOffset", "Order Offset",
            "Additional ordering relative to the UI Rect." }, orderOffset_);
        orderOffset_ = std::clamp(
            orderOffset_, -kUiElementOrderLimit, kUiElementOrderLimit);
    }

    void UiButtonComponent::Serialize(nlohmann::json& out) const {
        out = {
            { "version", 1 }, { "enabled", enabled_ },
            { "interactable", interactable_ },
            { "defaultFocus", defaultFocus_ },
            { "navigationOrder", navigationOrder_ },
            { "submitActionId", submitActionId_ },
            { "onClickEventId", onClickEventId_ },
            { "normalTint", WriteColor(normalTint_) },
            { "transitionSeconds", transitionSeconds_ },
            { "hoverTint", WriteColor(hoverTint_) },
            { "pressedTint", WriteColor(pressedTint_) },
            { "disabledTint", WriteColor(disabledTint_) }
        };
    }

    void UiButtonComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
        interactable_ = JSONREAD::BooleanMemberOr(in, "interactable", true);
        defaultFocus_ = JSONREAD::BooleanMemberOr(in, "defaultFocus", false);
        navigationOrder_ = JSONREAD::IntegerMemberOr(
            in, "navigationOrder", 0);
        submitActionId_ = JSONREAD::StringMemberOr(
            in, "submitActionId", "Gameplay.Interact");
        onClickEventId_ = JSONREAD::StringMemberOr(in, "onClickEventId", {});
        normalTint_ = ReadColorMember(in, "normalTint", {});
        hoverTint_ = ReadColorMember(
            in, "hoverTint", { 0.72f, 0.88f, 1.0f, 1.0f });
        pressedTint_ = ReadColorMember(
            in, "pressedTint", { 0.45f, 0.72f, 1.0f, 1.0f });
        disabledTint_ = ReadColorMember(
            in, "disabledTint", { 0.38f, 0.38f, 0.42f, 0.65f });
        visualState_ = UiButtonVisualState::Normal;
        animatedTint_ = transitionFrom_ = normalTint_;
        transitionElapsed_ = 0;
        transitionSeconds_ = std::clamp(FiniteOr(JSONREAD::NumberMemberOr(
            in, "transitionSeconds", 0.1f), 0.1f), 0.0f, 2.0f);
    }

    void UiButtonComponent::BuildInspector(IInspectorBuilder& builder) {
        if (!builder.Section("UI Button", true)) return;
        (void)builder.Bool({ "enabled", "Enabled",
            "Enables button state and activation." }, enabled_);
        (void)builder.Bool({ "interactable", "Interactable",
            "Allows pointer and submit-action activation." }, interactable_);
        (void)builder.Bool({ "defaultFocus", "Default Focus",
            "Selects this button when the Canvas first becomes active." },
            defaultFocus_);
        (void)builder.Int({ "navigationOrder", "Navigation Order",
            "Keyboard/gamepad focus order." }, navigationOrder_);
        (void)builder.InputActionIdPicker(
            { "submitActionId", "Submit Action",
              "Button action used while this element has focus." },
            INPUT::InputActionValueType::Button,
            submitActionId_);
        (void)builder.RuntimeEventIdPicker(
            { "onClickEventId", "On Click Event",
              "Optional no-payload Runtime Binding event." },
            AuthoringEventPayloadPolicy::None,
            BINDING::ValueType::Bool,
            onClickEventId_);
        if (builder.Section("Button Colors", false)) {
            builder.FloatRange("State transition seconds", transitionSeconds_, 0, 2);
            InspectColor(builder, "normalTint", "Normal", normalTint_);
            InspectColor(builder, "hoverTint", "Hover", hoverTint_);
            InspectColor(builder, "pressedTint", "Pressed", pressedTint_);
            InspectColor(builder, "disabledTint", "Disabled", disabledTint_);
        }
    }

    void UiButtonComponent::AdvanceVisual(float dt) noexcept {
        if (!std::isfinite(dt) || dt < 0) return;
        transitionElapsed_ = std::min(transitionElapsed_ + dt, transitionSeconds_);
        const float t = transitionSeconds_ > 0 ? transitionElapsed_ / transitionSeconds_ : 1;
        const float eased = t * t * (3 - 2 * t);
        const auto& target = GetStateTint();
        animatedTint_ = {std::lerp(transitionFrom_.r,target.r,eased), std::lerp(transitionFrom_.g,target.g,eased),
            std::lerp(transitionFrom_.b,target.b,eased),std::lerp(transitionFrom_.a,target.a,eased)};
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
