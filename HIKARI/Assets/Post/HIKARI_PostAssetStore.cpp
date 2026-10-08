#include "Assets/Post/HIKARI_PostAssetStore.h"

#include <utility>

namespace HIKARI::POST {
void PostAssetStore::Configure(const AssetDatabase* database) {
    effects_.Configure(database);
    profiles_.Configure(database);
    ClearProfileDraft();
}

std::shared_ptr<const PostEffectDefinition> PostAssetStore::LoadEffect(std::string_view id, std::string& error) const {
    if (id.starts_with(kBuiltinPostEffectPrefix)) {
        return builtinEffects_.Load(id, ResolveBuiltinPostEffectPath(id), &LoadPostEffectDefinition, error);
    }
    return effects_.Load(AssetGuid{std::string(id)}, error);
}

std::shared_ptr<const PostProfileAsset> PostAssetStore::LoadProfile(std::string_view id, std::string& error) const {
    if (draft_ && id == draftId_) {
        return draft_;
    }
    if (id.starts_with(kBuiltinPostProfilePrefix)) {
        return builtinProfiles_.Load(id, ResolveBuiltinPostProfilePath(id), &LoadPostProfileAsset, error);
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
