#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Assets/Animation/HIKARI_AnimationStateMachineAsset.h"

namespace HIKARI {

    class AssetDatabase;

    class AnimationStateMachineAssetStore {
    public:
        void SetAssetDatabase(const AssetDatabase* database) noexcept;
        std::shared_ptr<const AnimationStateMachineAsset> Load(
            const AssetGuid& guid,
            std::string* outError = nullptr);
        void Invalidate(const AssetGuid& guid);
        void Clear();

    private:
        const AssetDatabase* database_ = nullptr;
        std::unordered_map<std::string,
            std::shared_ptr<const AnimationStateMachineAsset>> cache_{};
    };

} // namespace HIKARI
