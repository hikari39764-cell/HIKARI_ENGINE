#include "Assets/Animation/HIKARI_AnimationStateMachineAssetStore.h"

#include "Assets/HIKARI_AssetDatabase.h"

namespace HIKARI {

    void AnimationStateMachineAssetStore::SetAssetDatabase(
        const AssetDatabase* database) noexcept {
        if (database_ != database) cache_.clear();
        database_ = database;
    }

    std::shared_ptr<const AnimationStateMachineAsset>
        AnimationStateMachineAssetStore::Load(
            const AssetGuid& guid,
            std::string* outError) {
        if (!guid.IsValid() || database_ == nullptr) {
            if (outError != nullptr) {
                *outError = "Animation state machine asset store is not ready";
            }
            return {};
        }
        if (const auto found = cache_.find(guid.value);
            found != cache_.end()) {
            if (outError != nullptr) outError->clear();
            return found->second;
        }
        const AssetRecord* record = database_->FindByGuid(guid);
        if (record == nullptr ||
            record->type != AssetType::AnimationStateMachine) {
            if (outError != nullptr) {
                *outError = "Animation state machine asset record was not found";
            }
            return {};
        }
        const std::filesystem::path path = record->sourcePath.is_absolute()
            ? record->sourcePath
            : (database_->GetProjectRoot() / record->sourcePath)
                .lexically_normal();
        auto loaded = std::make_shared<AnimationStateMachineAsset>();
        if (!LoadAnimationStateMachineAsset(
                path,
                record->guid,
                *loaded,
                outError)) {
            return {};
        }
        cache_[guid.value] = loaded;
        return loaded;
    }

    void AnimationStateMachineAssetStore::Invalidate(const AssetGuid& guid) {
        cache_.erase(guid.value);
    }

    void AnimationStateMachineAssetStore::Clear() {
        cache_.clear();
    }

} // namespace HIKARI
