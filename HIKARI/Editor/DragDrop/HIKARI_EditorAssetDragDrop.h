#pragma once

#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"

namespace HIKARI::EDITOR {

constexpr const char* kAssetGuidPayload = "HIKARI_ASSET_GUID";

struct DroppedAssetPayload {
    AssetGuid guid{};
    const AssetRecord* record = nullptr;
};

bool BeginAssetDragSource(const AssetRecord& record);
bool AcceptAssetDrop(const AssetDatabase& assetDatabase, DroppedAssetPayload& outPayload);
// moduleAssetTypeId が空でなければ、モジュールが登記したその種類の資産だけを受ける。
bool AcceptAssetDropOfType(const AssetDatabase& assetDatabase, AssetType requiredType, DroppedAssetPayload& outPayload,
                           std::string_view moduleAssetTypeId = {});

} // namespace HIKARI::EDITOR
