#pragma once

#include "Assets/HIKARI_AssetDatabase.h"

namespace HIKARI::EDITOR {

    constexpr const char* kAssetGuidPayload = "HIKARI_ASSET_GUID";

    struct DroppedAssetPayload {
        AssetGuid guid{};
        const AssetRecord* record = nullptr;
    };

    bool BeginAssetDragSource(const AssetRecord& record);
    bool AcceptAssetDrop(const AssetDatabase& assetDatabase, DroppedAssetPayload& outPayload);
    bool AcceptAssetDropOfType(
        const AssetDatabase& assetDatabase,
        AssetType requiredType,
        DroppedAssetPayload& outPayload);

} // namespace HIKARI::EDITOR
