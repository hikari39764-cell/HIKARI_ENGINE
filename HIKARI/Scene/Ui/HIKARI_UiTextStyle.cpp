#include "Scene/Ui/HIKARI_UiTextStyle.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace HIKARI {
namespace {
nlohmann::json Color(const GameProjectUiColor& c) { return {c.r, c.g, c.b, c.a}; }
GameProjectUiColor ReadColor(const nlohmann::json& in, const char* key, GameProjectUiColor fallback) {
    if (const auto* node = JSONREAD::FindMember(in, key)) {
        const auto v = JSONREAD::Vec4Or(*node, {fallback.r, fallback.g, fallback.b, fallback.a});
        if (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w))
            return {std::clamp(v.x, 0.f, 16.f), std::clamp(v.y, 0.f, 16.f), std::clamp(v.z, 0.f, 16.f),
                    std::clamp(v.w, 0.f, 1.f)};
    }
    return fallback;
}
void InspectColor(IInspectorBuilder& b, const char* id, const char* label, GameProjectUiColor& color) {
    MATH::Vec3 rgb{color.r, color.g, color.b};
    b.Color3(AuthoringFieldDescriptor{id, TEXT::Localize(label).c_str()}, rgb.x, rgb.y, rgb.z);
    color.r = rgb.x;
    color.g = rgb.y;
    color.b = rgb.z;
    const std::string alpha = std::string(id) + "Alpha";
    b.FloatRange(AuthoringFieldDescriptor{alpha, TEXT::Localize("uix.opacity").c_str()}, color.a, 0, 1);
}
} // namespace
void UiTextStyle::Write(nlohmann::json& out) const {
    out["lineHeight"] = lineHeight;
    out["letterSpacing"] = letterSpacing;
    out["reveal"] = reveal;
    out["wrap"] = static_cast<int>(wrap);
    out["outlineWidth"] = outlineWidth;
    out["outlineColor"] = Color(outline);
    out["shadowOffset"] = {shadowX, shadowY};
    out["shadowColor"] = Color(shadow);
    out["glowColor"] = Color(glow.glowColor);
    out["glowWidth"] = glow.glowWidth;
    out["glowIntensity"] = glow.glowIntensity;
}
void UiTextStyle::Read(const nlohmann::json& in) {
    lineHeight = JSONREAD::ClampedNumberMemberOr(in, "lineHeight", 1.15f, .5f, 5);
    letterSpacing = JSONREAD::ClampedNumberMemberOr(in, "letterSpacing", 0, -128, 256);
    reveal = JSONREAD::ClampedNumberMemberOr(in, "reveal", 1, 0, 1);
    outlineWidth = JSONREAD::ClampedNumberMemberOr(in, "outlineWidth", 1.5f, 0, 32);
    wrap = static_cast<GameProjectUiTextWrap>(std::clamp(JSONREAD::IntegerMemberOr(in, "wrap", 1), 0, 1));
    outline = ReadColor(in, "outlineColor", {.0156863f, .1686275f, .5058824f, 1});
    shadow = ReadColor(in, "shadowColor", {0, 0, 0, .55f});
    MATH::Vec2 offset{2, 2};
    if (const auto* v = JSONREAD::FindMember(in, "shadowOffset"))
        offset = JSONREAD::Vec2Or(*v, offset);
    shadowX = std::clamp(std::isfinite(offset.x) ? offset.x : 2.f, -256.f, 256.f);
    shadowY = std::clamp(std::isfinite(offset.y) ? offset.y : 2.f, -256.f, 256.f);
    glow.glowColor = ReadColor(in, "glowColor", {});
    glow.glowWidth = JSONREAD::ClampedNumberMemberOr(in, "glowWidth", 0, 0, 32);
    glow.glowIntensity = JSONREAD::ClampedNumberMemberOr(in, "glowIntensity", 0, 0, 16);
}
void UiTextStyle::Apply(GameProjectUiTextRequest& r) const noexcept {
    r.lineHeightMultiplier = lineHeight;
    r.letterSpacing = letterSpacing;
    r.revealAmount = reveal;
    r.wrap = wrap;
    r.outlineWidth = outlineWidth;
    r.outlineColor = outline;
    r.shadowColor = shadow;
    r.shadowOffsetX = shadowX;
    r.shadowOffsetY = shadowY;
}
void UiTextStyle::InspectContent(IInspectorBuilder& b) {
    const auto L = [](const char* key) { return TEXT::Localize(key).c_str(); };

    b.FloatRange(AuthoringFieldDescriptor{"reveal", L("uix.reveal")}, reveal, 0, 1);
    const std::array<AuthoringChoice, 2> choices{{{"0", L("uix.noWrap")}, {"1", L("uix.characterWrap")}}};
    int index = static_cast<int>(wrap);
    if (b.Choice(AuthoringFieldDescriptor{"wrap", L("uix.wrap")}, index, choices))
        wrap = static_cast<GameProjectUiTextWrap>(std::clamp(index, 0, 1));
}
void UiTextStyle::Inspect(IInspectorBuilder& b, bool contentControls) {
    const auto L = [](const char* key) { return TEXT::Localize(key).c_str(); };
    if (b.Section(L("uix.typography"))) {
        b.FloatRange(AuthoringFieldDescriptor{"lineHeight", L("uix.lineHeight")}, lineHeight, .5f, 5);
        b.FloatRange(AuthoringFieldDescriptor{"letterSpacing", L("uix.letterSpacing")}, letterSpacing, -128, 256);
        if (contentControls)
            InspectContent(b);
    }
    if (b.Section(L("uix.outline"), false)) {
        b.FloatRange(AuthoringFieldDescriptor{"outlineWidth", L("uix.width")}, outlineWidth, 0, 32);
        InspectColor(b, "outlineColor", "uix.color", outline);
    }
    if (b.Section(L("uix.shadow"), false)) {
        b.Vec2(AuthoringFieldDescriptor{"shadowOffset", L("uix.offset")}, shadowX, shadowY);
        InspectColor(b, "shadowColor", "uix.color", shadow);
    }
    if (b.Section(L("uix.glow"), false)) {
        b.FloatRange(AuthoringFieldDescriptor{"glowWidth", L("uix.width")}, glow.glowWidth, 0, 32);
        b.FloatRange(AuthoringFieldDescriptor{"glowIntensity", L("uix.intensity")}, glow.glowIntensity, 0, 16);
        InspectColor(b, "glowColor", "uix.color", glow.glowColor);
    }
}
} // namespace HIKARI
