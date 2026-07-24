#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Assets/Sequence/HIKARI_SequenceAsset.h"

namespace HIKARI {

    class AssetDatabase;

    class SequenceAssetStore {
    public:
        void SetAssetDatabase(const AssetDatabase* assetDatabase) noexcept;
        std::shared_ptr<const SequenceAsset> Load(
            const AssetGuid& guid,
            std::string* outError = nullptr);
        void Invalidate(const AssetGuid& guid);
        void Clear();

    private:
        struct CacheEntry {
            std::shared_ptr<const SequenceAsset> asset{};
            std::filesystem::file_time_type lastWriteTime{};
        };

        const AssetDatabase* assetDatabase_ = nullptr;
        std::unordered_map<std::string, CacheEntry> cache_{};
    };

} // namespace HIKARI
