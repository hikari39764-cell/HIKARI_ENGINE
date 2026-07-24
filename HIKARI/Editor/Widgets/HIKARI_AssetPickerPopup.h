#pragma once

#include <array>
#include <string>

#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI {

    class AssetDatabase;

    namespace EDITOR {

        struct AssetPickerPopupState {
            std::array<char, 160> searchBuffer{};
        };

        bool DrawAssetPickerPopup(
            const char* popupId,
            const AssetDatabase& assetDatabase,
            AssetType requiredType,
            std::string& inOutGuid,
            AssetPickerPopupState& state);

    } // namespace EDITOR

} // namespace HIKARI
