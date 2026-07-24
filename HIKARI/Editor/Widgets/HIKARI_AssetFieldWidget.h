#pragma once

#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI {

    class AssetDatabase;
    struct EditorSelection;

    namespace EDITOR {

        struct AssetFieldOptions {
            std::string_view label{};
            AssetType requiredType = AssetType::Unknown;
            bool allowClear = true;
            bool allowLocate = true;
            bool allowCopyGuid = true;
            bool drawLabel = true;
        };

        bool DrawAssetField(
            const AssetDatabase* assetDatabase,
            const AssetFieldOptions& options,
            std::string& inOutGuid,
            EditorSelection* selection = nullptr);

    } // namespace EDITOR

} // namespace HIKARI
