#pragma once

#include <string>
#include <string_view>

#include "RenderContracts/Asset/HIKARI_AssetTypes.h"

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
    float width = 0.0f;
    // 空でなければ、モジュールが清単に登記したその種類（typeId）の資産だけを選べる（requiredType は ModuleAsset）。
    std::string_view moduleAssetTypeId{};
};

bool DrawAssetField(const AssetDatabase* assetDatabase, const AssetFieldOptions& options, std::string& inOutGuid,
                    EditorSelection* selection = nullptr);

// 一覧・欠落表示に使う GUID の先頭 8 文字。
inline std::string ShortGuid(std::string_view guid) { return std::string(guid.substr(0, 8)); }

} // namespace EDITOR

} // namespace HIKARI
