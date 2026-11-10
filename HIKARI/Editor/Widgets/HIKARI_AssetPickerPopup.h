#pragma once

#include <array>
#include <string>
#include <string_view>

#include "RenderContracts/Asset/HIKARI_AssetTypes.h"

namespace HIKARI {

class AssetDatabase;

namespace EDITOR {

struct AssetPickerPopupState {
    std::array<char, 160> searchBuffer{};
    std::string selectedFolder{};
};

// 選ぶ資産の種類の表示名（"Model" など）。モジュールが登記した種類（moduleAssetTypeId）は清単の表示名。
std::string AssetKindDisplayName(AssetType type, std::string_view moduleAssetTypeId = {});

// moduleAssetTypeId が空でなければ、モジュールが登記したその種類の資産だけを並べる。
bool DrawAssetPickerPopup(const char* popupId, const AssetDatabase& assetDatabase, AssetType requiredType,
                          std::string& inOutGuid, AssetPickerPopupState& state,
                          std::string_view moduleAssetTypeId = {});

} // namespace EDITOR

} // namespace HIKARI
