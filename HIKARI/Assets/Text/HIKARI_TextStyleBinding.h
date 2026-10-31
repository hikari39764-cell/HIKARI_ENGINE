#pragma once
#include "Assets/HIKARI_AssetTypeEnum.h"
#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
namespace HIKARI {
struct TextStyleBinding {
    AssetGuid style, font;
    bool overrideFont = false, overrideSize = false, overrideColor = false, overrideEffects = false;
    bool operator==(const TextStyleBinding&) const = default;
    void Write(nlohmann::json&) const;
    void Read(const nlohmann::json&);
    bool Inspect(IInspectorBuilder&, bool allowSizeOverride = true);
};
// 保存・読み取り・検査器・説明が共有する項目表。AllowSizeOverride は文字の大きさの上書きを選ばせるか（WorldText の大きさ
// は fontHeight が持つので false。保存はどちらも同じ）。
template <bool AllowSizeOverride>
inline constexpr auto kTextStyleBindingFields = [](auto&& field) {
    using S = TextStyleBinding;
    using namespace JSONFIELDS;
    constexpr auto styled = [](const S& s) { return s.style.IsValid(); };
    constexpr const char* kOverrides = "Local overrides";
    field("textStyleAssetGuid", &S::style, Label{"Text style"}, AssetRef{AssetType::TextStyle}, Common{});
    field("fontAssetGuid", &S::font, Label{"Font asset"}, AssetRef{AssetType::Font},
          Show{[](const S& s) { return !s.style.IsValid() || s.overrideFont; }}, Common{});
    field("overrideFont", &S::overrideFont, Label{"Font asset"}, Show{styled}, Group{kOverrides});
    field("overrideTextSize", &S::overrideSize, Label{"Pixel Height"},
          Show{[](const S& s) { return AllowSizeOverride && s.style.IsValid(); }}, Group{kOverrides});
    field("overrideTextColor", &S::overrideColor, Label{"Color"}, Show{styled}, Group{kOverrides});
    field("overrideTextEffects", &S::overrideEffects, Label{"Appearance"}, Show{styled}, Group{kOverrides});
};
// 内部の長さ単位は Canvas pixels。WorldText の換算は描画投影側で一度だけ行う。
TextStyleAsset ResolveTextStyle(const TextAssetStore*, const TextStyleBinding&, TextStyleAsset local,
                                const TextStyleAsset* preview = nullptr);
} // namespace HIKARI
