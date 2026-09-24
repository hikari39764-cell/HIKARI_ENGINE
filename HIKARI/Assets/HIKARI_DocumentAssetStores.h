#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "Assets/Animation/HIKARI_AnimationStateMachineAsset.h"
#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Assets/Dialogue/HIKARI_DialogueAsset.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"

namespace HIKARI {
class AssetDatabase;
namespace ASSETS::INTERNAL {
template <class Asset>
struct SourceAssetCacheEntry {
    std::shared_ptr<const Asset> asset;
    std::filesystem::file_time_type lastWriteTime{};
};
template <class Asset>
using SourceAssetCache = std::unordered_map<std::string, SourceAssetCacheEntry<Asset>>;
} // namespace ASSETS::INTERNAL
class AnimationStateMachineAssetStore {
public:
    void SetAssetDatabase(const AssetDatabase* database) noexcept;
    std::shared_ptr<const AnimationStateMachineAsset> Load(const AssetGuid& guid, std::string* outError = nullptr);
    void Invalidate(const AssetGuid& guid);
    void Clear();

private:
    const AssetDatabase* database_ = nullptr;
    ASSETS::INTERNAL::SourceAssetCache<AnimationStateMachineAsset> cache_;
};

class SequenceAssetStore {
public:
    void SetAssetDatabase(const AssetDatabase* assetDatabase) noexcept;
    std::shared_ptr<const SequenceAsset> Load(const AssetGuid& guid, std::string* outError = nullptr);
    void Invalidate(const AssetGuid& guid);
    void Clear();

private:
    const AssetDatabase* assetDatabase_ = nullptr;
    ASSETS::INTERNAL::SourceAssetCache<SequenceAsset> cache_;
};

class DialogueAssetStore final {
public:
    void SetAssetDatabase(const AssetDatabase* database) noexcept;
    std::shared_ptr<const DialogueAsset> Load(const AssetGuid& guid, std::string* outError = nullptr);
    std::shared_ptr<const DIALOGUE::DialogueProjectStyle> LoadProjectStyle(std::string* outError = nullptr);
    void Invalidate(const AssetGuid& guid);
    void InvalidateProjectStyle() noexcept;
    void Clear();

private:
    const AssetDatabase* database_ = nullptr;
    ASSETS::INTERNAL::SourceAssetCache<DialogueAsset> cache_;
    std::shared_ptr<const DIALOGUE::DialogueProjectStyle> projectStyle_{};
};

} // namespace HIKARI
