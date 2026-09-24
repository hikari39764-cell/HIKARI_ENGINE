#include "Assets/Text/HIKARI_TextStyleBinding.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/HIKARI_JsonRead.h"
namespace HIKARI {
void TextStyleBinding::Write(nlohmann::json& data) const {
    data["textStyleAssetGuid"] = style.value;
    data["fontAssetGuid"] = font.value;
    data["overrideFont"] = overrideFont;
    data["overrideTextSize"] = overrideSize;
    data["overrideTextColor"] = overrideColor;
    data["overrideTextEffects"] = overrideEffects;
}
void TextStyleBinding::Read(const nlohmann::json& data) {
    style.value = JSONREAD::StringMemberOr(data, "textStyleAssetGuid", {});
    font.value = JSONREAD::StringMemberOr(data, "fontAssetGuid", {});
    overrideFont = JSONREAD::BooleanMemberOr(data, "overrideFont", false);
    overrideSize = JSONREAD::BooleanMemberOr(data, "overrideTextSize", false);
    overrideColor = JSONREAD::BooleanMemberOr(data, "overrideTextColor", false);
    overrideEffects = JSONREAD::BooleanMemberOr(data, "overrideTextEffects", false);
}
bool TextStyleBinding::Inspect(IInspectorBuilder& b, bool allowSizeOverride) {
    const auto L = [](const char* k) { return TEXT::LocalizeCStr(k); };
    bool changed = b.AssetIdPicker(AuthoringFieldDescriptor{"textStyleAssetGuid", L("textstyle.asset")},
                                   AssetType::TextStyle, style.value);
    if (style.IsValid()) {
        if (b.Button(L("textstyle.restore"))) {
            overrideFont = overrideSize = overrideColor = overrideEffects = false;
            changed = true;
        }
        if (b.Section(L("textstyle.overrides"), false)) {
            changed = b.Bool(AuthoringFieldDescriptor{"overrideFont", L("textstyle.font")}, overrideFont) || changed;
            if (allowSizeOverride)
                changed =
                    b.Bool(AuthoringFieldDescriptor{"overrideTextSize", L("Pixel Height")}, overrideSize) || changed;
            changed = b.Bool(AuthoringFieldDescriptor{"overrideTextColor", L("Color")}, overrideColor) || changed;
            changed = b.Bool(AuthoringFieldDescriptor{"overrideTextEffects", L("uix.appearance")}, overrideEffects) ||
                      changed;
        }
    }
    if (!style.IsValid() || overrideFont)
        changed = b.AssetIdPicker(AuthoringFieldDescriptor{"fontAssetGuid", L("textstyle.font")}, AssetType::Font,
                                  font.value) ||
                  changed;
    return changed;
}
TextStyleAsset ResolveTextStyle(const TextAssetStore* store, const TextStyleBinding& binding, TextStyleAsset local) {
    local.font = binding.font;
    if (!store || !binding.style.IsValid())
        return local;
    std::string error;
    const auto shared = store->LoadStyle(binding.style, error);
    if (!shared)
        return local;
    auto resolved = *shared;
    if (binding.overrideFont)
        resolved.font = local.font;
    if (binding.overrideSize)
        resolved.pixelHeight = local.pixelHeight;
    if (binding.overrideColor)
        resolved.color = local.color;
    if (binding.overrideEffects)
        resolved.effects = local.effects;
    resolved.effects.reveal = local.effects.reveal;
    resolved.effects.wrap = local.effects.wrap;
    return resolved;
}
} // namespace HIKARI
