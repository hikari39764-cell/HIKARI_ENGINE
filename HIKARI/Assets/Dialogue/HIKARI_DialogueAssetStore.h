#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Assets/Dialogue/HIKARI_DialogueAsset.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"

namespace HIKARI {

    class AssetDatabase;

    class DialogueAssetStore final {
    public:
        void SetAssetDatabase(const AssetDatabase* database) noexcept;
        std::shared_ptr<const DialogueAsset> Load(
            const AssetGuid& guid,
            std::string* outError = nullptr);
        std::shared_ptr<const DIALOGUE::DialogueProjectStyle>
            LoadProjectStyle(std::string* outError = nullptr);
        void Invalidate(const AssetGuid& guid);
        void InvalidateProjectStyle() noexcept;
        void Clear();

    private:
        const AssetDatabase* database_ = nullptr;
        std::unordered_map<std::string,
            std::shared_ptr<const DialogueAsset>> cache_{};
        std::shared_ptr<const DIALOGUE::DialogueProjectStyle>
            projectStyle_{};
    };

} // namespace HIKARI
