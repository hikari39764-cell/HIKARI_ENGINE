#include "Assets/RenderStyle/HIKARI_RenderStyleAssetStore.h"

#include <utility>

namespace HIKARI::RENDERSTYLE {

void RenderStyleAssetStore::Configure(const AssetDatabase* database) {
    styles_.Configure(database);
    ClearDraft();
}

std::shared_ptr<const RenderStyleAsset> RenderStyleAssetStore::Load(std::string_view id, std::string& error) const {
    if (draft_ && id == draftId_) {
        return draft_;
    }
    if (id.starts_with(kBuiltinRenderStylePrefix)) {
        return builtinStyles_.Load(id, ResolveBuiltinRenderStylePath(id), &LoadRenderStyleAsset, error);
    }
    return styles_.Load(AssetGuid{std::string(id)}, error);
}

void RenderStyleAssetStore::SetDraft(std::string_view id, std::shared_ptr<const RenderStyleAsset> draft) {
    draftId_ = id;
    draft_ = std::move(draft);
}

void RenderStyleAssetStore::ClearDraft() noexcept {
    draftId_.clear();
    draft_.reset();
}

} // namespace HIKARI::RENDERSTYLE
