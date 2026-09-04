#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
    namespace {
        constexpr size_t kMaximumFontAssetPathBytes = 1024u;

        float ClampFinite(
            float value,
            float fallback,
            float minimum,
            float maximum) noexcept {
            return std::clamp(
                std::isfinite(value) ? value : fallback,
                minimum,
                maximum);
        }

        MATH::Vec2 SanitizeSize(MATH::Vec2 value) noexcept {
            value.x = ClampFinite(value.x, 4.0f, 0.001f, 100000.0f);
            value.y = ClampFinite(value.y, 1.0f, 0.001f, 100000.0f);
            return value;
        }

        MATH::Vec2 SanitizePivot(MATH::Vec2 value) noexcept {
            value.x = ClampFinite(value.x, 0.5f, 0.0f, 1.0f);
            value.y = ClampFinite(value.y, 0.5f, 0.0f, 1.0f);
            return value;
        }

        MATH::Vec3 SanitizeColor(
            MATH::Vec3 value,
            MATH::Vec3 fallback) noexcept {
            value.x = ClampFinite(value.x, fallback.x, 0.0f, 16.0f);
            value.y = ClampFinite(value.y, fallback.y, 0.0f, 16.0f);
            value.z = ClampFinite(value.z, fallback.z, 0.0f, 16.0f);
            return value;
        }

        const char* FacingName(
            WorldTextComponent::FacingMode value) noexcept {
            switch (value) {
            case WorldTextComponent::FacingMode::Fixed:
                return "Fixed";
            case WorldTextComponent::FacingMode::FixedYAxis:
                return "FixedYAxis";
            case WorldTextComponent::FacingMode::CameraFacing:
            default:
                return "CameraFacing";
            }
        }

        WorldTextComponent::FacingMode ParseFacing(
            std::string_view value) noexcept {
            if (value == "Fixed") {
                return WorldTextComponent::FacingMode::Fixed;
            }
            if (value == "FixedYAxis") {
                return WorldTextComponent::FacingMode::FixedYAxis;
            }
            return WorldTextComponent::FacingMode::CameraFacing;
        }

        const char* HorizontalAlignName(
            WorldTextComponent::HorizontalAlign value) noexcept {
            switch (value) {
            case WorldTextComponent::HorizontalAlign::Center:
                return "Center";
            case WorldTextComponent::HorizontalAlign::Right:
                return "Right";
            case WorldTextComponent::HorizontalAlign::Left:
            default:
                return "Left";
            }
        }

        WorldTextComponent::HorizontalAlign ParseHorizontalAlign(
            std::string_view value) noexcept {
            if (value == "Center") {
                return WorldTextComponent::HorizontalAlign::Center;
            }
            if (value == "Right") {
                return WorldTextComponent::HorizontalAlign::Right;
            }
            return WorldTextComponent::HorizontalAlign::Left;
        }

        const char* VerticalAlignName(
            WorldTextComponent::VerticalAlign value) noexcept {
            switch (value) {
            case WorldTextComponent::VerticalAlign::Center:
                return "Center";
            case WorldTextComponent::VerticalAlign::Bottom:
                return "Bottom";
            case WorldTextComponent::VerticalAlign::Top:
            default:
                return "Top";
            }
        }

        WorldTextComponent::VerticalAlign ParseVerticalAlign(
            std::string_view value) noexcept {
            if (value == "Center") {
                return WorldTextComponent::VerticalAlign::Center;
            }
            if (value == "Bottom") {
                return WorldTextComponent::VerticalAlign::Bottom;
            }
            return WorldTextComponent::VerticalAlign::Top;
        }

        const char* WrapName(
            WorldTextComponent::WrapMode value) noexcept {
            return value == WorldTextComponent::WrapMode::Character
                ? "Character"
                : "None";
        }

        WorldTextComponent::WrapMode ParseWrap(
            std::string_view value) noexcept {
            return value == "Character"
                ? WorldTextComponent::WrapMode::Character
                : WorldTextComponent::WrapMode::None;
        }

        nlohmann::json WriteVec2(const MATH::Vec2& value) {
            return nlohmann::json::array({ value.x, value.y });
        }

        nlohmann::json WriteVec3(const MATH::Vec3& value) {
            return nlohmann::json::array({ value.x, value.y, value.z });
        }

        AuthoringFieldDescriptor Field(
            std::string_view id,
            std::string_view label,
            std::string_view help) noexcept {
            return { id, label, help };
        }
    } // namespace

    void WorldTextComponent::SetText(std::string text) {
        TEXT::TruncateUtf8(
            text,
            TEXT_RENDERING::kSdfTextMaximumUtf8Bytes);
        text_ = std::move(text);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetFontAssetPath(std::string path) {
        TEXT::TruncateUtf8(path, kMaximumFontAssetPathBytes);
        fontAssetPath_ = std::move(path);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetVisible(bool visible) {
        visible_ = visible;
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetLayoutSize(MATH::Vec2 size) {
        layoutSize_ = SanitizeSize(size);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetFontHeight(float height) {
        fontHeight_ = ClampFinite(height, 0.5f, 0.001f, 10000.0f);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetHorizontalAlign(HorizontalAlign align) {
        horizontalAlign_ = static_cast<uint32_t>(align) <=
                static_cast<uint32_t>(HorizontalAlign::Right)
            ? align
            : HorizontalAlign::Left;
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetVerticalAlign(VerticalAlign align) {
        verticalAlign_ = static_cast<uint32_t>(align) <=
                static_cast<uint32_t>(VerticalAlign::Bottom)
            ? align
            : VerticalAlign::Top;
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetWrapMode(WrapMode wrap) {
        wrap_ = static_cast<uint32_t>(wrap) <=
                static_cast<uint32_t>(WrapMode::Character)
            ? wrap
            : WrapMode::None;
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetLineHeightMultiplier(float multiplier) {
        lineHeightMultiplier_ = ClampFinite(
            multiplier, 1.2f, 0.25f, 4.0f);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetLetterSpacing(float spacing) {
        letterSpacing_ = ClampFinite(
            spacing, 0.0f, -10000.0f, 10000.0f);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetFacingMode(FacingMode facing) {
        facing_ = static_cast<uint32_t>(facing) <=
                static_cast<uint32_t>(FacingMode::FixedYAxis)
            ? facing
            : FacingMode::CameraFacing;
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetColor(MATH::Vec3 color) {
        color_ = SanitizeColor(color, { 1.0f, 1.0f, 1.0f });
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetOutlineColor(MATH::Vec3 color) {
        outlineColor_ = SanitizeColor(color, {});
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetOutlineWidth(float width) {
        outlineWidth_ = ClampFinite(width, 0.04f, 0.0f, 0.25f);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetOpacity(float opacity) {
        opacity_ = ClampFinite(opacity, 1.0f, 0.0f, 1.0f);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetRevealAmount(float amount) {
        revealAmount_ = ClampFinite(amount, 1.0f, 0.0f, 1.0f);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetPivot(MATH::Vec2 pivot) {
        pivot_ = SanitizePivot(pivot);
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::SetTransparentSortPriority(int priority) {
        transparentSortPriority_ = std::clamp(priority, -32768, 32767);
        NotifyRenderStateDirty();
    }

    bool WorldTextComponent::QueryGeometryFit(
        GeometryFitDesc& outFit) const noexcept {
        const MATH::Vec2 size = SanitizeSize(layoutSize_);
        outFit = {};
        outFit.shape = GeometryFitShape::Box;
        outFit.center = {
            (0.5f - pivot_.x) * size.x,
            (0.5f - pivot_.y) * size.y,
            0.0f
        };
        outFit.size = { size.x, size.y, 0.02f };
        outFit.radius = 0.01f;
        outFit.height = size.y;
        return true;
    }

    void WorldTextComponent::Serialize(nlohmann::json& out) const {
        out = {
            { "version", 1u },
            { WorldTextComponentPropertyIds::Text, text_ },
            { WorldTextComponentPropertyIds::FontAssetPath,
              fontAssetPath_ },
            { WorldTextComponentPropertyIds::Visible, visible_ },
            { WorldTextComponentPropertyIds::LayoutSize,
              WriteVec2(layoutSize_) },
            { WorldTextComponentPropertyIds::FontHeight, fontHeight_ },
            { WorldTextComponentPropertyIds::HorizontalAlign,
              HorizontalAlignName(horizontalAlign_) },
            { WorldTextComponentPropertyIds::VerticalAlign,
              VerticalAlignName(verticalAlign_) },
            { WorldTextComponentPropertyIds::Wrap, WrapName(wrap_) },
            { WorldTextComponentPropertyIds::LineHeightMultiplier,
              lineHeightMultiplier_ },
            { WorldTextComponentPropertyIds::LetterSpacing,
              letterSpacing_ },
            { WorldTextComponentPropertyIds::Facing,
              FacingName(facing_) },
            { WorldTextComponentPropertyIds::Color,
              WriteVec3(color_) },
            { WorldTextComponentPropertyIds::OutlineColor,
              WriteVec3(outlineColor_) },
            { WorldTextComponentPropertyIds::OutlineWidth,
              outlineWidth_ },
            { WorldTextComponentPropertyIds::Opacity, opacity_ },
            { WorldTextComponentPropertyIds::RevealAmount,
              revealAmount_ },
            { WorldTextComponentPropertyIds::Pivot,
              WriteVec2(pivot_) },
            { WorldTextComponentPropertyIds::TransparentSortPriority,
              transparentSortPriority_ }
        };
    }

    void WorldTextComponent::Deserialize(const nlohmann::json& in) {
        text_ = JSONREAD::StringMemberOr(
            in, WorldTextComponentPropertyIds::Text, "World Text");
        fontAssetPath_ = JSONREAD::StringMemberOr(
            in,
            WorldTextComponentPropertyIds::FontAssetPath,
            "Fonts/NotoSansCJKjp-Regular.otf");
        visible_ = JSONREAD::BooleanMemberOr(
            in, WorldTextComponentPropertyIds::Visible, true);
        if (const nlohmann::json* value = JSONREAD::FindMember(
                in, WorldTextComponentPropertyIds::LayoutSize)) {
            layoutSize_ = JSONREAD::Vec2Or(*value, layoutSize_);
        }
        fontHeight_ = JSONREAD::NumberMemberOr(
            in, WorldTextComponentPropertyIds::FontHeight, 0.5f);
        horizontalAlign_ = ParseHorizontalAlign(JSONREAD::StringMemberOr(
            in,
            WorldTextComponentPropertyIds::HorizontalAlign,
            "Center"));
        verticalAlign_ = ParseVerticalAlign(JSONREAD::StringMemberOr(
            in,
            WorldTextComponentPropertyIds::VerticalAlign,
            "Center"));
        wrap_ = ParseWrap(JSONREAD::StringMemberOr(
            in, WorldTextComponentPropertyIds::Wrap, "Character"));
        lineHeightMultiplier_ = JSONREAD::NumberMemberOr(
            in,
            WorldTextComponentPropertyIds::LineHeightMultiplier,
            1.2f);
        letterSpacing_ = JSONREAD::NumberMemberOr(
            in, WorldTextComponentPropertyIds::LetterSpacing, 0.0f);
        facing_ = ParseFacing(JSONREAD::StringMemberOr(
            in, WorldTextComponentPropertyIds::Facing, "CameraFacing"));
        if (const nlohmann::json* value = JSONREAD::FindMember(
                in, WorldTextComponentPropertyIds::Color)) {
            color_ = JSONREAD::Vec3Or(*value, color_);
        }
        if (const nlohmann::json* value = JSONREAD::FindMember(
                in, WorldTextComponentPropertyIds::OutlineColor)) {
            outlineColor_ = JSONREAD::Vec3Or(*value, outlineColor_);
        }
        outlineWidth_ = JSONREAD::NumberMemberOr(
            in, WorldTextComponentPropertyIds::OutlineWidth, 0.04f);
        opacity_ = JSONREAD::NumberMemberOr(
            in, WorldTextComponentPropertyIds::Opacity, 1.0f);
        revealAmount_ = JSONREAD::NumberMemberOr(
            in, WorldTextComponentPropertyIds::RevealAmount, 1.0f);
        if (const nlohmann::json* value = JSONREAD::FindMember(
                in, WorldTextComponentPropertyIds::Pivot)) {
            pivot_ = JSONREAD::Vec2Or(*value, pivot_);
        }
        transparentSortPriority_ = JSONREAD::IntegerMemberOr(
            in,
            WorldTextComponentPropertyIds::TransparentSortPriority,
            0);
        Sanitize();
        NotifyRenderStateDirty();
    }

    void WorldTextComponent::BuildInspector(IInspectorBuilder& builder) {
        bool changed = false;
        if (builder.Section(
                TEXT::Localize("ui.inspector.worldText.content"), true)) {
            changed = builder.Bool(
                Field(
                    WorldTextComponentPropertyIds::Visible,
                    TEXT::Localize("ui.inspector.worldText.visible"),
                    TEXT::Localize("ui.inspector.worldText.visibleHelp")),
                visible_) || changed;
            changed = builder.String(
                Field(
                    WorldTextComponentPropertyIds::Text,
                    TEXT::Localize("ui.inspector.worldText.text"),
                    TEXT::Localize("ui.inspector.worldText.textHelp")),
                text_) || changed;
            changed = builder.String(
                Field(
                    WorldTextComponentPropertyIds::FontAssetPath,
                    TEXT::Localize("ui.inspector.worldText.font"),
                    TEXT::Localize("ui.inspector.worldText.fontHelp")),
                fontAssetPath_) || changed;
        }

        if (builder.Section(
                TEXT::Localize("ui.inspector.worldText.layout"), true)) {
            changed = builder.Vec2(
                Field(
                    WorldTextComponentPropertyIds::LayoutSize,
                    TEXT::Localize("ui.inspector.worldText.layoutSize"),
                    TEXT::Localize(
                        "ui.inspector.worldText.layoutSizeHelp")),
                layoutSize_.x,
                layoutSize_.y) || changed;
            changed = builder.FloatRange(
                Field(
                    WorldTextComponentPropertyIds::FontHeight,
                    TEXT::Localize("ui.inspector.worldText.fontHeight"),
                    TEXT::Localize(
                        "ui.inspector.worldText.fontHeightHelp")),
                fontHeight_,
                0.001f,
                10000.0f,
                0.01f) || changed;

            const std::array<std::string, 3u> horizontalLabels{
                TEXT::Localize("ui.inspector.worldText.left"),
                TEXT::Localize("ui.inspector.worldText.center"),
                TEXT::Localize("ui.inspector.worldText.right"),
            };
            const std::array horizontalChoices{
                AuthoringChoice{ "Left", horizontalLabels[0] },
                AuthoringChoice{ "Center", horizontalLabels[1] },
                AuthoringChoice{ "Right", horizontalLabels[2] },
            };
            int horizontal = static_cast<int>(horizontalAlign_);
            if (builder.Choice(
                    Field(
                        WorldTextComponentPropertyIds::HorizontalAlign,
                        TEXT::Localize(
                            "ui.inspector.worldText.horizontalAlign"),
                        TEXT::Localize(
                            "ui.inspector.worldText.horizontalAlignHelp")),
                    horizontal,
                    horizontalChoices)) {
                horizontalAlign_ = static_cast<HorizontalAlign>(
                    std::clamp(horizontal, 0, 2));
                changed = true;
            }

            const std::array<std::string, 3u> verticalLabels{
                TEXT::Localize("ui.inspector.worldText.top"),
                TEXT::Localize("ui.inspector.worldText.center"),
                TEXT::Localize("ui.inspector.worldText.bottom"),
            };
            const std::array verticalChoices{
                AuthoringChoice{ "Top", verticalLabels[0] },
                AuthoringChoice{ "Center", verticalLabels[1] },
                AuthoringChoice{ "Bottom", verticalLabels[2] },
            };
            int vertical = static_cast<int>(verticalAlign_);
            if (builder.Choice(
                    Field(
                        WorldTextComponentPropertyIds::VerticalAlign,
                        TEXT::Localize(
                            "ui.inspector.worldText.verticalAlign"),
                        TEXT::Localize(
                            "ui.inspector.worldText.verticalAlignHelp")),
                    vertical,
                    verticalChoices)) {
                verticalAlign_ = static_cast<VerticalAlign>(
                    std::clamp(vertical, 0, 2));
                changed = true;
            }

            const std::array<std::string, 2u> wrapLabels{
                TEXT::Localize("ui.inspector.worldText.wrapNone"),
                TEXT::Localize("ui.inspector.worldText.wrapCharacter"),
            };
            const std::array wrapChoices{
                AuthoringChoice{ "None", wrapLabels[0] },
                AuthoringChoice{ "Character", wrapLabels[1] },
            };
            int wrap = static_cast<int>(wrap_);
            if (builder.Choice(
                    Field(
                        WorldTextComponentPropertyIds::Wrap,
                        TEXT::Localize("ui.inspector.worldText.wrap"),
                        TEXT::Localize("ui.inspector.worldText.wrapHelp")),
                    wrap,
                    wrapChoices)) {
                wrap_ = static_cast<WrapMode>(std::clamp(wrap, 0, 1));
                changed = true;
            }
            changed = builder.FloatRange(
                Field(
                    WorldTextComponentPropertyIds::LineHeightMultiplier,
                    TEXT::Localize("ui.inspector.worldText.lineHeight"),
                    TEXT::Localize(
                        "ui.inspector.worldText.lineHeightHelp")),
                lineHeightMultiplier_,
                0.25f,
                4.0f,
                0.01f) || changed;
            changed = builder.Float(
                Field(
                    WorldTextComponentPropertyIds::LetterSpacing,
                    TEXT::Localize("ui.inspector.worldText.letterSpacing"),
                    TEXT::Localize(
                        "ui.inspector.worldText.letterSpacingHelp")),
                letterSpacing_) || changed;
        }

        if (builder.Section(
                TEXT::Localize("ui.inspector.worldText.appearance"), true)) {
            changed = builder.Color3(
                Field(
                    WorldTextComponentPropertyIds::Color,
                    TEXT::Localize("ui.inspector.worldText.color"),
                    TEXT::Localize("ui.inspector.worldText.colorHelp")),
                color_.x,
                color_.y,
                color_.z) || changed;
            changed = builder.Color3(
                Field(
                    WorldTextComponentPropertyIds::OutlineColor,
                    TEXT::Localize("ui.inspector.worldText.outlineColor"),
                    TEXT::Localize(
                        "ui.inspector.worldText.outlineColorHelp")),
                outlineColor_.x,
                outlineColor_.y,
                outlineColor_.z) || changed;
            changed = builder.FloatRange(
                Field(
                    WorldTextComponentPropertyIds::OutlineWidth,
                    TEXT::Localize("ui.inspector.worldText.outlineWidth"),
                    TEXT::Localize(
                        "ui.inspector.worldText.outlineWidthHelp")),
                outlineWidth_,
                0.0f,
                0.25f,
                0.0025f) || changed;
            changed = builder.FloatRange(
                Field(
                    WorldTextComponentPropertyIds::Opacity,
                    TEXT::Localize("ui.inspector.worldText.opacity"),
                    TEXT::Localize("ui.inspector.worldText.opacityHelp")),
                opacity_,
                0.0f,
                1.0f,
                0.01f) || changed;
            changed = builder.FloatRange(
                Field(
                    WorldTextComponentPropertyIds::RevealAmount,
                    TEXT::Localize("ui.inspector.worldText.reveal"),
                    TEXT::Localize("ui.inspector.worldText.revealHelp")),
                revealAmount_,
                0.0f,
                1.0f,
                0.01f) || changed;

            const std::array<std::string, 3u> facingLabels{
                TEXT::Localize("ui.inspector.worldText.fixed"),
                TEXT::Localize("ui.inspector.worldText.cameraFacing"),
                TEXT::Localize("ui.inspector.worldText.fixedYAxis"),
            };
            const std::array facingChoices{
                AuthoringChoice{ "Fixed", facingLabels[0] },
                AuthoringChoice{ "CameraFacing", facingLabels[1] },
                AuthoringChoice{ "FixedYAxis", facingLabels[2] },
            };
            int facing = static_cast<int>(facing_);
            if (builder.Choice(
                    Field(
                        WorldTextComponentPropertyIds::Facing,
                        TEXT::Localize("ui.inspector.worldText.facing"),
                        TEXT::Localize("ui.inspector.worldText.facingHelp")),
                    facing,
                    facingChoices)) {
                facing_ = static_cast<FacingMode>(
                    std::clamp(facing, 0, 2));
                changed = true;
            }
        }

        if (builder.Section(
                TEXT::Localize("ui.inspector.worldText.advanced"), false)) {
            changed = builder.Vec2(
                Field(
                    WorldTextComponentPropertyIds::Pivot,
                    TEXT::Localize("ui.inspector.worldText.pivot"),
                    TEXT::Localize("ui.inspector.worldText.pivotHelp")),
                pivot_.x,
                pivot_.y) || changed;
            changed = builder.Int(
                Field(
                    WorldTextComponentPropertyIds::TransparentSortPriority,
                    TEXT::Localize("ui.inspector.worldText.sortPriority"),
                    TEXT::Localize(
                        "ui.inspector.worldText.sortPriorityHelp")),
                transparentSortPriority_) || changed;
        }

        Sanitize();
        if (changed) NotifyRenderStateDirty();
    }

    void WorldTextComponent::Sanitize() noexcept {
        TEXT::TruncateUtf8(
            text_,
            TEXT_RENDERING::kSdfTextMaximumUtf8Bytes);
        TEXT::TruncateUtf8(fontAssetPath_, kMaximumFontAssetPathBytes);
        layoutSize_ = SanitizeSize(layoutSize_);
        pivot_ = SanitizePivot(pivot_);
        color_ = SanitizeColor(color_, { 1.0f, 1.0f, 1.0f });
        outlineColor_ = SanitizeColor(outlineColor_, {});
        fontHeight_ = ClampFinite(
            fontHeight_, 0.5f, 0.001f, 10000.0f);
        lineHeightMultiplier_ = ClampFinite(
            lineHeightMultiplier_, 1.2f, 0.25f, 4.0f);
        letterSpacing_ = ClampFinite(
            letterSpacing_, 0.0f, -10000.0f, 10000.0f);
        outlineWidth_ = ClampFinite(
            outlineWidth_, 0.04f, 0.0f, 0.25f);
        opacity_ = ClampFinite(opacity_, 1.0f, 0.0f, 1.0f);
        revealAmount_ = ClampFinite(
            revealAmount_, 1.0f, 0.0f, 1.0f);
        transparentSortPriority_ = std::clamp(
            transparentSortPriority_, -32768, 32767);
        if (static_cast<uint32_t>(horizontalAlign_) >
            static_cast<uint32_t>(HorizontalAlign::Right)) {
            horizontalAlign_ = HorizontalAlign::Left;
        }
        if (static_cast<uint32_t>(verticalAlign_) >
            static_cast<uint32_t>(VerticalAlign::Bottom)) {
            verticalAlign_ = VerticalAlign::Top;
        }
        if (static_cast<uint32_t>(wrap_) >
            static_cast<uint32_t>(WrapMode::Character)) {
            wrap_ = WrapMode::None;
        }
        if (static_cast<uint32_t>(facing_) >
            static_cast<uint32_t>(FacingMode::FixedYAxis)) {
            facing_ = FacingMode::CameraFacing;
        }
    }

    void WorldTextComponent::NotifyRenderStateDirty() {
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

} // namespace HIKARI
