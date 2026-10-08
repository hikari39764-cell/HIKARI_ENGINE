#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetFileCache.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAsset.h"

namespace HIKARI::RENDERSTYLE {

// エンジン内蔵（engine.style.*）とプロジェクト資産（GUID）の描画の様式を同じ ID で引く。
class RenderStyleAssetStore {
public:
    void Configure(const AssetDatabase* database);
    std::shared_ptr<const RenderStyleAsset> Load(std::string_view id, std::string& error) const;
    // エディターで編集中の未保存の様式。保存か破棄まで、この内容で描画する。
    void SetDraft(std::string_view id, std::shared_ptr<const RenderStyleAsset> draft);
    void ClearDraft() noexcept;

private:
    using StyleCache = AssetFileCache<RenderStyleAsset, AssetType::RenderStyle, &LoadRenderStyleAsset>;
    StyleCache styles_;
    BuiltinAssetCache<RenderStyleAsset> builtinStyles_;
    std::string draftId_;
    std::shared_ptr<const RenderStyleAsset> draft_;
};

} // namespace HIKARI::RENDERSTYLE
