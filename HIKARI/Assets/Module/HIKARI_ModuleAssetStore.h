#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetFileCache.h"

namespace HIKARI {

// モジュールが清単に登記した種類の資産の中身（JSON の原文）。引擎は解釈せず、読むのはモジュール。
struct ModuleAssetText {
    // 清単に登記された種類（拡張子から決まる）。登記の無い拡張子なら空。
    std::string typeId{};
    std::string text{};
    // 中身から作る版。中身が変われば変わり、同じ中身なら読み直しても同じ。0 にはならない。
    uint64_t revision = 0u;
};

bool LoadModuleAssetText(const std::filesystem::path& path, ModuleAssetText& out, std::string& error);

// モジュールの資産を GUID で読んで保持する。資産庫の内容が変わるまでファイルを読み直さない。
class ModuleAssetStore {
public:
    void Configure(const AssetDatabase* database) { cache_.Configure(database); }
    // moduleAssetTypeId は清単の typeId。その種類でない資産は渡さずに理由を返す。毎フレーム呼んでよい。
    std::shared_ptr<const ModuleAssetText> Load(const AssetGuid& guid, std::string_view moduleAssetTypeId,
                                                std::string& error) const;

private:
    AssetFileCache<ModuleAssetText, AssetType::ModuleAsset, &LoadModuleAssetText> cache_{};
};

} // namespace HIKARI
