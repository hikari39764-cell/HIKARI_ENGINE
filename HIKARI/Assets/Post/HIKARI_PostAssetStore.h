#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Assets/HIKARI_AssetFileCache.h"
#include "Assets/Post/HIKARI_PostAsset.h"

namespace HIKARI::POST {

// エンジン内蔵（engine.post.*）とプロジェクト資産（GUID）の後処理効果・プロファイルを同じ ID で引く。
class PostAssetStore {
public:
    void Configure(const AssetDatabase* database);
    std::shared_ptr<const PostEffectDefinition> LoadEffect(std::string_view id, std::string& error) const;
    std::shared_ptr<const PostProfileAsset> LoadProfile(std::string_view id, std::string& error) const;
    // エディターで編集中の未保存プロファイル。保存か破棄まで、この内容で描画する。
    void SetProfileDraft(std::string_view id, std::shared_ptr<const PostProfileAsset> draft);
    void ClearProfileDraft() noexcept;

private:
    using EffectCache = AssetFileCache<PostEffectDefinition, AssetType::PostEffect, &LoadPostEffectDefinition>;
    using ProfileCache = AssetFileCache<PostProfileAsset, AssetType::PostProfile, &LoadPostProfileAsset>;
    EffectCache effects_;
    ProfileCache profiles_;
    mutable std::unordered_map<std::string, std::shared_ptr<const PostEffectDefinition>> builtinEffects_;
    mutable std::unordered_map<std::string, std::shared_ptr<const PostProfileAsset>> builtinProfiles_;
    std::string draftId_;
    std::shared_ptr<const PostProfileAsset> draft_;
};

} // namespace HIKARI::POST
