#pragma once
#include "Assets/Text/HIKARI_TextStyleAsset.h"
namespace HIKARI {
struct TextStyleBinding {
    AssetGuid style, font;
    bool overrideFont = false, overrideSize = false, overrideColor = false, overrideEffects = false;
    void Write(nlohmann::json&) const;
    void Read(const nlohmann::json&);
    bool Inspect(IInspectorBuilder&, bool allowSizeOverride = true);
};
// 内部の長さ単位は Canvas pixels。WorldText の換算は描画投影側で一度だけ行う。
TextStyleAsset ResolveTextStyle(const TextAssetStore*, const TextStyleBinding&, TextStyleAsset local,
                                const TextStyleAsset* preview = nullptr);
} // namespace HIKARI
