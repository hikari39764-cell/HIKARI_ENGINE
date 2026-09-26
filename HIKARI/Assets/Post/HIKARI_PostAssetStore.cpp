#include "Assets/Post/HIKARI_PostAssetStore.h"

#include <utility>

namespace HIKARI::POST {
namespace {
// エンジンの内容は実行中に変わらないので、成功した読み込みだけを保持する。
template <class Asset>
std::shared_ptr<const Asset> LoadBuiltin(std::unordered_map<std::string, std::shared_ptr<const Asset>>& cache,
                                         std::string_view id, const std::filesystem::path& path,
                                         bool (*load)(const std::filesystem::path&, Asset&, std::string&),
                                         std::string& error) {
    if (const auto found = cache.find(std::string(id)); found != cache.end()) {
        return found->second;
    }
    if (path.empty()) {
        error = "Unknown engine post content: " + std::string(id);
        return {};
    }
    auto asset = std::make_shared<Asset>();
    if (!load(path, *asset, error)) {
        return {};
    }
    std::shared_ptr<const Asset> result = std::move(asset);
    cache.emplace(std::string(id), result);
    return result;
}
} // namespace

void PostAssetStore::Configure(const AssetDatabase* database) {
    effects_.Configure(database);
    profiles_.Configure(database);
    ClearProfileDraft();
}

std::shared_ptr<const PostEffectDefinition> PostAssetStore::LoadEffect(std::string_view id, std::string& error) const {
    if (id.starts_with(kBuiltinPostEffectPrefix)) {
        return LoadBuiltin(builtinEffects_, id, ResolveBuiltinPostEffectPath(id), &LoadPostEffectDefinition, error);
    }
    return effects_.Load(AssetGuid{std::string(id)}, error);
}

std::shared_ptr<const PostProfileAsset> PostAssetStore::LoadProfile(std::string_view id, std::string& error) const {
    if (draft_ && id == draftId_) {
        return draft_;
    }
    if (id.starts_with(kBuiltinPostProfilePrefix)) {
        return LoadBuiltin(builtinProfiles_, id, ResolveBuiltinPostProfilePath(id), &LoadPostProfileAsset, error);
    }
    return profiles_.Load(AssetGuid{std::string(id)}, error);
}

void PostAssetStore::SetProfileDraft(std::string_view id, std::shared_ptr<const PostProfileAsset> draft) {
    draftId_ = id;
    draft_ = std::move(draft);
}

void PostAssetStore::ClearProfileDraft() noexcept {
    draftId_.clear();
    draft_.reset();
}

} // namespace HIKARI::POST
